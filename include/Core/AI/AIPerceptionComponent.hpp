#pragma once

#include "Core/AI/SensoryBlackboard.hpp"
#include "Core/ECS/Entity.hpp"
#include "Graphics/Math3D.hpp"
#include <array>
#include <cstdint>

namespace WiiU {

/**
 * @brief [OPT-386] Estimulo percibido individual en el espacio 3D.
 */
struct PerceivedStimulus {
    StimulusType type{StimulusType::None};
    Entity sourceEntity{Entity::Null};
    Point3D position{0.0f, 0.0f, 0.0f};
    float strength{0.0f};
    float age{0.0f};
    bool active{false};
};

/**
 * @brief [OPT-386] Componente ECS de Percepcion Multi-Estimulo y Pizarra Sensorial.
 *
 * Permite que los agentes perciban vista (cono direccional), oido (radio acustico)
 * y dano con decaimiento temporal de memoria sin acoplarse a la entidad del jugador.
 */
struct AIPerceptionComponent {
    static constexpr size_t MAX_RECENT_STIMULI = 8;

    uint8_t factionId{1};          // Faccion del agente (ignora estimulos aliados de igual faccion)
    float sightRange{12.0f};       // Distancia maxima de vision (m)
    float sightHalfFovCos{0.573f}; // cos(55 deg) por defecto (~110 deg FOV total)
    float hearingRange{14.0f};     // Sensibilidad auditiva maxima (m)
    float memoryDuration{5.0f};    // Segundos que recuerda un estimulo tras perder contacto
    float alertDecayRate{0.25f};   // Tasa de descenso del nivel de alerta por segundo
    bool sensesEnabled{true};

    SensoryBlackboard blackboard{};
    std::array<PerceivedStimulus, MAX_RECENT_STIMULI> recentStimuli{};
};

} // namespace WiiU
