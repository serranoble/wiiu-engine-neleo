#pragma once

#include "Audio/Audio3DTypes.hpp"
#include <array>
#include <cstdint>

namespace Neleo {

/**
 * @brief Motor de Audio Espacial 3D de Alto Rendimiento para Nintendo Wii U y Framework Multiplataforma.
 * 
 * Implementa las directivas del Hito 62:
 * 1. Voice Pool estático de 32 canales sin alocaciones en runtime.
 * 2. Atenuación física por distancia (Inverse Distance Clamped Model).
 * 3. Paneo azimutal estéreo con Ley de Potencia Constante (Constant-Power Panning).
 * 4. Oclusión acústica dinámica y atenuación subacuática mediante Filtro Paso Bajo (LPF) de 1 polo.
 * 5. Algoritmo determinista de robo de voces por prioridad (Priority Voice Stealing).
 * 6. Síntesis procedural integrada de clips de audio para ambientes sin dependencias externas.
 */
class AudioEngine3D {
public:
    static constexpr size_t MAX_PHYSICAL_VOICES = 32;
    static constexpr size_t MAX_VIRTUAL_VOICES  = 128;
    static constexpr size_t MAX_VOICES          = MAX_VIRTUAL_VOICES;

    static AudioEngine3D& Instance() {
        static AudioEngine3D instance;
        return instance;
    }

    AudioEngine3D(const AudioEngine3D&) = delete;
    AudioEngine3D& operator=(const AudioEngine3D&) = delete;
    AudioEngine3D(AudioEngine3D&&) = delete;
    AudioEngine3D& operator=(AudioEngine3D&&) = delete;

    bool Init();
    void Shutdown();

    // Actualización de estado del oyente (cámara / jugador)
    void SetListener(const AudioListener3D& listener);
    [[nodiscard]] AudioListener3D GetListener() const;

    // Reproducción de sonido 3D espacializado
    SoundHandle PlaySound3D(const SoundClip& clip, const AudioEmitter3D& emitter);

    // Reproducción de sonido 2D sin espacialización (interfaz, BGM auxiliar)
    SoundHandle PlaySound2D(const SoundClip& clip, float volume = 1.0f, bool loop = false,
                            uint8_t priority = 50, AudioBus bus = AudioBus::SFX);

    // Control de buses de submezcla, Sidechain Ducking y Reverb Acústico Schroeder
    void SetBusVolume(AudioBus bus, float volume);
    [[nodiscard]] float GetBusVolume(AudioBus bus) const;
    void SetReverbWetMix(float wet);
    [[nodiscard]] float GetReverbWetMix() const;
    [[nodiscard]] float GetActiveDuckingGain() const;
    [[nodiscard]] float GetLastDopplerPitch() const;

    // Control dinámico de emisores activos
    void UpdateEmitter(SoundHandle handle, const AudioEmitter3D& emitter);
    void StopSound(SoundHandle handle);
    void StopAllSounds();
    [[nodiscard]] bool IsPlaying(SoundHandle handle) const;

    // Modificadores de entorno global
    void SetSubmerged(bool isSubmerged);
    [[nodiscard]] bool IsSubmerged() const;

    // Bucle de mezcla de bajo nivel (Invocado desde AudioCallback nativo)
    void MixAudio(int16_t* outStereoStream, int numFrames, float masterVolume);

    // Telemetría para HUD / OSD
    [[nodiscard]] AudioEngineTelemetry GetTelemetry() const;

    // Clips procedurales de sonido integrados en el motor
    [[nodiscard]] const SoundClip& GetWaterAmbienceClip() const { return m_waterAmbienceClip; }
    [[nodiscard]] const SoundClip& GetBeaconHumClip() const { return m_beaconHumClip; }
    [[nodiscard]] const SoundClip& GetFootstepClip() const { return m_footstepClip; }
    [[nodiscard]] const SoundClip& GetWindAmbienceClip() const { return m_windAmbienceClip; }

private:
    AudioEngine3D() = default;
    ~AudioEngine3D();

    void GenerateProceduralClips();
    size_t AllocateVoice(uint8_t priority, float perceivedLoudness);
    void CalculateVoiceGains(AudioVoice& voice, const AudioListener3D& listener);
    void RebalanceVirtualVoices() noexcept;

    std::array<AudioVoice, MAX_VOICES> m_voices{};
    AudioListener3D m_listener{};
    uint32_t m_stolenVoicesCounter{0};
    bool m_initialized{false};

    // Submix Buses, Sidechain Ducking y Schroeder Acoustic Reverb (Zero-Allocation)
    static constexpr size_t REVERB_DELAY_FRAMES = 2048;
    static constexpr size_t MIX_CHUNK_FRAMES = 512;
    std::array<float, static_cast<size_t>(AudioBus::Count)> m_busVolumes{1.0f, 0.85f, 1.0f, 0.90f};
    float m_duckingEnvelope{1.0f};
    float m_reverbWetMix{0.0f};
    float m_lastDopplerPitch{1.0f};
    std::array<float, REVERB_DELAY_FRAMES> m_reverbDelayL{};
    std::array<float, REVERB_DELAY_FRAMES> m_reverbDelayR{};
    size_t m_reverbCursorL{0};
    size_t m_reverbCursorR{0};
    float m_reverbDampL{0.0f};
    float m_reverbDampR{0.0f};
    std::array<float, MIX_CHUNK_FRAMES> m_reverbSendL{};
    std::array<float, MIX_CHUNK_FRAMES> m_reverbSendR{};

    // Clips de sonido procedurales sin dependencias de disco
    SoundClip m_waterAmbienceClip;
    SoundClip m_beaconHumClip;
    SoundClip m_footstepClip;
    SoundClip m_windAmbienceClip;
};

} // namespace Neleo
