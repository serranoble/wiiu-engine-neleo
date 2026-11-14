#include "Core/ECS/Systems/KinematicCharacterSystem.hpp"
#include "Audio/AudioEngine3D.hpp"
#include <cmath>
#include <algorithm>

namespace WiiU {

float KinematicCharacterSystem::GetTerrainHeight(float x, float z) noexcept {
    float h1 = 0.70f * std::sin(0.32f * x) * std::cos(0.32f * z);
    float h2 = 0.35f * std::sin(0.65f * x + 0.40f * z);
    float dx = x - 7.0f;
    float dz = z - 7.0f;
    float d2 = dx * dx + dz * dz;
    float basin = 1.35f * std::exp(-d2 / 16.0f);
    return h1 + h2 - basin;
}

Point3D KinematicCharacterSystem::GetTerrainNormal(float x, float z) noexcept {
    constexpr float eps = 0.05f;
    constexpr float inv2Eps = 1.0f / (2.0f * eps);
    float dh_dx = (GetTerrainHeight(x + eps, z) - GetTerrainHeight(x - eps, z)) * inv2Eps;
    float dh_dz = (GetTerrainHeight(x, z + eps) - GetTerrainHeight(x, z - eps)) * inv2Eps;
    Point3D n{-dh_dx, 1.0f, -dh_dz};
    return n.Normalized();
}

void KinematicCharacterSystem::ResolveSweptCollision(Point3D& pos, Point3D& vel, float radius, float height, float /*dt*/) noexcept {
    float feetY = pos.y - height * 0.5f;
    float headY = pos.y + height * 0.5f;

    // 1. [OPT-207] Colisión Swept contra Muralla de Ladrillos AABB
    if (headY > WALL_AABB.min.y && feetY < WALL_AABB.max.y) {
        float xmin = WALL_AABB.min.x - radius;
        float xmax = WALL_AABB.max.x + radius;
        float zmin = WALL_AABB.min.z - radius;
        float zmax = WALL_AABB.max.z + radius;

        if (pos.x >= xmin && pos.x <= xmax && pos.z >= zmin && pos.z <= zmax) {
            float dLeft   = pos.x - xmin;
            float dRight  = xmax - pos.x;
            float dBottom = pos.z - zmin;
            float dTop    = zmax - pos.z;

            float minPen = std::min({dLeft, dRight, dBottom, dTop});

            if (minPen == dLeft) {
                pos.x = xmin;
                if (vel.x > 0.0f) vel.x = 0.0f; // Slide response
            } else if (minPen == dRight) {
                pos.x = xmax;
                if (vel.x < 0.0f) vel.x = 0.0f;
            } else if (minPen == dBottom) {
                pos.z = zmin;
                if (vel.z > 0.0f) vel.z = 0.0f;
            } else {
                pos.z = zmax;
                if (vel.z < 0.0f) vel.z = 0.0f;
            }
        }
    }

    // 2. Colisión radial contra obstáculos esféricos (Peñascos)
    static constexpr ObstacleSphere SPHERES[] = {
        ROCK1_SPHERE,
        ROCK2_SPHERE
    };

    for (const auto& sph : SPHERES) {
        if (headY > sph.center.y - sph.radius && feetY < sph.center.y + sph.radius) {
            float dx = pos.x - sph.center.x;
            float dz = pos.z - sph.center.z;
            float distSq = dx * dx + dz * dz;
            float totalR = sph.radius + radius;

            if (distSq < totalR * totalR && distSq > 0.0001f) {
                float dist = std::sqrt(distSq);
                float nx = dx / dist;
                float nz = dz / dist;

                pos.x = sph.center.x + nx * totalR;
                pos.z = sph.center.z + nz * totalR;

                // Slide response: anular componente de velocidad entrante
                float dot = vel.x * nx + vel.z * nz;
                if (dot < 0.0f) {
                    vel.x -= dot * nx;
                    vel.z -= dot * nz;
                }
            }
        }
    }

    // 4. Perímetro límite del escenario [-14.0, 14.0]
    if (pos.x < -14.0f) { pos.x = -14.0f; if (vel.x < 0.0f) vel.x = 0.0f; }
    if (pos.x >  14.0f) { pos.x =  14.0f; if (vel.x > 0.0f) vel.x = 0.0f; }
    if (pos.z < -14.0f) { pos.z = -14.0f; if (vel.z < 0.0f) vel.z = 0.0f; }
    if (pos.z >  14.0f) { pos.z =  14.0f; if (vel.z > 0.0f) vel.z = 0.0f; }
}

bool KinematicCharacterSystem::CheckLineOfSight(const Point3D& from, const Point3D& to, float& outOcclusion) noexcept {
    Point3D dir = to - from;
    float dist = dir.Length();
    outOcclusion = 0.0f;
    if (dist < 0.001f) return false;
    Point3D unitDir = dir * (1.0f / dist);

    // 1. Ray-AABB Slab Test contra la muralla de ladrillos
    float tmin = 0.0f;
    float tmax = dist;
    bool wallHit = true;
    for (int i = 0; i < 3; ++i) {
        float o = (i == 0) ? from.x : ((i == 1) ? from.y : from.z);
        float d = (i == 0) ? unitDir.x : ((i == 1) ? unitDir.y : unitDir.z);
        float bmin = (i == 0) ? WALL_AABB.min.x : ((i == 1) ? WALL_AABB.min.y : WALL_AABB.min.z);
        float bmax = (i == 0) ? WALL_AABB.max.x : ((i == 1) ? WALL_AABB.max.y : WALL_AABB.max.z);

        if (std::abs(d) < 1e-6f) {
            if (o < bmin || o > bmax) {
                wallHit = false;
                break;
            }
        } else {
            float invD = 1.0f / d;
            float t1 = (bmin - o) * invD;
            float t2 = (bmax - o) * invD;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) {
                wallHit = false;
                break;
            }
        }
    }
    if (wallHit && tmax > 0.0f && tmin < dist) {
        outOcclusion = std::max(outOcclusion, 0.85f); // Atenuación acústica por mampostería sólida
    }

