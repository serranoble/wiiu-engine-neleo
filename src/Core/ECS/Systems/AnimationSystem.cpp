#include "Core/ECS/Systems/AnimationSystem.hpp"
#include "Core/ECS/Systems/IKSystem.hpp"
#include "Core/ECS/Components/AnimatorComponent.hpp"
#include "Core/ECS/Components/SkeletalMeshComponent.hpp"
#include <algorithm>

namespace Neleo {

void AnimationSystem::Update(ECSWorld& world, float dt) {
    if (dt < 0.0f) return;

    world.ForEach<AnimatorComponent, SkeletalMeshComponent>(
        [dt](Entity /*e*/, AnimatorComponent& anim, SkeletalMeshComponent& skelMesh) {
            if (!skelMesh.IsValid() || skelMesh.jointCount == 0) return;

            const uint32_t jointCount = std::min(skelMesh.jointCount, MAX_SKELETON_JOINTS);

            // 1. Muestrear pose base SQT (incluso en pausa para evitar deriva acumulativa al aplicar IK)
            if (anim.currentClip) {
                if (anim.isPlaying && dt > 0.0f) {
                    anim.playbackTime += dt * anim.playbackSpeed;
                }
                anim.currentClip->SampleSQTPose(anim.playbackTime, skelMesh.localSQT, jointCount);

                // 2. Cross-Fade suave en espacio SQT (LERP traslación/escala + SLERP cuaternión)
                if (anim.IsCrossFading()) {
                    if (anim.isPlaying && dt > 0.0f) {
                        anim.nextPlaybackTime += dt * anim.playbackSpeed;
                        anim.crossFadeElapsed += dt;
                    }

                    JointTransform nextSQT[MAX_SKELETON_JOINTS]{};
                    anim.nextClip->SampleSQTPose(anim.nextPlaybackTime, nextSQT, jointCount);
                    const float alpha = anim.GetCrossFadeAlpha();

                    for (uint32_t i = 0; i < jointCount; ++i) {
                        skelMesh.localSQT[i] = JointTransform::Blend(skelMesh.localSQT[i], nextSQT[i], alpha);
                    }

                    if (anim.crossFadeElapsed >= anim.crossFadeDuration) {
                        anim.currentClip = anim.nextClip;
                        anim.playbackTime = anim.nextPlaybackTime;
                        anim.isLooping = anim.nextIsLooping;
                        anim.nextClip = nullptr;
                        anim.nextPlaybackTime = 0.0f;
                        anim.crossFadeDuration = 0.0f;
                        anim.crossFadeElapsed = 0.0f;
                    }
                }

                // 3. Capa parcial enmascarada (Partial-Body Layer Blending, ej. caminar + saludar)
                if (anim.overlayClip && anim.overlayWeight > 0.001f && anim.overlayJointMask != 0u) {
                    if (anim.isPlaying && dt > 0.0f) {
                        anim.overlayPlaybackTime += dt * anim.playbackSpeed;
                    }

                    JointTransform overlaySQT[MAX_SKELETON_JOINTS]{};
                    anim.overlayClip->SampleSQTPose(anim.overlayPlaybackTime, overlaySQT, jointCount);

                    static_assert(MAX_SKELETON_JOINTS <= 32, "overlayJointMask es bitmask de 32 bits");
                    for (uint32_t i = 0; i < jointCount && i < 32u; ++i) {
                        if ((anim.overlayJointMask & (1u << i)) != 0u) {
                            skelMesh.localSQT[i] = JointTransform::Blend(
                                skelMesh.localSQT[i], overlaySQT[i], anim.overlayWeight
                            );
                        }
                    }
                }

                // 4. Convertir pose SQT mezclada a matrices locales 4x4
                for (uint32_t i = 0; i < jointCount; ++i) {
                    skelMesh.localPoses[i] = skelMesh.localSQT[i].ToMatrix();
                }
            }

            // 5. Resolver pose global y paleta de skinning mediante forward loop O(N) lineal
            skelMesh.RecomputeWorldAndSkinningMatrices();
        });

    // 6. Post-proceso de Cinemática Inversa (Two-Bone IK, Look-At IK, FABRIK)
    IKSystem::Update(world, dt);
}

} // namespace Neleo
