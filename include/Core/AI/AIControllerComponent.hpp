#pragma once

#include "Graphics/Math3D.hpp"
#include "Core/AI/NavGraph.hpp"
#include "Core/AI/AStarPathfinder.hpp"
#include <cstdint>
#include <string_view>

namespace Neleo {

/**
 * @brief Estados discretos de la Máquina de Estados Finitos (FSM) de IA.
 * Referencia: Jason Gregory (Game Engine Architecture 3rd Ed., Cap. 19.2).
 */
enum class AIState : uint8_t {
    Idle = 0,
    Patrol,
    Investigate,
    Chase,
    ReturnHome
};

[[nodiscard]] constexpr std::string_view GetAIStateName(AIState state) noexcept {
    switch (state) {
        case AIState::Idle:        return "IDLE";
        case AIState::Patrol:      return "PATROL";
        case AIState::Investigate: return "INVESTIGATE";
        case AIState::Chase:       return "CHASE";
        case AIState::ReturnHome:  return "RETURN_HOME";
        default:                   return "UNKNOWN";
    }
}

/**
 * @brief Componente ECS que almacena el estado cognitivo, ruta activa y parámetros de navegación de un agente IA.
 * 
 * Sigue estrictamente el paradigma Data-Oriented Design (DOD):
 * - Estructura POD contigua en memoria, 0 punteros a vtables ni alocaciones dinámicas.
 * - Integrable en los pools densos de `ECSWorld` con búsqueda $O(1)$.
 */
struct AIControllerComponent {
    AIState currentState{AIState::Patrol};
    AIState previousState{AIState::Idle};
    float stateTimer{0.0f};

    NavPath path{};
    Point3D velocity{0.0f, 0.0f, 0.0f};
    Point3D targetPosition{0.0f, 0.0f, 0.0f};
    Point3D homePosition{0.0f, 0.0f, 0.0f};
    Point3D lastKnownTargetPos{0.0f, 0.0f, 0.0f};

    float moveSpeed{3.2f};
    float chaseSpeed{4.8f};
    float maxAcceleration{15.0f};
    float waypointArrivalRadius{0.9f};
    float slowingRadius{1.5f};

    // Parámetros sensoriales (Cono de visión y proximidad)
    float perceptionRadius{8.5f};
    float perceptionFOV{110.0f}; // Grados de apertura del campo de visión
    float loseTargetTimeout{2.0f};
    float loseTargetTimer{0.0f};
    float chaseCooldown{0.0f}; // Tiempo de enfriamiento para desenganche y retorno a patrulla
    Point3D lastInspectedTargetPos{9999.0f, 9999.0f, 9999.0f}; // Última posición donde el objetivo fue inspeccionado
    bool targetStationaryIgnored{false}; // Bandera para evitar reenganchar en Chase si el jugador permanece detenido

    // Control de patrulla
    uint32_t patrolStartNode{0};
    uint32_t patrolTargetNode{0};
    bool isPatrolForward{true};

    bool targetVisible{false};
    bool showDebugVision{true};

    void ChangeState(AIState newState) noexcept {
        if (currentState != newState) {
            previousState = currentState;
            currentState = newState;
            stateTimer = 0.0f;
        }
    }
};

} // namespace Neleo
