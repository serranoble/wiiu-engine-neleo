#pragma once

#include "Core/AI/AIPerceptionComponent.hpp"
#include "Core/ECS/ECSWorld.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Neleo {

struct AIStimulusEvent {
    StimulusType type{StimulusType::None};
    Entity sourceEntity{Entity::Null};
    Point3D position{0.0f, 0.0f, 0.0f};
    float radius{10.0f};
    float strength{1.0f};
    uint8_t factionId{0}; // 0 = jugador/hostil respecto a agentes de faccion 1
};

/**
 * @brief [OPT-386] Sistema ECS de Percepcion Multi-Estimulo Zero-Alloc.
 *
 * Recibe estimulos emitidos durante el frame (vista, pasos/disparos, impactos) y
 * actualiza las pizarras sensoriales (SensoryBlackboard) de todos los agentes con
 * AIPerceptionComponent + TransformComponent.
 */
class AIPerceptionSystem {
public:
    static constexpr size_t MAX_FRAME_STIMULI = 64;

    void ClearStimuli() noexcept { m_stimulusCount = 0; }

    bool EmitStimulus(const AIStimulusEvent& stimulus) noexcept;

    bool EmitSightTarget(Entity source, const Point3D& pos, uint8_t factionId = 0, float strength = 1.0f) noexcept {
        return EmitStimulus(AIStimulusEvent{StimulusType::Sight, source, pos, 100.0f, strength, factionId});
    }

    bool EmitSound(Entity source, const Point3D& pos, float audibleRadius,
                   uint8_t factionId = 0, float strength = 0.8f) noexcept {
        return EmitStimulus(AIStimulusEvent{StimulusType::Hearing, source, pos, audibleRadius, strength, factionId});
    }

    bool EmitDamage(Entity attacker, const Point3D& originPos,
                    uint8_t attackerFaction = 0, float strength = 1.0f) noexcept {
        return EmitStimulus(AIStimulusEvent{StimulusType::Damage, attacker, originPos, 25.0f, strength, attackerFaction});
    }

    /**
     * @brief Evalua todos los estimulos acumulados contra los sensores de cada agente y limpia la cola.
     */
    void Update(ECSWorld& world, float dt) noexcept;

    [[nodiscard]] size_t GetQueuedStimulusCount() const noexcept { return m_stimulusCount; }

private:
    std::array<AIStimulusEvent, MAX_FRAME_STIMULI> m_stimuli{};
    size_t m_stimulusCount{0};
};

} // namespace Neleo