    // 2. Ray-Sphere Test contra peñascos
    auto testSphere = [&](const ObstacleSphere& sph, float occFactor) {
        Point3D m = from - sph.center;
        float b = Point3D::Dot(unitDir, m);
        float c = Point3D::Dot(m, m) - sph.radius * sph.radius;
        if (c > 0.0f && b > 0.0f) return;
        float disc = b * b - c;
        if (disc >= 0.0f) {
            float t = -b - std::sqrt(disc);
            if (t > 0.0f && t < dist) {
                outOcclusion = std::max(outOcclusion, occFactor);
            }
        }
    };
    testSphere(ROCK1_SPHERE, 0.75f);
    testSphere(ROCK2_SPHERE, 0.70f);

    // 3. Trazado continuo contra elevación del terreno (3 muestras equidistantes)
    for (int s = 1; s <= 3; ++s) {
        float frac = static_cast<float>(s) * 0.25f;
        Point3D p = from + dir * frac;
        float h = GetTerrainHeight(p.x, p.z);
        if (h > p.y + 0.10f) {
            float diff = h - p.y;
            float terrOcc = std::clamp(diff * 0.85f, 0.0f, 0.85f);
            outOcclusion = std::max(outOcclusion, terrOcc);
        }
    }

    return outOcclusion > 0.05f;
}

