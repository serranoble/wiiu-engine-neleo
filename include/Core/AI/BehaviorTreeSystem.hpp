#pragma once

#include "Core/AI/BehaviorTreeComponent.hpp"
#include "Core/ECS/ECSWorld.hpp"

namespace Neleo {

/**
 * @brief [OPT-385] Evaluador ECS de Arboles de Comportamiento (BehaviorTreeRunner).
 *
 * Ejecuta todos los agentes con BehaviorTreeComponent en cada frame con 0 alocaciones en heap,
 * conectando automaticamente la SensoryBlackboard de AIPerceptionComponent si esta presente.
 */
class BehaviorTreeSystem {
public:
    static void Update(ECSWorld& world, float dt) noexcept;

    [[nodiscard]] static BTStatus TickEntity(ECSWorld& world, Entity entity,
                                             BehaviorTreeComponent& btComp,
                                             SensoryBlackboard& blackboard,
                                             float dt) noexcept;

private:
    static constexpr uint32_t MAX_RECURSION_DEPTH = 16;

    [[nodiscard]] static BTStatus EvaluateNode(ECSWorld& world, Entity entity,
                                               BehaviorTreeComponent& btComp,
                                               SensoryBlackboard& blackboard,
                                               uint16_t nodeIdx, float dt,
                                               uint32_t depth) noexcept;
};

} // namespace Neleo
