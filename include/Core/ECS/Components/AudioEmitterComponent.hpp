#pragma once

#include "Audio/Audio3DTypes.hpp"

namespace WiiU {

/**
 * @brief [OPT-242] Componente Emisor de Sonido Espacial 3D orientado a datos.
 * 
 * Vincula una fuente acústica tridimensional con el Voice Pool de 32 canales de AudioEngine3D.
 * Almacena parámetros de atenuación física, rolloff, oclusión y handle generacional activo.
 */
struct AudioEmitterComponent {
    SoundHandle voiceHandle{};
    AudioEmitter3D emitter{};
    const SoundClip* clip{nullptr};
    Point3D prevPosition{0.0f, 0.0f, 0.0f};
    bool hasPrevPosition{false};
    bool isPlaying{false};
    bool isDirty{true};

    constexpr AudioEmitterComponent() noexcept = default;

    explicit AudioEmitterComponent(const SoundClip* audioClip, float volume = 1.0f, float minDistance = 1.5f,
                                  float maxDistance = 35.0f, bool loop = false, uint8_t priority = 50,
                                  AudioBus audioBus = AudioBus::SFX) noexcept
        : clip(audioClip), hasPrevPosition(false), isPlaying(false), isDirty(true) {
        emitter.volume = volume;
        emitter.minDistance = minDistance;
        emitter.maxDistance = maxDistance;
        emitter.isLooping = loop;
        emitter.priority = priority;
        emitter.bus = audioBus;
    }

    void Play() noexcept {
        isPlaying = true;
        isDirty = true;
    }

    void Stop() noexcept {
        isPlaying = false;
        isDirty = true;
    }

    void SetVolume(float vol) noexcept {
        emitter.volume = vol;
        isDirty = true;
    }

    void SetOcclusion(float occ) noexcept {
        emitter.occlusion = occ;
        isDirty = true;
    }
};

} // namespace WiiU