void KinematicCharacterSystem::Update(ECSWorld& world, float dt) {
    if (dt <= 0.0f || dt > 0.25f) {
        dt = 0.016667f; // Clamping de estabilidad numérica anti-delta spikes
    }

    world.ForEach<TransformComponent, KinematicCharacterComponent>(
        [dt](Entity /*entity*/, TransformComponent& transform, KinematicCharacterComponent& kcc) {
            // 1. Procesar solicitud de impulso de salto vertical
            if (kcc.jumpRequested) {
                if (kcc.isGrounded) {
                    kcc.velocity.y = kcc.jumpSpeed;
                    kcc.isGrounded = false;
                    kcc.isJumping = true;
                }
                kcc.jumpRequested = false;
            }

            // 2. Aceleración horizontal amortiguada hacia desiredMove
            float accelRate = kcc.isGrounded ? 18.0f : 6.0f; // Mayor tracción en suelo que en el aire
            float smoothFactor = std::clamp(accelRate * dt, 0.0f, 1.0f);
            kcc.velocity.x += (kcc.desiredMove.x - kcc.velocity.x) * smoothFactor;
            kcc.velocity.z += (kcc.desiredMove.z - kcc.velocity.z) * smoothFactor;
            kcc.horizontalSpeed = std::sqrt(kcc.velocity.x * kcc.velocity.x + kcc.velocity.z * kcc.velocity.z);

            // 3. Orientación visual del personaje (Yaw) hacia el rumbo de avance
            if (kcc.horizontalSpeed > 0.15f) {
                kcc.facingAngle = std::atan2(kcc.velocity.x, kcc.velocity.z);
            }

            // 4. Aplicación continua de gravedad si no está apoyado en suelo firme
            if (!kcc.isGrounded) {
                kcc.velocity.y -= kcc.gravity * dt;
                if (kcc.velocity.y < -24.0f) {
                    kcc.velocity.y = -24.0f; // Velocidad terminal de caída
                }
            }

            // 5. Integración de traslación
            Point3D newPos = transform.position;
            newPos.x += kcc.velocity.x * dt;
            newPos.z += kcc.velocity.z * dt;
            newPos.y += kcc.velocity.y * dt;

            // 6. [OPT-207] Resolución continua de colisión con obstáculos (Swept Collision & Slide)
            ResolveSweptCollision(newPos, kcc.velocity, kcc.capsuleRadius, kcc.capsuleHeight, dt);

            // 7. Detección continua de suelo y pendiente analítica
            float groundH = GetTerrainHeight(newPos.x, newPos.z);
            Point3D groundNorm = GetTerrainNormal(newPos.x, newPos.z);
            kcc.groundNormal = groundNorm;

            float feetY = newPos.y - kcc.capsuleHeight * 0.5f;
            kcc.groundDistance = feetY - groundH;

            if (feetY <= groundH + 0.05f && kcc.velocity.y <= 0.05f) {
                // Verificar ángulo transitable de pendiente
                if (groundNorm.y >= kcc.slopeLimitCos) {
                    // Suelo transitable: fijar pies a ras de suelo y cancelar velocidad descendente
                    newPos.y = groundH + kcc.capsuleHeight * 0.5f;
                    kcc.velocity.y = 0.0f;
                    kcc.isGrounded = true;
                    kcc.isJumping = false;

                    // Acumulador de pasos de audio 3D
                    if (kcc.horizontalSpeed > 0.4f) {
                        kcc.footstepDistanceAccum += kcc.horizontalSpeed * dt;
                        if (kcc.footstepDistanceAccum >= kcc.footstepInterval) {
                            kcc.footstepDistanceAccum = 0.0f;
                            AudioEngine3D::Instance().PlaySound3D(
                                AudioEngine3D::Instance().GetFootstepClip(),
                                AudioEmitter3D{newPos, Point3D{0.0f, 0.0f, 0.0f}, 1.0f, 15.0f, 1.0f, 0.40f, 1.0f, 0.0f, false, 70}
                            );
                        }
                    }
                } else {
                    // Pendiente excesiva (> 45°): resbalar cuesta abajo por fuerza de deslizamiento
                    kcc.isGrounded = false;
                    kcc.velocity.x += groundNorm.x * kcc.gravity * 0.65f * dt;
                    kcc.velocity.z += groundNorm.z * kcc.gravity * 0.65f * dt;
                }
            } else {
                kcc.isGrounded = false;
            }

            // Sincronizar transformación del componente ECS
            transform.position = newPos;
            transform.rotation.y = kcc.facingAngle;
            transform.isDirty = true;
        }
    );
}

} // namespace WiiU
