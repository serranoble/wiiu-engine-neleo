#include "Core/ECS/Systems/RigidBodyPhysicsSystem.hpp"
#include <algorithm>
#include <cmath>

namespace WiiU {

namespace {

constexpr float kDefaultLinearSleepSq  = 0.008f; // (~0.09 m/s)^2
constexpr float kDefaultAngularSleepSq = 0.015f; // (~0.12 rad/s)^2
constexpr float kSleepTimeThreshold    = 0.85f;
constexpr float kBaumgarteBeta         = 0.22f;
constexpr float kPenetrationSlop       = 0.008f;
constexpr float kRestitutionVelSlop    = 1.15f;
constexpr float kMaxAngularSpeed       = 18.0f;
constexpr float kMaxLinearSpeed        = 45.0f;

[[nodiscard]] inline float ClampAbsSpeedSq(Point3D& v, float maxSpeed) noexcept {
    const float lenSq = Point3D::Dot(v, v);
    const float maxSq = maxSpeed * maxSpeed;
    if (lenSq > maxSq && lenSq > 1e-8f) {
        const float scale = maxSpeed * Point3D::FastInvSqrt(lenSq);
        v = v * scale;
        return maxSq;
    }
    return lenSq;
}

[[nodiscard]] inline Quaternion IntegrateQuaternion(const Quaternion& q, const Point3D& omega, float dt) noexcept {
    const Quaternion qNorm = q.Normalized();
    const Quaternion spin = Quaternion(omega.x, omega.y, omega.z, 0.0f) * qNorm;
    const float halfDt = 0.5f * dt;
    return Quaternion(
        qNorm.x + spin.x * halfDt,
        qNorm.y + spin.y * halfDt,
        qNorm.z + spin.z * halfDt,
        qNorm.w + spin.w * halfDt
    ).Normalized();
}

inline void BuildTangentBasis(const Point3D& n, Point3D& outT1, Point3D& outT2) noexcept {
    const Point3D ref = (std::abs(n.x) < 0.57735f) ? Point3D{1.0f, 0.0f, 0.0f} : Point3D{0.0f, 1.0f, 0.0f};
    outT1 = Point3D::Cross(n, ref).Normalized();
    outT2 = Point3D::Cross(n, outT1).Normalized();
}

[[nodiscard]] inline Point3D GetWorldPointVelocity(const RigidBodyComponent* rb, const Point3D& r) noexcept {
    if (!rb || rb->motionType == RigidBodyMotionType::Static) {
        return Point3D{0.0f, 0.0f, 0.0f};
    }
    return rb->linearVelocity + Point3D::Cross(rb->angularVelocity, r);
}

[[nodiscard]] inline float ComputeDirectionalInvMass(const RigidBodyComponent* rb,
                                                     const Point3D& r,
                                                     const Point3D& dir) noexcept {
    if (!rb || rb->motionType != RigidBodyMotionType::Dynamic || rb->invMass <= 0.0f) {
        return 0.0f;
    }
    const Point3D rCrossDir = Point3D::Cross(r, dir);
    const Point3D invIRCrossDir = rb->ComputeWorldInvInertia(rCrossDir);
    const float angularTerm = Point3D::Dot(Point3D::Cross(invIRCrossDir, r), dir);
    return rb->invMass + std::max(0.0f, angularTerm);
}

inline void ApplyImpulseSigned(RigidBodyComponent* rb,
                               const Point3D& r,
                               const Point3D& impulse) noexcept {
    if (!rb || rb->motionType != RigidBodyMotionType::Dynamic || rb->invMass <= 0.0f) {
        return;
    }
    rb->linearVelocity = rb->linearVelocity + (impulse * rb->invMass);
    rb->angularVelocity = rb->angularVelocity + rb->ComputeWorldInvInertia(Point3D::Cross(r, impulse));
}

[[nodiscard]] inline Point3D LocalToWorldPoint(const TransformComponent& tf,
                                               const RigidBodyComponent* rb,
                                               const Point3D& localOffset) noexcept {
    const Point3D rotated = rb ? rb->orientation.Normalized().Rotate(localOffset) : localOffset;
    return tf.position + rotated;
}

inline void WakeProxyIfDynamic(RigidBodyComponent* rb) noexcept {
    if (rb && rb->motionType == RigidBodyMotionType::Dynamic) {
        rb->WakeUp();
    }
}

inline float ReadAxis(const Point3D& v, int axis) noexcept {
    if (axis == 0) return v.x;
    if (axis == 1) return v.y;
    return v.z;
}

inline Point3D MakeAxisNormal(int axis, float sign) noexcept {
    if (axis == 0) return Point3D{sign, 0.0f, 0.0f};
    if (axis == 1) return Point3D{0.0f, sign, 0.0f};
    return Point3D{0.0f, 0.0f, sign};
}

} // namespace

// =============================================================================
// Ciclo Principal y Configuración
// =============================================================================

void RigidBodyPhysicsSystem::Clear() noexcept {
    m_proxyCount = 0;
    m_contactCount = 0;
    m_constraintCount = 0;
    m_telemetry = PhysicsTelemetry{};
}

int32_t RigidBodyPhysicsSystem::AddConstraint(const PhysicsConstraint& constraint) noexcept {
    if (m_constraintCount >= MAX_CONSTRAINTS || !constraint.entityA.IsValid()) {
        return -1;
    }
    const size_t idx = m_constraintCount++;
    m_constraints[idx] = constraint;
    m_constraints[idx].active = true;
    return static_cast<int32_t>(idx);
}

void RigidBodyPhysicsSystem::Update(ECSWorld& world, float dt) noexcept {
    const float clampedDt = std::clamp(dt, 0.0f, 1.0f / 20.0f);
    if (clampedDt <= 0.0001f) {
        return;
    }

    BuildProxyList(world);
    const float subDt = clampedDt / static_cast<float>(m_substeps);

    for (uint32_t step = 0; step < m_substeps; ++step) {
        ApplySpringForces(subDt);
        IntegrateVelocities(subDt);
        GenerateContacts();
        ResolveKCCInteractions(world, subDt);
        SolveVelocityContacts(subDt);
        IntegratePositions(subDt);
        SolveXPBDConstraints(subDt);
    }

    UpdateSleepingAndClearAccumulators(world, clampedDt);
}

// =============================================================================
// Construcción de Proxies e Integración
// =============================================================================

void RigidBodyPhysicsSystem::BuildProxyList(ECSWorld& world) noexcept {
    m_proxyCount = 0;

    world.ForEach<TransformComponent, ColliderComponent>(
        [this, &world](Entity entity, TransformComponent& tf, ColliderComponent& col) {
            if (!col.isSolid || (col.layer & Layer_Trigger) != 0U || m_proxyCount >= MAX_BODY_PROXIES) {
                return;
            }

            RigidBodyComponent* rb = world.GetComponent<RigidBodyComponent>(entity);
            const bool isStaticEnv = ((col.layer & Layer_StaticWorld) != 0U);
            if (!rb && !isStaticEnv) {
                return;
            }

            BodyProxy& proxy = m_proxies[m_proxyCount++];
            proxy.entity = entity;
            proxy.transform = &tf;
            proxy.collider = &col;
            proxy.rigidBody = rb;
            proxy.isDynamic = (rb != nullptr && rb->motionType == RigidBodyMotionType::Dynamic && rb->invMass > 0.0f);
            proxy.isStaticImmovable = !proxy.isDynamic;
        }
    );
}

int32_t RigidBodyPhysicsSystem::FindProxyIndex(Entity entity) const noexcept {
    if (!entity.IsValid()) {
        return -1;
    }
    for (size_t i = 0; i < m_proxyCount; ++i) {
        if (m_proxies[i].entity == entity) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void RigidBodyPhysicsSystem::ApplySpringForces(float /*dt*/) noexcept {
    for (size_t i = 0; i < m_constraintCount; ++i) {
        const PhysicsConstraint& c = m_constraints[i];
        if (!c.active || c.type != PhysicsConstraintType::Spring) {
            continue;
        }

        const int32_t idxA = FindProxyIndex(c.entityA);
        if (idxA < 0) continue;
        BodyProxy& pA = m_proxies[static_cast<size_t>(idxA)];

        const int32_t idxB = FindProxyIndex(c.entityB);
        if (c.entityB.IsValid() && idxB < 0) continue;
        BodyProxy* pB = (idxB >= 0) ? &m_proxies[static_cast<size_t>(idxB)] : nullptr;

        const Point3D wA = LocalToWorldPoint(*pA.transform, pA.rigidBody, c.localAnchorA);
        const Point3D wB = pB ? LocalToWorldPoint(*pB->transform, pB->rigidBody, c.localAnchorB) : c.localAnchorB;

        const Point3D delta = wB - wA;
        const float dist = delta.Length();
        if (dist < 1e-5f) {
            continue;
        }

        const Point3D dir = delta * (1.0f / dist);
        const float stretch = dist - c.restLength;
        if (std::abs(stretch) > 0.02f) {
            WakeProxyIfDynamic(pA.rigidBody);
            if (pB) WakeProxyIfDynamic(pB->rigidBody);
        }

        const Point3D rA = wA - pA.transform->position;
        const Point3D rB = pB ? (wB - pB->transform->position) : Point3D{0.0f, 0.0f, 0.0f};
        const Point3D velA = GetWorldPointVelocity(pA.rigidBody, rA);
        const Point3D velB = pB ? GetWorldPointVelocity(pB->rigidBody, rB) : Point3D{0.0f, 0.0f, 0.0f};
        const float relVelAlongDir = Point3D::Dot(velB - velA, dir);

        // Fuerza sobre A en dirección A -> B: Hooke (+k*stretch) + Amortiguamiento (+c*v_rel)
        const float springForceMag = c.stiffness * stretch + c.damping * relVelAlongDir;
        const Point3D forceOnA = dir * springForceMag;

        if (pA.isDynamic && !pA.rigidBody->isSleeping) {
            pA.rigidBody->ApplyForceAtPoint(forceOnA, wA, pA.transform->position);
        }
        if (pB && pB->isDynamic && !pB->rigidBody->isSleeping) {
            pB->rigidBody->ApplyForceAtPoint(forceOnA * -1.0f, wB, pB->transform->position);
        }
    }
}

void RigidBodyPhysicsSystem::IntegrateVelocities(float dt) noexcept {
    for (size_t i = 0; i < m_proxyCount; ++i) {
        BodyProxy& p = m_proxies[i];
        if (!p.isDynamic || p.rigidBody->isSleeping) {
            continue;
        }

        RigidBodyComponent& rb = *p.rigidBody;
        const Point3D linAccel = (m_gravity * rb.gravityScale) + (rb.forceAccum * rb.invMass);
        const Point3D angAccel = rb.ComputeWorldInvInertia(rb.torqueAccum);

        const float linDampFactor = 1.0f / (1.0f + rb.linearDamping * dt);
        const float angDampFactor = 1.0f / (1.0f + rb.angularDamping * dt);

        rb.linearVelocity  = (rb.linearVelocity + (linAccel * dt)) * linDampFactor;
        rb.angularVelocity = (rb.angularVelocity + (angAccel * dt)) * angDampFactor;

        (void)ClampAbsSpeedSq(rb.linearVelocity, kMaxLinearSpeed);
        (void)ClampAbsSpeedSq(rb.angularVelocity, kMaxAngularSpeed);
    }
}

// =============================================================================
// Generación de Contactos Analíticos
// =============================================================================

void RigidBodyPhysicsSystem::GenerateContacts() noexcept {
    m_contactCount = 0;
    if (m_proxyCount < 2) return;

    // [OPT-275] Fase Ancha (Broadphase) mediante SpatialHashGrid Zero-Alloc O(N) amortizado
    m_broadphaseGrid.Clear();
    for (size_t i = 0; i < m_proxyCount; ++i) {
        const BodyProxy& p = m_proxies[i];
        const Point3D c = p.transform->position + p.collider->localOffset;
        if (p.collider->shape == ColliderShape::Sphere) {
            m_broadphaseGrid.InsertSphere(static_cast<uint32_t>(i), c, p.collider->radius);
        } else {
            m_broadphaseGrid.Insert(static_cast<uint32_t>(i),
                                    c - p.collider->halfExtents,
                                    c + p.collider->halfExtents);
        }
    }

    uint32_t candidates[MAX_BODY_PROXIES]{};

    for (size_t i = 0; i < m_proxyCount; ++i) {
        const BodyProxy& a = m_proxies[i];
        const Point3D centerA = a.transform->position + a.collider->localOffset;

        size_t candCount = 0;
        if (a.collider->shape == ColliderShape::Sphere) {
            candCount = m_broadphaseGrid.QuerySphere(centerA, a.collider->radius, candidates, MAX_BODY_PROXIES);
        } else {
            candCount = m_broadphaseGrid.QueryAABB(centerA - a.collider->halfExtents,
                                                   centerA + a.collider->halfExtents,
                                                   candidates, MAX_BODY_PROXIES);
        }

        for (size_t ci = 0; ci < candCount; ++ci) {
            const size_t j = static_cast<size_t>(candidates[ci]);
            if (j <= i || j >= m_proxyCount) {
                continue;
            }
            if (m_contactCount >= MAX_CONTACTS) {
                return;
            }

            const BodyProxy& b = m_proxies[j];
            if (a.isStaticImmovable && b.isStaticImmovable) {
                continue;
            }

            const bool sleepA = a.isDynamic && a.rigidBody->isSleeping;
            const bool sleepB = b.isDynamic && b.rigidBody->isSleeping;
            if ((sleepA && (sleepB || b.isStaticImmovable)) ||
                (sleepB && a.isStaticImmovable)) {
                continue;
            }

            const bool layersMatch =
                ((a.collider->layer & b.collider->collisionMask) != 0U) &&
                ((b.collider->layer & a.collider->collisionMask) != 0U);
            if (!layersMatch) {
                continue;
            }

            const Point3D centerB = b.transform->position + b.collider->localOffset;

            Point3D normalAB{0.0f, 1.0f, 0.0f};
            Point3D contactPt{0.0f, 0.0f, 0.0f};
            float penetration = 0.0f;
            bool hit = false;

            if (a.collider->shape == ColliderShape::Sphere && b.collider->shape == ColliderShape::Sphere) {
                hit = IntersectSphereSphere(centerA, a.collider->radius, centerB, b.collider->radius,
                                            normalAB, contactPt, penetration);
            } else if (a.collider->shape == ColliderShape::Sphere && b.collider->shape == ColliderShape::AABB) {
                hit = IntersectSphereAABB(centerA, a.collider->radius, centerB, b.collider->halfExtents,
                                          normalAB, contactPt, penetration);
            } else if (a.collider->shape == ColliderShape::AABB && b.collider->shape == ColliderShape::Sphere) {
                hit = IntersectSphereAABB(centerB, b.collider->radius, centerA, a.collider->halfExtents,
                                          normalAB, contactPt, penetration);
                if (hit) {
                    // Invertir normal para mantener convención estricta A -> B
                    normalAB = normalAB * -1.0f;
                }
            } else {
                hit = IntersectAABBAABB(centerA, a.collider->halfExtents, centerB, b.collider->halfExtents,
                                        normalAB, contactPt, penetration);
            }

            if (hit && penetration > 0.0f) {
                if (a.isDynamic && b.isDynamic) {
                    if (sleepA && Point3D::Dot(b.rigidBody->linearVelocity, b.rigidBody->linearVelocity) > 0.04f) {
                        a.rigidBody->WakeUp();
                    }
                    if (sleepB && Point3D::Dot(a.rigidBody->linearVelocity, a.rigidBody->linearVelocity) > 0.04f) {
                        b.rigidBody->WakeUp();
                    }
                }

                RigidBodyContact& c = m_contacts[m_contactCount++];
                c.entityA = a.entity;
                c.entityB = b.entity;
                c.proxyIdxA = static_cast<int16_t>(i);
                c.proxyIdxB = static_cast<int16_t>(j);
                c.normal = normalAB;
                c.contactPoint = contactPt;
                c.penetration = penetration;
                c.normalImpulseSum = 0.0f;
                c.tangentImpulseSum1 = 0.0f;
                c.tangentImpulseSum2 = 0.0f;
            }
        }
    }
}

void RigidBodyPhysicsSystem::UpdateKinematicCharactersFlat(ECSWorld& world, float dt, float groundY) noexcept {
    const float stepDt = std::clamp(dt, 0.0f, 0.05f);
    if (stepDt <= 0.0001f) {
        return;
    }

    world.ForEach<TransformComponent, KinematicCharacterComponent>(
        [stepDt, groundY](Entity /*entity*/, TransformComponent& tf, KinematicCharacterComponent& kcc) {
            if (kcc.jumpRequested) {
                if (kcc.isGrounded) {
                    kcc.velocity.y = kcc.jumpSpeed;
                    kcc.isGrounded = false;
                    kcc.isJumping = true;
                }
                kcc.jumpRequested = false;
            }

            const float accelRate = kcc.isGrounded ? 18.0f : 6.0f;
            const float smooth = std::clamp(accelRate * stepDt, 0.0f, 1.0f);
            kcc.velocity.x += (kcc.desiredMove.x - kcc.velocity.x) * smooth;
            kcc.velocity.z += (kcc.desiredMove.z - kcc.velocity.z) * smooth;
            kcc.horizontalSpeed = std::sqrt(kcc.velocity.x * kcc.velocity.x + kcc.velocity.z * kcc.velocity.z);

            if (kcc.horizontalSpeed > 0.15f) {
                kcc.facingAngle = std::atan2(kcc.velocity.x, kcc.velocity.z);
            }

            if (!kcc.isGrounded) {
                kcc.velocity.y -= std::abs(kcc.gravity) * stepDt;
                kcc.velocity.y = std::max(kcc.velocity.y, -24.0f);
            }

            tf.position = tf.position + (kcc.velocity * stepDt);

            const float halfH = kcc.capsuleHeight * 0.5f;
            const float feetY = tf.position.y - halfH;
            kcc.groundDistance = feetY - groundY;

            if (feetY <= groundY + 0.03f && kcc.velocity.y <= 0.05f) {
                tf.position.y = groundY + halfH;
                kcc.velocity.y = 0.0f;
                kcc.isGrounded = true;
                kcc.isJumping = false;
            } else {
                kcc.isGrounded = false;
            }

            tf.rotation.y = kcc.facingAngle;
            tf.isDirty = true;
        }
    );
}

void RigidBodyPhysicsSystem::ResolveKCCInteractions(ECSWorld& world, float /*dt*/) noexcept {
    world.ForEach<TransformComponent, KinematicCharacterComponent>(
        [this](Entity /*kccEntity*/, TransformComponent& kccTf, KinematicCharacterComponent& kcc) {
            const float kccRadius = kcc.capsuleRadius;
            const float halfH = kcc.capsuleHeight * 0.5f;
            const float segMinY = kccTf.position.y - halfH + kccRadius;
            const float segMaxY = std::max(segMinY, kccTf.position.y + halfH - kccRadius);

            for (size_t i = 0; i < m_proxyCount; ++i) {
                BodyProxy& p = m_proxies[i];
                if (!p.isDynamic || p.rigidBody == nullptr) {
                    continue;
                }

                const Point3D bodyCenter = p.transform->position + p.collider->localOffset;
                const Point3D kccClosest{
                    kccTf.position.x,
                    std::clamp(bodyCenter.y, segMinY, segMaxY),
                    kccTf.position.z
                };

                Point3D normalKB{0.0f, 1.0f, 0.0f};
                Point3D contactPt{0.0f, 0.0f, 0.0f};
                float penetration = 0.0f;
                bool hit = false;

                if (p.collider->shape == ColliderShape::Sphere) {
                    hit = IntersectSphereSphere(kccClosest, kccRadius, bodyCenter, p.collider->radius,
                                                normalKB, contactPt, penetration);
                } else {
                    hit = IntersectSphereAABB(kccClosest, kccRadius, bodyCenter, p.collider->halfExtents,
                                              normalKB, contactPt, penetration);
                }

                if (!hit || penetration <= 0.001f) {
                    continue;
                }

                p.rigidBody->WakeUp();

                // Empuje bidireccional: personaje cede parcialmente ante cuerpos muy pesados e impulsa al rígido
                const float bodyWeightRatio = std::clamp(p.rigidBody->mass / 8.0f, 0.08f, 0.55f);
                kccTf.position.x -= normalKB.x * (penetration * bodyWeightRatio);
                kccTf.position.z -= normalKB.z * (penetration * bodyWeightRatio);
                kccTf.MarkDirty();

                const Point3D kccHorizVel{kcc.velocity.x, 0.0f, kcc.velocity.z};
                const float pushAlongN = std::max(0.0f, Point3D::Dot(kccHorizVel - p.rigidBody->linearVelocity, normalKB));
                const float impulseMag = (pushAlongN * 1.15f + penetration * 4.5f) * std::min(p.rigidBody->mass, 4.0f);
                const Point3D impulse = normalKB * impulseMag;
                const Point3D rBody = contactPt - p.transform->position;
                p.rigidBody->ApplyImpulseAtOffset(impulse, rBody);
            }
        }
    );
}

// =============================================================================
// Solver Iterativo Sequential Impulses + Fricción de Coulomb
// =============================================================================

void RigidBodyPhysicsSystem::SolveVelocityContacts(float dt) noexcept {
    if (m_contactCount == 0 || dt <= 1e-6f) {
        return;
    }

    const float invDt = 1.0f / dt;

    for (uint32_t iter = 0; iter < m_solverIterations; ++iter) {
        for (size_t ci = 0; ci < m_contactCount; ++ci) {
            RigidBodyContact& c = m_contacts[ci];
            const int32_t idxA = (c.proxyIdxA >= 0 && static_cast<size_t>(c.proxyIdxA) < m_proxyCount)
                                 ? static_cast<int32_t>(c.proxyIdxA) : FindProxyIndex(c.entityA);
            const int32_t idxB = (c.proxyIdxB >= 0 && static_cast<size_t>(c.proxyIdxB) < m_proxyCount)
                                 ? static_cast<int32_t>(c.proxyIdxB) : FindProxyIndex(c.entityB);
            if (idxA < 0 || idxB < 0) continue;

            BodyProxy& pA = m_proxies[static_cast<size_t>(idxA)];
            BodyProxy& pB = m_proxies[static_cast<size_t>(idxB)];

            const Point3D rA = c.contactPoint - pA.transform->position;
            const Point3D rB = c.contactPoint - pB.transform->position;

            // 1. Impulso Normal + Estabilización de Baumgarte
            const float invMassN = ComputeDirectionalInvMass(pA.rigidBody, rA, c.normal) +
                                   ComputeDirectionalInvMass(pB.rigidBody, rB, c.normal);
            if (invMassN > 1e-6f) {
                const Point3D vA = GetWorldPointVelocity(pA.rigidBody, rA);
                const Point3D vB = GetWorldPointVelocity(pB.rigidBody, rB);
                const float sepVel = Point3D::Dot(vB - vA, c.normal);

                const float restA = pA.rigidBody ? pA.rigidBody->restitution : 0.3f;
                const float restB = pB.rigidBody ? pB.rigidBody->restitution : 0.3f;
                const float combinedRest = (sepVel < -kRestitutionVelSlop) ? std::max(restA, restB) : 0.0f;

                const float penetrationBias = (kBaumgarteBeta * invDt) * std::max(0.0f, c.penetration - kPenetrationSlop);
                const float rawDeltaLambda = (-(1.0f + combinedRest) * sepVel + penetrationBias) / invMassN;

                const float oldSum = c.normalImpulseSum;
                c.normalImpulseSum = std::max(0.0f, oldSum + rawDeltaLambda);
                const float appliedLambda = c.normalImpulseSum - oldSum;

                if (appliedLambda != 0.0f) {
                    const Point3D normalImpulse = c.normal * appliedLambda;
                    ApplyImpulseSigned(pA.rigidBody, rA, normalImpulse * -1.0f);
                    ApplyImpulseSigned(pB.rigidBody, rB, normalImpulse);
                }
            }

            // 2. Impulsos Tangenciales (Cono de Fricción de Coulomb 2D)
            const float fricA = pA.rigidBody ? pA.rigidBody->friction : 0.6f;
            const float fricB = pB.rigidBody ? pB.rigidBody->friction : 0.6f;
            const float mu = std::sqrt(std::max(0.0f, fricA * fricB));
            const float maxFriction = mu * c.normalImpulseSum;
            if (maxFriction <= 1e-6f) {
                continue;
            }

            Point3D t1{}, t2{};
            BuildTangentBasis(c.normal, t1, t2);

            auto solveTangentAxis = [&](const Point3D& tAxis, float& tangentSum) {
                const float invMassT = ComputeDirectionalInvMass(pA.rigidBody, rA, tAxis) +
                                       ComputeDirectionalInvMass(pB.rigidBody, rB, tAxis);
                if (invMassT <= 1e-6f) return;

                const Point3D rv = GetWorldPointVelocity(pB.rigidBody, rB) - GetWorldPointVelocity(pA.rigidBody, rA);
                const float vt = Point3D::Dot(rv, tAxis);
                const float rawDeltaJt = -vt / invMassT;

                const float oldJt = tangentSum;
                tangentSum = std::clamp(oldJt + rawDeltaJt, -maxFriction, maxFriction);
                const float appliedJt = tangentSum - oldJt;
                if (appliedJt != 0.0f) {
                    const Point3D frictionImpulse = tAxis * appliedJt;
                    ApplyImpulseSigned(pA.rigidBody, rA, frictionImpulse * -1.0f);
                    ApplyImpulseSigned(pB.rigidBody, rB, frictionImpulse);
                }
            };

            solveTangentAxis(t1, c.tangentImpulseSum1);
            solveTangentAxis(t2, c.tangentImpulseSum2);
        }
    }
}

void RigidBodyPhysicsSystem::IntegratePositions(float dt) noexcept {
    for (size_t i = 0; i < m_proxyCount; ++i) {
        BodyProxy& p = m_proxies[i];
        if (!p.isDynamic || p.rigidBody->isSleeping) {
            continue;
        }

        RigidBodyComponent& rb = *p.rigidBody;
        TransformComponent& tf = *p.transform;

        tf.position = tf.position + (rb.linearVelocity * dt);
        rb.orientation = IntegrateQuaternion(rb.orientation, rb.angularVelocity, dt);

        tf.worldMatrix = Matrix4x4::FromTRS(tf.position, rb.orientation, tf.scale);
        tf.isDirty = false;
    }
}

// =============================================================================
// Solver Posicional XPBD (Distance & BallSocketPin)
// =============================================================================

void RigidBodyPhysicsSystem::SolveXPBDConstraints(float dt) noexcept {
    if (m_constraintCount == 0 || dt <= 1e-6f) {
        return;
    }

    const float invDtSq = 1.0f / (dt * dt);
    const uint32_t xpbdPasses = 4U;

    for (uint32_t pass = 0; pass < xpbdPasses; ++pass) {
        for (size_t i = 0; i < m_constraintCount; ++i) {
            const PhysicsConstraint& c = m_constraints[i];
            if (!c.active || c.type == PhysicsConstraintType::Spring) {
                continue;
            }

            const int32_t idxA = FindProxyIndex(c.entityA);
            if (idxA < 0) continue;
            BodyProxy& pA = m_proxies[static_cast<size_t>(idxA)];

            const int32_t idxB = FindProxyIndex(c.entityB);
            if (c.entityB.IsValid() && idxB < 0) continue;
            BodyProxy* pB = (idxB >= 0) ? &m_proxies[static_cast<size_t>(idxB)] : nullptr;

            const Point3D wA = LocalToWorldPoint(*pA.transform, pA.rigidBody, c.localAnchorA);
            const Point3D wB = pB ? LocalToWorldPoint(*pB->transform, pB->rigidBody, c.localAnchorB) : c.localAnchorB;

            const Point3D diff = wB - wA;
            const float dist = diff.Length();
            const float targetDist = (c.type == PhysicsConstraintType::BallSocketPin) ? 0.0f : c.restLength;
            const float error = dist - targetDist;

            if (std::abs(error) < 1e-5f) {
                continue;
            }

            WakeProxyIfDynamic(pA.rigidBody);
            if (pB) WakeProxyIfDynamic(pB->rigidBody);

            const Point3D n = (dist > 1e-5f) ? (diff * (1.0f / dist)) : Point3D{0.0f, 1.0f, 0.0f};
            const Point3D rA = wA - pA.transform->position;
            const Point3D rB = pB ? (wB - pB->transform->position) : Point3D{0.0f, 0.0f, 0.0f};

            const float wSum = ComputeDirectionalInvMass(pA.rigidBody, rA, n) +
                               (pB ? ComputeDirectionalInvMass(pB->rigidBody, rB, n) : 0.0f) +
                               (c.compliance * invDtSq);
            if (wSum <= 1e-6f) {
                continue;
            }

            // A debe moverse en +n y B en -n para reducir error = dist - targetDist
            const float posImpulse = error / wSum;
            const Point3D deltaP = n * posImpulse;

            if (pA.isDynamic) {
                pA.transform->position = pA.transform->position + (deltaP * pA.rigidBody->invMass);
                const Point3D angPos = pA.rigidBody->ComputeWorldInvInertia(Point3D::Cross(rA, deltaP));
                pA.rigidBody->orientation = IntegrateQuaternion(pA.rigidBody->orientation, angPos, 1.0f);
                pA.transform->worldMatrix = Matrix4x4::FromTRS(pA.transform->position, pA.rigidBody->orientation, pA.transform->scale);
                pA.transform->isDirty = false;
            }
            if (pB && pB->isDynamic) {
                pB->transform->position = pB->transform->position - (deltaP * pB->rigidBody->invMass);
                const Point3D angPosB = pB->rigidBody->ComputeWorldInvInertia(Point3D::Cross(rB, deltaP * -1.0f));
                pB->rigidBody->orientation = IntegrateQuaternion(pB->rigidBody->orientation, angPosB, 1.0f);
                pB->transform->worldMatrix = Matrix4x4::FromTRS(pB->transform->position, pB->rigidBody->orientation, pB->transform->scale);
                pB->transform->isDirty = false;
            }

            // Proyección bilateral de velocidad a lo largo de la restricción en la última iteración
            if (pass + 1U == xpbdPasses) {
                const Point3D vA = GetWorldPointVelocity(pA.rigidBody, rA);
                const Point3D vB = pB ? GetWorldPointVelocity(pB->rigidBody, rB) : Point3D{0.0f, 0.0f, 0.0f};
                const float relVelN = Point3D::Dot(vB - vA, n);
                const float invMassOnly = ComputeDirectionalInvMass(pA.rigidBody, rA, n) +
                                          (pB ? ComputeDirectionalInvMass(pB->rigidBody, rB, n) : 0.0f);
                if (invMassOnly > 1e-6f) {
                    const Point3D velCorr = n * ((relVelN * 0.65f) / invMassOnly);
                    ApplyImpulseSigned(pA.rigidBody, rA, velCorr);
                    if (pB) ApplyImpulseSigned(pB->rigidBody, rB, velCorr * -1.0f);
                }
            }
        }
    }
}

// =============================================================================
// Hibernación (Sleeping), Limpieza de Acumuladores y Telemetría
// =============================================================================

void RigidBodyPhysicsSystem::UpdateSleepingAndClearAccumulators(ECSWorld& world, float dt) noexcept {
    uint32_t activeCount = 0;
    uint32_t sleepCount = 0;

    world.ForEach<TransformComponent, RigidBodyComponent>(
        [&](Entity /*entity*/, TransformComponent& tf, RigidBodyComponent& rb) {
            rb.forceAccum = Point3D{0.0f, 0.0f, 0.0f};
            rb.torqueAccum = Point3D{0.0f, 0.0f, 0.0f};

            if (rb.motionType != RigidBodyMotionType::Dynamic) {
                return;
            }

            const float linSq = Point3D::Dot(rb.linearVelocity, rb.linearVelocity);
            const float angSq = Point3D::Dot(rb.angularVelocity, rb.angularVelocity);

            if (rb.canSleep) {
                if (linSq < kDefaultLinearSleepSq && angSq < kDefaultAngularSleepSq) {
                    rb.sleepTimer += dt;
                    if (rb.sleepTimer >= kSleepTimeThreshold) {
                        rb.isSleeping = true;
                        rb.linearVelocity = Point3D{0.0f, 0.0f, 0.0f};
                        rb.angularVelocity = Point3D{0.0f, 0.0f, 0.0f};
                    }
                } else {
                    rb.sleepTimer = 0.0f;
                    rb.isSleeping = false;
                }
            } else {
                rb.isSleeping = false;
            }

            if (rb.isSleeping) {
                ++sleepCount;
            } else {
                ++activeCount;
            }

            tf.worldMatrix = Matrix4x4::FromTRS(tf.position, rb.orientation, tf.scale);
            tf.isDirty = false;
        }
    );

    uint32_t activeConstraintCount = 0;
    for (size_t i = 0; i < m_constraintCount; ++i) {
        if (m_constraints[i].active) {
            ++activeConstraintCount;
        }
    }

    m_telemetry.activeBodies = activeCount;
    m_telemetry.sleepingBodies = sleepCount;
    m_telemetry.activeContacts = static_cast<uint32_t>(m_contactCount);
    m_telemetry.activeConstraints = activeConstraintCount;
    m_telemetry.solverIterations = m_solverIterations;
    m_telemetry.substeps = m_substeps;
}

// =============================================================================
// Utilidades de Gameplay y Pruebas Analíticas Estrechas
// =============================================================================

void RigidBodyPhysicsSystem::WakeAllBodies(ECSWorld& world) noexcept {
    world.ForEach<RigidBodyComponent>([](Entity /*e*/, RigidBodyComponent& rb) {
        rb.WakeUp();
    });
}

void RigidBodyPhysicsSystem::ApplyRadialExplosion(ECSWorld& world,
                                                  const Point3D& center,
                                                  float radius,
                                                  float peakImpulse,
                                                  float upwardBoost) noexcept {
    if (radius <= 0.01f) {
        return;
    }

    const float radiusSq = radius * radius;

    world.ForEach<TransformComponent, RigidBodyComponent>(
        [&](Entity /*entity*/, TransformComponent& tf, RigidBodyComponent& rb) {
            if (rb.motionType != RigidBodyMotionType::Dynamic || rb.invMass <= 0.0f) {
                return;
            }

            const Point3D delta = tf.position - center;
            const float distSq = Point3D::Dot(delta, delta);
            if (distSq > radiusSq) {
                return;
            }

            const float dist = std::sqrt(distSq);
            Point3D dir = (dist > 1e-4f) ? (delta * (1.0f / dist)) : Point3D{0.0f, 1.0f, 0.0f};
            dir.y += upwardBoost;
            dir = dir.Normalized();

            const float falloff = 1.0f - (dist / radius);
            const float impulseMag = peakImpulse * falloff;
            rb.WakeUp();

            // Aplicamos un pequeño offset inferior para inducir giro físico realista por torque
            const Point3D leverOffset{0.0f, -0.22f, 0.0f};
            rb.ApplyImpulseAtOffset(dir * impulseMag, leverOffset);
        }
    );
}

bool RigidBodyPhysicsSystem::IntersectSphereSphere(const Point3D& centerA, float radiusA,
                                                   const Point3D& centerB, float radiusB,
                                                   Point3D& outNormalAB,
                                                   Point3D& outContactPoint,
                                                   float& outPenetration) noexcept {
    const Point3D diff = centerB - centerA;
    const float sumRadii = radiusA + radiusB;
    const float distSq = Point3D::Dot(diff, diff);

    if (distSq >= sumRadii * sumRadii) {
        return false;
    }

    const float dist = std::sqrt(distSq);
    if (dist > 1e-5f) {
        outNormalAB = diff * (1.0f / dist);
    } else {
        outNormalAB = Point3D{0.0f, 1.0f, 0.0f};
    }

    outPenetration = sumRadii - dist;
    outContactPoint = centerA + (outNormalAB * (radiusA - outPenetration * 0.5f));
    return true;
}

bool RigidBodyPhysicsSystem::IntersectSphereAABB(const Point3D& sphereCenter, float sphereRadius,
                                                 const Point3D& boxCenter, const Point3D& boxHalfExtents,
                                                 Point3D& outNormalAB,
                                                 Point3D& outContactPoint,
                                                 float& outPenetration) noexcept {
    const Point3D boxMin = boxCenter - boxHalfExtents;
    const Point3D boxMax = boxCenter + boxHalfExtents;

    const Point3D closest{
        std::clamp(sphereCenter.x, boxMin.x, boxMax.x),
        std::clamp(sphereCenter.y, boxMin.y, boxMax.y),
        std::clamp(sphereCenter.z, boxMin.z, boxMax.z)
    };

    const Point3D diffBoxToSphere = sphereCenter - closest;
    const float distSq = Point3D::Dot(diffBoxToSphere, diffBoxToSphere);

    if (distSq > 1e-8f) {
        if (distSq >= sphereRadius * sphereRadius) {
            return false;
        }
        const float dist = std::sqrt(distSq);
        outNormalAB = (closest - sphereCenter) * (1.0f / dist);
        outPenetration = sphereRadius - dist;
        outContactPoint = closest;
        return true;
    }

    // Centro de la esfera contenido en el AABB: elegir cara de menor escape
    const Point3D local = sphereCenter - boxCenter;
    int bestAxis = 1;
    float bestDist = 1e30f;
    float bestSignAB = -1.0f;

    for (int axis = 0; axis < 3; ++axis) {
        const float extent = ReadAxis(boxHalfExtents, axis);
        const float coord = ReadAxis(local, axis);
        const float distPos = extent - coord;
        const float distNeg = extent + coord;

        if (distPos < bestDist) {
            bestDist = distPos;
            bestAxis = axis;
            bestSignAB = -1.0f; // Escape hacia +axis implica A -> B = -axis
        }
        if (distNeg < bestDist) {
            bestDist = distNeg;
            bestAxis = axis;
            bestSignAB = 1.0f;  // Escape hacia -axis implica A -> B = +axis
        }
    }

    outNormalAB = MakeAxisNormal(bestAxis, bestSignAB);
    outPenetration = bestDist + sphereRadius;
    outContactPoint = closest;
    return true;
}

bool RigidBodyPhysicsSystem::IntersectAABBAABB(const Point3D& centerA, const Point3D& halfExtA,
                                               const Point3D& centerB, const Point3D& halfExtB,
                                               Point3D& outNormalAB,
                                               Point3D& outContactPoint,
                                               float& outPenetration) noexcept {
    const Point3D delta = centerB - centerA;
    const float ox = (halfExtA.x + halfExtB.x) - std::abs(delta.x);
    if (ox <= 0.0f) return false;
    const float oy = (halfExtA.y + halfExtB.y) - std::abs(delta.y);
    if (oy <= 0.0f) return false;
    const float oz = (halfExtA.z + halfExtB.z) - std::abs(delta.z);
    if (oz <= 0.0f) return false;

    int bestAxis = 1;
    float minOverlap = oy;
    if (ox < minOverlap) {
        minOverlap = ox;
        bestAxis = 0;
    }
    if (oz < minOverlap) {
        minOverlap = oz;
        bestAxis = 2;
    }

    const float axisDelta = ReadAxis(delta, bestAxis);
    const float sign = (axisDelta >= 0.0f) ? 1.0f : -1.0f;
    outNormalAB = MakeAxisNormal(bestAxis, sign);
    outPenetration = minOverlap;

    // Punto representativo de contacto en el centro de la región de solapamiento AABB
    const Point3D minA = centerA - halfExtA;
    const Point3D maxA = centerA + halfExtA;
    const Point3D minB = centerB - halfExtB;
    const Point3D maxB = centerB + halfExtB;

    outContactPoint = Point3D{
        0.5f * (std::max(minA.x, minB.x) + std::min(maxA.x, maxB.x)),
        0.5f * (std::max(minA.y, minB.y) + std::min(maxA.y, maxB.y)),
        0.5f * (std::max(minA.z, minB.z) + std::min(maxA.z, maxB.z))
    };
    return true;
}

} // namespace WiiU
