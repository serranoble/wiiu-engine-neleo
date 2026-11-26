#pragma once

#include "Core/AI/BehaviorTree.hpp"
#include <array>
#include <cstdint>

namespace Neleo {

/**
 * @brief [OPT-385] Componente ECS que vincula una entidad con un BehaviorTreeAsset compartido.
 *
 * Almacena el estado de ejecucion por instancia (temporizadores de cooldown y contadores
 * de repeticion) en memoria POD plana con 0 alocaciones dinamicas.
 */
struct BehaviorTreeComponent {
    const BehaviorTreeAsset* tree{nullptr};
    void* userData{nullptr};

    std::array<float, BehaviorTreeAsset::MAX_NODES> cooldownTimers{};
    std::array<uint16_t, BehaviorTreeAsset::MAX_NODES> repeatCounters{};

    BTStatus lastRootStatus{BTStatus::Invalid};
    uint16_t lastActiveNode{BehaviorTreeAsset::INVALID_NODE};
    bool enabled{true};

    void ResetRuntimeState() noexcept {
        cooldownTimers.fill(0.0f);
        repeatCounters.fill(0u);
        lastRootStatus = BTStatus::Invalid;
        lastActiveNode = BehaviorTreeAsset::INVALID_NODE;
    }
};

} // namespace Neleo
