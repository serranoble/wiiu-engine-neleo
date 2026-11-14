#include "Core/AI/AISteeringSystem.hpp"
#include "Core/AI/AIControllerComponent.hpp"
#include "Core/AI/SteeringBehaviors.hpp"
#include "Core/AI/AStarPathfinder.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include <cmath>
#include <algorithm>

namespace WiiU {

void AISteeringSystem::Update(ECSWorld& world, 
                              const NavGraph& navGraph, 
                              const Point3D& targetPosition, 
                              float dt) noexcept {
    if (dt <= 0.0f) return;

    world.ForEach<TransformComponent, AIControllerComponent>([&](Entity /*entity*/, 
                                                                 TransformComponent& trans, 
                                                                 AIControllerComponent& ai) {
        ai.stateTimer += dt;
        if (ai.chaseCooldown > 0.0f) {
            ai.chaseCooldown -= dt;
        }

        // Si el objetivo se desplazó significativamente desde la última inspección, reanudar reactividad
        const float distFromInspectedSq = (targetPosition.x - ai.lastInspectedTargetPos.x) * (targetPosition.x - ai.lastInspectedTargetPos.x) +
                                          (targetPosition.z - ai.lastInspectedTargetPos.z) * (targetPosition.z - ai.lastInspectedTargetPos.z);
        if (distFromInspectedSq > (1.2f * 1.2f)) {
            ai.targetStationaryIgnored = false;
        }

        // 1. Evaluación Sensorial (Cono de visión y radio de proximidad)
        const float distToTargetSq = (targetPosition.x - trans.position.x) * (targetPosition.x - trans.position.x) +
                                     (targetPosition.z - trans.position.z) * (targetPosition.z - trans.position.z);
        const float distToTarget = std::sqrt(distToTargetSq);

        bool canSeeTarget = SteeringBehaviors::IsInSightCone(trans.position, 
                                                             trans.rotation.y, 
                                                             targetPosition, 
                                                             ai.perceptionRadius, 
                                                             ai.perceptionFOV);

        // Alerta por proximidad inmediata (percepción auditiva / 360° en distancias cortas <= 2.2m)
        if (distToTarget <= 2.2f) {
            canSeeTarget = true;
        }

        ai.targetVisible = canSeeTarget;

        // Transición sensorial hacia Persecución (respetando cooldown y desenganche de objetivo estático)
        if (canSeeTarget && ai.chaseCooldown <= 0.0f && !ai.targetStationaryIgnored) {
            ai.lastKnownTargetPos = targetPosition;
            ai.loseTargetTimer = 0.0f;
            if (ai.currentState == AIState::Patrol || ai.currentState == AIState::ReturnHome || ai.currentState == AIState::Idle) {
                ai.ChangeState(AIState::Chase);
            }
        }

        Point3D steeringForce{0.0f, 0.0f, 0.0f};

        // 2. Máquina de Estados Finitos (HFSM)
        switch (ai.currentState) {
            case AIState::Patrol: {
                // Si no tiene ruta o terminó el camino actual, planificar el siguiente tramo
                if (!ai.path.isValid || ai.path.IsFinished()) {
                    uint32_t fromNode = navGraph.FindNearestNode(trans.position);
                    uint32_t nextTargetNode = ai.isPatrolForward ? ai.patrolTargetNode : ai.patrolStartNode;

                    if (fromNode == nextTargetNode) {
                        ai.isPatrolForward = !ai.isPatrolForward;
                        nextTargetNode = ai.isPatrolForward ? ai.patrolTargetNode : ai.patrolStartNode;
                    }

                    AStarPathfinder::FindPath(navGraph, fromNode, nextTargetNode, ai.path);
                }

                if (ai.path.isValid && !ai.path.IsFinished()) {
                    Point3D wp = ai.path.GetCurrentWaypoint();
                    steeringForce = SteeringBehaviors::Arrive(trans.position, wp, ai.velocity, ai.moveSpeed, ai.slowingRadius);

                    const float dx = wp.x - trans.position.x;
                    const float dz = wp.z - trans.position.z;
                    if ((dx * dx + dz * dz) <= (ai.waypointArrivalRadius * ai.waypointArrivalRadius)) {
                        ai.path.Advance();
                    }
                }
                break;
            }

            case AIState::Chase: {
                // Desaceleración suave (Arrive) hacia el objetivo para no colisionar ni oscilar violentamente
                steeringForce = SteeringBehaviors::Arrive(trans.position, targetPosition, ai.velocity, ai.chaseSpeed, 2.5f);

                if (canSeeTarget) {
                    ai.lastKnownTargetPos = targetPosition;
                    ai.loseTargetTimer = 0.0f;

                    // Si el agente alcanza al jugador (distancia de contacto <= 1.8m), pasa a inspeccionarlo
                    if (distToTarget <= 1.8f) {
                        ai.ChangeState(AIState::Investigate);
                    }
                } else {
                    ai.loseTargetTimer += dt;
                    if (ai.loseTargetTimer >= ai.loseTargetTimeout) {
                        // Pierde de vista al jugador: pasa a investigar la última posición vista
                        ai.ChangeState(AIState::Investigate);
                    }
                }
                break;
            }

            case AIState::Investigate: {
                // Frenar suavemente en la posición del objetivo mientras lo inspecciona
                steeringForce = SteeringBehaviors::Arrive(trans.position, ai.lastKnownTargetPos, ai.velocity, ai.moveSpeed * 0.35f, 1.8f);

                // Tras 2.5s de inspección pacífica o detenido, desenganchar y regresar a patrullar
                if (ai.stateTimer >= 2.5f) {
                    ai.lastInspectedTargetPos = targetPosition;
                    ai.targetStationaryIgnored = true;
                    ai.chaseCooldown = 5.0f; // Tiempo de enfriamiento para retornar a patrulla sin re-enganchar si el jugador no se mueve
                    ai.ChangeState(AIState::ReturnHome);

                    // Trazar ruta A* hacia el nodo de patrulla más cercano
                    uint32_t nearestNode = navGraph.FindNearestNode(trans.position);
                    const NavNode* nd = navGraph.GetNode(nearestNode);
                    Point3D returnDest = nd ? nd->position : ai.homePosition;
                    AStarPathfinder::FindPath(navGraph, trans.position, returnDest, ai.path);
                }
                break;
            }

            case AIState::ReturnHome: {
                // Si la ruta no es válida, calcular trayectoria A* al nodo más cercano
                if (!ai.path.isValid) {
                    uint32_t nearestNode = navGraph.FindNearestNode(trans.position);
                    const NavNode* nd = navGraph.GetNode(nearestNode);
                    Point3D returnDest = nd ? nd->position : ai.homePosition;
                    AStarPathfinder::FindPath(navGraph, trans.position, returnDest, ai.path);
                }

                if (ai.path.isValid && !ai.path.IsFinished()) {
                    Point3D wp = ai.path.GetCurrentWaypoint();
                    steeringForce = SteeringBehaviors::Arrive(trans.position, wp, ai.velocity, ai.moveSpeed, ai.slowingRadius);

                    const float dx = wp.x - trans.position.x;
                    const float dz = wp.z - trans.position.z;
                    if ((dx * dx + dz * dz) <= (ai.waypointArrivalRadius * ai.waypointArrivalRadius)) {
                        ai.path.Advance();
                    }
                }

                // Al completar el camino de regreso al grafo (o si ya estaba en el nodo), reanudar la patrulla
                if (ai.path.IsFinished() || !ai.path.isValid) {
                    ai.ChangeState(AIState::Patrol);
                    ai.path.Clear(); // Forzar cálculo de nuevo tramo de patrulla en el siguiente ciclo
                }
                break;
            }

            case AIState::Idle:
            default:
                // Decelerar hasta detenerse
                steeringForce = Point3D{ -ai.velocity.x * 4.0f, 0.0f, -ai.velocity.z * 4.0f };
                break;
        }

        // 3. Integración Cinemática (Euler explícito)
        ai.velocity.x += steeringForce.x * (ai.maxAcceleration * dt);
        ai.velocity.z += steeringForce.z * (ai.maxAcceleration * dt);

        const float speedSq = ai.velocity.x * ai.velocity.x + ai.velocity.z * ai.velocity.z;
        const float maxAllowedSpeed = (ai.currentState == AIState::Chase) ? ai.chaseSpeed : ai.moveSpeed;

        if (speedSq > maxAllowedSpeed * maxAllowedSpeed && speedSq > 0.0001f) {
            const float currentSpeed = std::sqrt(speedSq);
            ai.velocity.x = (ai.velocity.x / currentSpeed) * maxAllowedSpeed;
            ai.velocity.z = (ai.velocity.z / currentSpeed) * maxAllowedSpeed;
        }

        trans.position.x += ai.velocity.x * dt;
        trans.position.z += ai.velocity.z * dt;

        // 4. Orientación Angular Suave (Shortest-Path Slerp sobre Yaw)
        if (speedSq > 0.04f) {
            const float desiredYaw = SteeringBehaviors::VelocityToYaw(ai.velocity, trans.rotation.y);
            float diff = desiredYaw - trans.rotation.y;
            constexpr float PI = 3.14159265358979323846f;
            constexpr float TWO_PI = 2.0f * PI;

            while (diff > PI)  diff -= TWO_PI;
            while (diff < -PI) diff += TWO_PI;

            trans.rotation.y += diff * std::min(1.0f, 10.0f * dt);
        }

        trans.MarkDirty();
    });
}

} // namespace WiiU
