#include "Core/ECS/Systems/CollisionSystem.hpp"
#include <cmath>
#include <algorithm>

namespace WiiU {

bool CollisionSystem::IntersectSphereSphere(const Point3D& centerA, float radiusA,
                                            const Point3D& centerB, float radiusB,
                                            Point3D& outNormal, float& outPenetration) noexcept {
    // -------------------------------------------------------------------------
    // ALGORITMO: Intersección Esfera vs Esfera
    // Fuente: Christer Ericson (Real-Time Collision Detection, Cap. 4.3.1)
    // -------------------------------------------------------------------------
    // 1. Vector de desplazamiento relativo entre centros: d = cA - cB
    float dx = centerA.x - centerB.x;
    float dy = centerA.y - centerB.y;
    float dz = centerA.z - centerB.z;
    float distSq = dx * dx + dy * dy + dz * dz;
    float sumRadius = radiusA + radiusB;

    // Comprobación de corte temprana con distancia al cuadrado:
    // Si distSq >= (rA + rB)^2, no hay solapamiento geométrico posible.
    if (distSq >= sumRadius * sumRadius) {
        return false;
    }

    // 2. Cálculo de la distancia euclídea y vector normal unitario
    float dist = std::sqrt(distSq);
    if (dist > 1e-5f) {
        float invDist = 1.0f / dist;
        // Vector normal apuntando de B hacia A (dirección de expulsión para A)
        outNormal = Point3D{dx * invDist, dy * invDist, dz * invDist};
        outPenetration = sumRadius - dist;
    } else {
        // Caso degenerado: Centros coincidentes -> normal de escape por defecto hacia +Z
        outNormal = Point3D{0.0f, 0.0f, 1.0f};
        outPenetration = sumRadius;
    }
    return true;
}

bool CollisionSystem::IntersectSphereAABB(const Point3D& sphereCenter, float sphereRadius,
                                          const Point3D& boxMin, const Point3D& boxMax,
                                          Point3D& outNormal, float& outPenetration) noexcept {
    // -------------------------------------------------------------------------
    // ALGORITMO: Punto Más Cercano en AABB e Intersección con Esfera
    // Fuente: James Arvo (Graphics Gems 1990) & Christer Ericson (Cap. 5.1.3)
    // -------------------------------------------------------------------------
    // 1. Proyectar ortogonalmente el centro de la esfera sobre los límites del AABB:
    //    q_i = clamp(c_i, min_i, max_i) para cada eje X, Y, Z.
    float closestX = std::clamp(sphereCenter.x, boxMin.x, boxMax.x);
    float closestY = std::clamp(sphereCenter.y, boxMin.y, boxMax.y);
    float closestZ = std::clamp(sphereCenter.z, boxMin.z, boxMax.z);

    // Vector desde el punto más cercano al centro de la esfera: d = c - q
    float dx = sphereCenter.x - closestX;
    float dy = sphereCenter.y - closestY;
    float dz = sphereCenter.z - closestZ;
    float distSq = dx * dx + dy * dy + dz * dz;

    // Descarte temprano si la distancia al cuadrado excede el radio al cuadrado
    if (distSq > sphereRadius * sphereRadius) {
        return false;
    }

    // -------------------------------------------------------------------------
    // CASO A: El centro de la esfera está fuera del AABB pero su corteza solapa
    // -------------------------------------------------------------------------
    if (distSq > 1e-6f) {
        float dist = std::sqrt(distSq);
        float invDist = 1.0f / dist;
        // Normal apuntando desde el punto de contacto en la superficie de la caja hacia la esfera
        outNormal = Point3D{dx * invDist, dy * invDist, dz * invDist};
        outPenetration = sphereRadius - dist;
        return true;
    }

    // -------------------------------------------------------------------------
    // CASO B: El centro de la esfera penetró dentro del volumen interno del AABB
    // Se calcula la distancia mínima a cada una de las 6 caras y se expulsa
    // por la cara más próxima (Minimum Translation Vector - MTV).
    // Fuente: Christer Ericson (Cap. 7.1 "Penetration Depth Estimation")
    // -------------------------------------------------------------------------
    float dLeft   = sphereCenter.x - boxMin.x; // Distancia a cara X-min
    float dRight  = boxMax.x - sphereCenter.x; // Distancia a cara X-max
    float dFront  = sphereCenter.z - boxMin.z; // Distancia a cara Z-min
    float dBack   = boxMax.z - sphereCenter.z; // Distancia a cara Z-max

    float minDist = std::min({dLeft, dRight, dFront, dBack});
    if (minDist == dLeft) {
        outNormal = Point3D{-1.0f, 0.0f, 0.0f};
        outPenetration = dLeft + sphereRadius;
    } else if (minDist == dRight) {
        outNormal = Point3D{1.0f, 0.0f, 0.0f};
        outPenetration = dRight + sphereRadius;
    } else if (minDist == dFront) {
        outNormal = Point3D{0.0f, 0.0f, -1.0f};
        outPenetration = dFront + sphereRadius;
    } else {
        outNormal = Point3D{0.0f, 0.0f, 1.0f};
        outPenetration = dBack + sphereRadius;
    }
    return true;
}

void CollisionSystem::Update(ECSWorld& world, float /*dt*/) noexcept {
    // -------------------------------------------------------------------------
    // ETAPA 1: Reiniciar el estado de contacto por frame en todos los colisionadores
    // -------------------------------------------------------------------------
    world.ForEach<ColliderComponent>([](Entity /*ent*/, ColliderComponent& col) {
        col.isColliding = false;
        col.penetrationDepth = 0.0f;
        col.collidedEntity = Entity::Null;
    });

    // -------------------------------------------------------------------------
    // ETAPA 2: Detección y Resolución Física entre Actores Móviles y Obstáculos
    // -------------------------------------------------------------------------
    world.ForEach<TransformComponent, ColliderComponent>(
        [&world](Entity dynEnt, TransformComponent& dynTrans, ColliderComponent& dynCol) {
            // Solo procesar entidades móviles sólidas (ej. Personaje Jugador)
            if (dynCol.isStatic || !dynCol.isSolid) {
                return;
            }

            Point3D dynCenter = dynTrans.position + dynCol.localOffset;
            float dynRadius = dynCol.radius;

            world.ForEach<TransformComponent, ColliderComponent>(
                [&](Entity statEnt, const TransformComponent& statTrans, ColliderComponent& statCol) {
                    // Evitar auto-colisión y comprobar que el obstáculo sea estático y sólido
                    if (dynEnt == statEnt || !statCol.isStatic || !statCol.isSolid) {
                        return;
                    }
                    // Filtrado por máscara de capas de colisión (Bitmask Collision Filtering)
                    if ((dynCol.collisionMask & statCol.layer) == 0) {
                        return;
                    }

                    Point3D normal{0.0f, 0.0f, 0.0f};
                    float penetration = 0.0f;
                    bool hit = false;

                    // Colisión con Esfera / Cilindro Estático (ej. Pilares Clásicos)
                    if (statCol.shape == ColliderShape::Sphere) {
                        Point3D statCenter = statTrans.position + statCol.localOffset;
                        // Acotar colisión en altura cilíndrica para simular fustes verticales
                        if (dynCenter.y + dynRadius > statTrans.position.y - 0.5f &&
                            dynCenter.y - dynRadius < statTrans.position.y + 4.2f) {
                            Point3D flatA{dynCenter.x, 0.0f, dynCenter.z};
                            Point3D flatB{statCenter.x, 0.0f, statCenter.z};
                            hit = IntersectSphereSphere(flatA, dynRadius, flatB, statCol.radius, normal, penetration);
                        }
                    } 
                    // Colisión con Caja AABB Estática (ej. Casa Holandesa)
                    else if (statCol.shape == ColliderShape::AABB) {
                        Point3D boxCenter = statTrans.position + statCol.localOffset;
                        Point3D boxMin = boxCenter - statCol.halfExtents;
                        Point3D boxMax = boxCenter + statCol.halfExtents;
                        hit = IntersectSphereAABB(dynCenter, dynRadius, boxMin, boxMax, normal, penetration);
                    }

                    if (hit && penetration > 0.0f) {
                        // -------------------------------------------------------------
                        // ALGORITMO: Deslizamiento Tangencial Suave (Slide Response)
                        // Fuente: Jason Gregory (Cap. 13.5 "Kinematic Character Controllers")
                        // -------------------------------------------------------------
                        // 1. Proyectar el vector normal en el plano horizontal XZ
                        Point3D slideNormal{normal.x, 0.0f, normal.z};
                        float lenSq = slideNormal.x * slideNormal.x + slideNormal.z * slideNormal.z;
                        if (lenSq > 1e-5f) {
                            float invLen = 1.0f / std::sqrt(lenSq);
                            slideNormal.x *= invLen;
                            slideNormal.z *= invLen;
                        } else {
                            slideNormal = normal;
                        }

                        // 2. Resolver penetración geométrica inmediata (MTV Positional Depenetration):
                        //    p' = p + n_slide * penetration
                        dynTrans.position.x += slideNormal.x * penetration;
                        dynTrans.position.z += slideNormal.z * penetration;
                        dynTrans.MarkDirty();
                        dynCenter = dynTrans.position + dynCol.localOffset;

                        // 3. Proyección de Velocidad Tangencial sobre el KCC:
                        //    v' = v - (v . n) * n  (Elimina la componente hacia la pared)
                        //    Garantiza que el personaje deslice fluidamente sin adherirse a muros.
                        if (auto* kcc = world.GetComponent<KinematicCharacterComponent>(dynEnt)) {
                            float vn = kcc->velocity.x * slideNormal.x + kcc->velocity.z * slideNormal.z;
                            if (vn < 0.0f) {
                                kcc->velocity.x -= vn * slideNormal.x;
                                kcc->velocity.z -= vn * slideNormal.z;
                            }
                        }

                        // 4. Registrar telemetría de contacto para visualización en HUD y DebugDraw3D
                        dynCol.isColliding = true;
                        dynCol.contactNormal = slideNormal;
                        dynCol.penetrationDepth = penetration;
                        dynCol.collidedEntity = statEnt;

                        statCol.isColliding = true;
                        statCol.contactNormal = slideNormal * -1.0f;
                        statCol.penetrationDepth = penetration;
                        statCol.collidedEntity = dynEnt;
                    }
                }
            );
        }
    );

    // -------------------------------------------------------------------------
    // ETAPA 3: Evaluación de Volúmenes Trigger Espaciales (Autómata de Flancos)
    // Fuente: Jason Gregory (Cap. 16 "Gameplay Foundation & Trigger Volumes")
    // -------------------------------------------------------------------------
    world.ForEach<TransformComponent, TriggerComponent>(
        [&world](Entity /*trigEnt*/, const TransformComponent& trigTrans, TriggerComponent& trig) {
            // Actualizar historial de estado
            trig.wasInside = trig.isInside;
            trig.isInside = false;
            trig.enteredThisFrame = false;
            trig.exitedThisFrame = false;
            trig.triggeringEntity = Entity::Null;

            if (!trig.isActive) {
                return;
            }

            Point3D trigCenter = trigTrans.position + trig.localOffset;

            // Comprobar solapamiento con cualquier entidad perteneciente a Layer_Player
            world.ForEach<TransformComponent, ColliderComponent>(
                [&](Entity actorEnt, const TransformComponent& actorTrans, const ColliderComponent& actorCol) {
                    if (actorCol.isStatic || (actorCol.layer & Layer_Player) == 0) {
                        return;
                    }

                    Point3D actorCenter = actorTrans.position + actorCol.localOffset;
                    Point3D dummyNormal{};
                    float dummyPen = 0.0f;

                    if (IntersectSphereSphere(actorCenter, actorCol.radius, trigCenter, trig.radius, dummyNormal, dummyPen)) {
                        trig.isInside = true;
                        trig.triggeringEntity = actorEnt;
                    }
                }
            );

            // Flanco de Subida (OnTriggerEnter): No estaba dentro en t-1 y está dentro en t
            if (trig.isInside && !trig.wasInside) {
                trig.enteredThisFrame = true;
                ++trig.activationCount;

                // Comportamiento semántico para plataformas de salto (BouncePad)
                if (trig.type == TriggerType::BouncePad && trig.triggeringEntity != Entity::Null) {
                    if (auto* kcc = world.GetComponent<KinematicCharacterComponent>(trig.triggeringEntity)) {
                        kcc->velocity.y = trig.bounceImpulse;
                        kcc->isGrounded = false;
                        kcc->isJumping = true;
                    }
                }

                // Comportamiento semántico para coleccionables de un solo uso (One-Shot)
                if (trig.type == TriggerType::Collectible && trig.oneShot) {
                    trig.isCollected = true;
                    trig.isActive = false;
                }
            } 
            // Flanco de Bajada (OnTriggerExit): Estaba dentro en t-1 y ya no está dentro en t
            else if (!trig.isInside && trig.wasInside) {
                trig.exitedThisFrame = true;
            }
        }
    );
}

} // namespace WiiU
