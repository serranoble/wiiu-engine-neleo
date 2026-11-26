#include "Core/AI/BehaviorTreeSystem.hpp"
#include "Core/AI/AIPerceptionComponent.hpp"
#include <algorithm>

namespace Neleo {

void BehaviorTreeSystem::Update(ECSWorld& world, float dt) noexcept {
    const float safeDt = std::clamp(dt, 0.0f, 0.1f);
    SensoryBlackboard fallbackBlackboard{};

    world.ForEach<BehaviorTreeComponent>(
        [&world, safeDt, &fallbackBlackboard](Entity ent, BehaviorTreeComponent& btComp) {
            if (!btComp.enabled || !btComp.tree || !btComp.tree->IsValid()) {
                return;
            }

            // Decrementar temporizadores de cooldown activos
            const uint16_t count = btComp.tree->nodeCount;
            for (uint16_t i = 0; i < count; ++i) {
                if (btComp.cooldownTimers[i] > 0.0f) {
                    btComp.cooldownTimers[i] = std::max(0.0f, btComp.cooldownTimers[i] - safeDt);
                }
            }

            SensoryBlackboard* bb = &fallbackBlackboard;
            if (auto* perc = world.GetComponent<AIPerceptionComponent>(ent)) {
                bb = &perc->blackboard;
            } else {
                fallbackBlackboard.Clear();
            }

            btComp.lastRootStatus = TickEntity(world, ent, btComp, *bb, safeDt);
        }
    );
}

BTStatus BehaviorTreeSystem::TickEntity(ECSWorld& world, Entity entity,
                                        BehaviorTreeComponent& btComp,
                                        SensoryBlackboard& blackboard,
                                        float dt) noexcept {
    if (!btComp.tree || !btComp.tree->IsValid()) {
        return BTStatus::Failure;
    }
    return EvaluateNode(world, entity, btComp, blackboard, btComp.tree->rootIndex, dt, 0u);
}

BTStatus BehaviorTreeSystem::EvaluateNode(ECSWorld& world, Entity entity,
                                          BehaviorTreeComponent& btComp,
                                          SensoryBlackboard& blackboard,
                                          uint16_t nodeIdx, float dt,
                                          uint32_t depth) noexcept {
    if (depth >= MAX_RECURSION_DEPTH || nodeIdx >= btComp.tree->nodeCount) {
        return BTStatus::Failure;
    }

    const BTNode& node = btComp.tree->nodes[nodeIdx];
    btComp.lastActiveNode = nodeIdx;

    switch (node.type) {
        case BTNodeType::Sequence: {
            for (uint8_t i = 0; i < node.childCount; ++i) {
                const BTStatus st = EvaluateNode(world, entity, btComp, blackboard,
                                                 node.children[i], dt, depth + 1u);
                if (st != BTStatus::Success) {
                    return st;
                }
            }
            return BTStatus::Success;
        }

        case BTNodeType::Selector: {
            for (uint8_t i = 0; i < node.childCount; ++i) {
                const BTStatus st = EvaluateNode(world, entity, btComp, blackboard,
                                                 node.children[i], dt, depth + 1u);
                if (st != BTStatus::Failure) {
                    return st;
                }
            }
            return BTStatus::Failure;
        }

        case BTNodeType::Parallel: {
            if (node.childCount == 0) return BTStatus::Success;
            int32_t successCount = 0;
            int32_t failureCount = 0;
            const int32_t childCountI = static_cast<int32_t>(node.childCount);
            const int32_t requiredSuccess = (node.paramInt > 0)
                                            ? std::clamp(node.paramInt, 1, childCountI)
                                            : childCountI;
            for (uint8_t i = 0; i < node.childCount; ++i) {
                const BTStatus st = EvaluateNode(world, entity, btComp, blackboard,
                                                 node.children[i], dt, depth + 1u);
                if (st == BTStatus::Success) ++successCount;
                else if (st == BTStatus::Failure) ++failureCount;
            }
            if (successCount >= requiredSuccess) return BTStatus::Success;
            if (failureCount > static_cast<int32_t>(node.childCount) - requiredSuccess) {
                return BTStatus::Failure;
            }
            return BTStatus::Running;
        }

        case BTNodeType::Inverter: {
            if (node.childCount == 0) return BTStatus::Failure;
            const BTStatus st = EvaluateNode(world, entity, btComp, blackboard,
                                             node.children[0], dt, depth + 1u);
            if (st == BTStatus::Success) return BTStatus::Failure;
            if (st == BTStatus::Failure) return BTStatus::Success;
            return st;
        }

        case BTNodeType::Succeeder: {
            if (node.childCount == 0) return BTStatus::Success;
            const BTStatus st = EvaluateNode(world, entity, btComp, blackboard,
                                             node.children[0], dt, depth + 1u);
            return (st == BTStatus::Running) ? BTStatus::Running : BTStatus::Success;
        }

        case BTNodeType::Cooldown: {
            if (node.childCount == 0 || btComp.cooldownTimers[nodeIdx] > 0.0f) {
                return BTStatus::Failure;
            }
            const BTStatus st = EvaluateNode(world, entity, btComp, blackboard,
                                             node.children[0], dt, depth + 1u);
            if (st == BTStatus::Success) {
                btComp.cooldownTimers[nodeIdx] = std::max(0.0f, node.paramFloat);
            }
            return st;
        }

        case BTNodeType::Repeater: {
            if (node.childCount == 0) return BTStatus::Failure;
            const BTStatus st = EvaluateNode(world, entity, btComp, blackboard,
                                             node.children[0], dt, depth + 1u);
            if (st == BTStatus::Running) return BTStatus::Running;
            if (st == BTStatus::Failure) {
                btComp.repeatCounters[nodeIdx] = 0;
                return BTStatus::Failure;
            }
            if (node.paramInt <= 0) {
                return BTStatus::Running; // Repeticion infinita
            }
            btComp.repeatCounters[nodeIdx]++;
            if (static_cast<int32_t>(btComp.repeatCounters[nodeIdx]) >= node.paramInt) {
                btComp.repeatCounters[nodeIdx] = 0;
                return BTStatus::Success;
            }
            return BTStatus::Running;
        }

        case BTNodeType::Condition:
        case BTNodeType::Action: {
            if (!node.callback) return BTStatus::Failure;
            return node.callback(world, entity, blackboard, dt, btComp.userData);
        }
    }

    return BTStatus::Failure;
}

} // namespace Neleo
