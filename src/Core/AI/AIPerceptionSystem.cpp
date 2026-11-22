#include "Core/AI/AIPerceptionSystem.hpp"
#include "Core/AI/SteeringBehaviors.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include <algorithm>
#include <cmath>

namespace WiiU {

bool AIPerceptionSystem::EmitStimulus(const AIStimulusEvent& stimulus) noexcept {
    if (stimulus.type == StimulusType::None || m_stimulusCount >= MAX_FRAME_STIMULI) {
        return false;
    }
    m_stimuli[m_stimulusCount++] = stimulus;
    return true;
}

void AIPerceptionSystem::Update(ECSWorld& world, float dt) noexcept {
    const float safeDt = std::clamp(dt, 0.0f, 0.1f);

    world.ForEach<TransformComponent, AIPerceptionComponent>(
        [this, safeDt](Entity /*agentEnt*/, const TransformComponent& tf, AIPerceptionComponent& perc) {
            if (!perc.sensesEnabled) {
                return;
            }

            // 1. Envejecer estimulos recientes y purgar memoria expirada
            for (auto& mem : perc.recentStimuli) {
                if (!mem.active) continue;
                mem.age += safeDt;
                if (mem.age >= perc.memoryDuration) {
                    mem.active = false;
                }
            }

            // 2. Vector frontal horizontal (Yaw canonico del motor: SteeringBehaviors atan2(-vx, vz))
            const float yaw = tf.rotation.y;
            const float fwdX = -std::sin(yaw);
            const float fwdZ = std::cos(yaw);
            const float sightRangeSq = perc.sightRange * perc.sightRange;

            bool perceivedAnyThisFrame = false;
            float bestScore = -1.0f;
            const AIStimulusEvent* bestStim = nullptr;

            for (size_t i = 0; i < m_stimulusCount; ++i) {
                const AIStimulusEvent& ev = m_stimuli[i];
                if (ev.factionId == perc.factionId) {
                    continue;
                }

                const Point3D delta = ev.position - tf.position;
                const float distSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
                bool detected = false;
                float weight = ev.strength;

                switch (ev.type) {
                    case StimulusType::Sight:
                        if (distSq <= sightRangeSq) {
                            const float xzSq = delta.x * delta.x + delta.z * delta.z;
                            if (xzSq <= 1e-6f) {
                                detected = true;
                            } else {
                                const float invXZ = 1.0f / std::sqrt(xzSq);
                                const float dotXZ = (delta.x * fwdX + delta.z * fwdZ) * invXZ;
                                detected = (dotXZ >= perc.sightHalfFovCos);
                            }
                        }
                        weight *= 1.5f;
                        break;

                    case StimulusType::Hearing: {
                        const float effRange = std::min(perc.hearingRange, ev.radius);
                        if (distSq <= effRange * effRange) {
                            detected = true;
                            const float dist = std::sqrt(distSq);
                            const float atten = 1.0f - (dist / std::max(effRange, 0.001f));
                            weight *= (0.4f + 0.6f * atten);
                        }
                        break;
                    }

                    case StimulusType::Damage:
                        if (distSq <= ev.radius * ev.radius) {
                            detected = true;
                            weight *= 2.5f;
                        }
                        break;

                    default:
                        break;
                }

                if (!detected) continue;

                perceivedAnyThisFrame = true;

                // Registrar en ranura libre o sobreescribir el estimulo mas antiguo
                size_t targetSlot = 0;
                float oldestAge = -1.0f;
                for (size_t s = 0; s < AIPerceptionComponent::MAX_RECENT_STIMULI; ++s) {
                    if (!perc.recentStimuli[s].active) {
                        targetSlot = s;
                        break;
                    }
                    if (perc.recentStimuli[s].age > oldestAge) {
                        oldestAge = perc.recentStimuli[s].age;
                        targetSlot = s;
                    }
                }
                perc.recentStimuli[targetSlot] = PerceivedStimulus{
                    ev.type, ev.sourceEntity, ev.position, weight, 0.0f, true
                };

                if (weight > bestScore) {
                    bestScore = weight;
                    bestStim = &ev;
                }
            }

            // 3. Actualizar SensoryBlackboard y nivel de alerta
            SensoryBlackboard& bb = perc.blackboard;
            if (perceivedAnyThisFrame && bestStim) {
                bb.hasTarget = true;
                bb.targetEntity = bestStim->sourceEntity;
                bb.lastKnownTargetPos = bestStim->position;
                bb.timeSinceLastStimulus = 0.0f;
                bb.dominantStimulus = bestStim->type;
                const float alertGain = (bestStim->type == StimulusType::Damage) ? 1.0f
                                      : (bestStim->type == StimulusType::Sight ? (1.8f * safeDt) : (0.9f * safeDt));
                bb.alertLevel = std::clamp(bb.alertLevel + alertGain, 0.0f, 1.0f);
            } else {
                bb.timeSinceLastStimulus += safeDt;
                bb.alertLevel = std::max(0.0f, bb.alertLevel - perc.alertDecayRate * safeDt);
                if (bb.timeSinceLastStimulus >= perc.memoryDuration) {
                    bb.hasTarget = false;
                    bb.targetEntity = Entity::Null;
                    bb.dominantStimulus = StimulusType::None;
                }
            }
        }
    );

    ClearStimuli();
}

} // namespace WiiU
