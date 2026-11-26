#pragma once

#include "Core/AI/SensoryBlackboard.hpp"
#include "Core/ECS/Entity.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Neleo {

class ECSWorld;

/**
 * @brief [OPT-385] Estado de ejecucion de un nodo de Arbol de Comportamiento (Jason Gregory Cap. 19.2.3).
 */
enum class BTStatus : uint8_t {
    Invalid = 0,
    Success,
    Failure,
    Running
};

enum class BTNodeType : uint8_t {
    Sequence = 0,  // Ejecuta hijos en orden; falla al primer Failure
    Selector,      // Ejecuta hijos en orden; tiene exito al primer Success
    Parallel,      // Evalua todos los hijos; exito si >= paramInt tienen exito
    Inverter,      // Decorador: invierte Success <-> Failure
    Succeeder,     // Decorador: convierte Failure en Success (mantiene Running)
    Cooldown,      // Decorador: bloquea re-ejecucion durante paramFloat segundos tras Success
    Repeater,      // Decorador: repite hijo paramInt veces (0 = infinito)
    Condition,     // Hoja de comprobacion booleana rapida
    Action         // Hoja de accion con estado Running/Success/Failure
};

using BTLeafCallback = BTStatus (*)(ECSWorld& world, Entity entity,
                                    SensoryBlackboard& blackboard,
                                    float dt, void* userData);

struct BTNode {
    static constexpr size_t MAX_CHILDREN = 8;

    BTNodeType type{BTNodeType::Action};
    std::array<uint16_t, MAX_CHILDREN> children{};
    uint8_t childCount{0};
    float paramFloat{0.0f};
    int32_t paramInt{0};
    BTLeafCallback callback{nullptr};
};

/**
 * @brief [OPT-385] Definicion inmutable Data-Driven de un Behavior Tree en arreglo contiguo (0 heap).
 *
 * Puede compartirse como recurso Flyweight entre decenas de agentes ECS simultaneos.
 */
struct BehaviorTreeAsset {
    static constexpr size_t MAX_NODES = 64;
    static constexpr uint16_t INVALID_NODE = 0xFFFFu;

    std::array<BTNode, MAX_NODES> nodes{};
    uint16_t nodeCount{0};
    uint16_t rootIndex{INVALID_NODE};

    void Clear() noexcept {
        nodeCount = 0;
        rootIndex = INVALID_NODE;
    }

    uint16_t AddNode(BTNodeType type, BTLeafCallback cb = nullptr,
                     float paramF = 0.0f, int32_t paramI = 0) noexcept {
        if (nodeCount >= MAX_NODES) return INVALID_NODE;
        const uint16_t idx = nodeCount++;
        nodes[idx] = BTNode{};
        nodes[idx].type = type;
        nodes[idx].callback = cb;
        nodes[idx].paramFloat = paramF;
        nodes[idx].paramInt = paramI;
        if (rootIndex == INVALID_NODE) {
            rootIndex = idx;
        }
        return idx;
    }

    bool AttachChild(uint16_t parentIdx, uint16_t childIdx) noexcept {
        if (parentIdx >= nodeCount || childIdx >= nodeCount || parentIdx == childIdx) return false;
        BTNode& parent = nodes[parentIdx];
        if (parent.childCount >= BTNode::MAX_CHILDREN) return false;
        parent.children[parent.childCount++] = childIdx;
        return true;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return rootIndex != INVALID_NODE && rootIndex < nodeCount;
    }
};

} // namespace Neleo
