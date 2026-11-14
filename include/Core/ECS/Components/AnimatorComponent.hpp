#pragma once

#include "Graphics/Animation/AnimationClip.hpp"
#include <algorithm>
#include <cstdint>

namespace WiiU {

/**
 * @brief [OPT-335] Componente ECS de Controlador y Reproductor de Animación.
 * 
 * Gestiona el clip activo, cross-fade blending SQT (Cap. 12.4) y capas parciales
 * mediante máscara de huesos de 32 bits sin alocaciones dinámicas en tiempo real.
 */
struct AnimatorComponent {
    const AnimationClip* currentClip{nullptr};
    float playbackTime{0.0f};
    float playbackSpeed{1.0f};
    bool isPlaying{true};
    bool isLooping{true};

    // Estado de Cross-Fade suave (Cap. 12.4)
    const AnimationClip* nextClip{nullptr};
    float nextPlaybackTime{0.0f};
    float crossFadeDuration{0.0f};
    float crossFadeElapsed{0.0f};
    bool nextIsLooping{true};

    // Capa parcial aditiva/sustitutiva por máscara de huesos (ej. tren superior Walk + Wave)
    const AnimationClip* overlayClip{nullptr};
    float overlayPlaybackTime{0.0f};
    float overlayWeight{0.0f};
    uint32_t overlayJointMask{0u};
    bool overlayLooping{true};

    constexpr AnimatorComponent() noexcept = default;

    explicit AnimatorComponent(const AnimationClip* clip, bool play = true, float speed = 1.0f) noexcept
        : currentClip(clip), playbackTime(0.0f), playbackSpeed(speed),
          isPlaying(play), isLooping(clip ? clip->isLooping : true) {}

    void Play(const AnimationClip* clip, bool loop = true) noexcept {
        currentClip = clip;
        playbackTime = 0.0f;
        isPlaying = true;
        isLooping = loop;
        nextClip = nullptr;
        nextPlaybackTime = 0.0f;
        crossFadeDuration = 0.0f;
        crossFadeElapsed = 0.0f;
    }

    /**
     * @brief Inicia una transición suave (Cross-Fade en espacio SQT) hacia `clip`.
     */
    void CrossFadeTo(const AnimationClip* clip, float duration = 0.28f, bool loop = true) noexcept {
        if (!clip) return;
        if (!currentClip || duration <= 0.001f) {
            Play(clip, loop);
            return;
        }
        if (clip == currentClip && nextClip == nullptr) {
            isLooping = loop;
            isPlaying = true;
            return;
        }
        if (clip == nextClip) {
            return;
        }
        nextClip = clip;
        nextPlaybackTime = 0.0f;
        crossFadeDuration = duration;
        crossFadeElapsed = 0.0f;
        nextIsLooping = loop;
        isPlaying = true;
    }

    [[nodiscard]] bool IsCrossFading() const noexcept {
        return (nextClip != nullptr) && (crossFadeDuration > 0.0f);
    }

    [[nodiscard]] float GetCrossFadeAlpha() const noexcept {
        if (!IsCrossFading()) return 0.0f;
        const float t = std::clamp(crossFadeElapsed / crossFadeDuration, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    void SetOverlayLayer(const AnimationClip* clip, uint32_t jointMask, float weight = 1.0f, bool loop = true) noexcept {
        overlayClip = clip;
        overlayJointMask = jointMask;
        overlayWeight = std::clamp(weight, 0.0f, 1.0f);
        overlayLooping = loop;
        overlayPlaybackTime = 0.0f;
    }

    void ClearOverlayLayer() noexcept {
        overlayClip = nullptr;
        overlayJointMask = 0u;
        overlayWeight = 0.0f;
        overlayPlaybackTime = 0.0f;
    }

    void Pause() noexcept {
        isPlaying = false;
    }

    void Resume() noexcept {
        isPlaying = true;
    }
};

} // namespace WiiU
