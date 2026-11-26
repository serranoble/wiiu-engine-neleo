#include "Audio/AudioEngine3D.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Core/Math/PRNG.hpp"
#include <cmath>
#include <algorithm>

namespace Neleo {

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;
constexpr float HALF_PI = 0.5f * PI;
constexpr float SAMPLE_RATE = 48000.0f;
constexpr float INV_SAMPLE_RATE = 1.0f / SAMPLE_RATE;

} // namespace

AudioEngine3D::~AudioEngine3D() {
    Shutdown();
}

bool AudioEngine3D::Init() {
    if (m_initialized) return true;

    const auto& cfg = EngineConfig::Instance();
    const float sfxVol = cfg.audio.sfxEnabled ? cfg.audio.sfxVolume : 0.0f;

    AudioManager::Instance().LockAudioDevice();
    for (size_t i = 0; i < MAX_VOICES; ++i) {
        m_voices[i] = AudioVoice{};
        m_voices[i].generation = 1;
        m_voices[i].active = false;
    }
    m_listener = AudioListener3D{};
    m_stolenVoicesCounter = 0;
    m_busVolumes = {1.0f, 0.85f, sfxVol, 0.90f};
    m_duckingEnvelope = 1.0f;
    m_reverbWetMix = 0.0f;
    m_lastDopplerPitch = 1.0f;
    m_reverbDelayL.fill(0.0f);
    m_reverbDelayR.fill(0.0f);
    m_reverbCursorL = 0;
    m_reverbCursorR = 0;
    m_reverbDampL = 0.0f;
    m_reverbDampR = 0.0f;
    AudioManager::Instance().UnlockAudioDevice();

    GenerateProceduralClips();

    m_initialized = true;
    return true;
}

void AudioEngine3D::Shutdown() {
    StopAllSounds();
    m_initialized = false;
}

void AudioEngine3D::GenerateProceduralClips() {
    PRNG prng(0xCAFE105EEDULL);

    // 1. Clip de Ambiente de Agua / Río Fluyente (~2.5 s en bucle continuo)
    {
        constexpr size_t waterFrames = static_cast<size_t>(SAMPLE_RATE * 2.5f);
        m_waterAmbienceClip.name = "Water_River_Ambience";
        m_waterAmbienceClip.channels = 1;
        m_waterAmbienceClip.sampleRate = static_cast<int>(SAMPLE_RATE);
        m_waterAmbienceClip.samples.resize(waterFrames);

        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f, b4 = 0.0f, b5 = 0.0f, b6 = 0.0f;
        for (size_t i = 0; i < waterFrames; ++i) {
            float white = prng.NextFloatSigned();
            b0 = 0.99886f * b0 + white * 0.0555179f;
            b1 = 0.99332f * b1 + white * 0.0750759f;
            b2 = 0.96900f * b2 + white * 0.1538520f;
            b3 = 0.86650f * b3 + white * 0.3104856f;
            b4 = 0.55000f * b4 + white * 0.5329522f;
            b5 = -0.7616f * b5 - white * 0.0168980f;
            float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
            b6 = white * 0.115926f;

            float t = static_cast<float>(i) * INV_SAMPLE_RATE;
            float swell = 0.65f + 0.25f * std::sin(TWO_PI * 0.4f * t) + 0.10f * std::sin(TWO_PI * 1.1f * t);
            float sample = pink * 0.14f * swell;
            m_waterAmbienceClip.samples[i] = static_cast<int16_t>(std::clamp(sample * 32767.0f, -32000.0f, 32000.0f));
        }
    }

    // 2. Clip de Zumbido Armónico de Faro / Orbe Solar (~1.5 s en bucle continuo)
    {
        constexpr size_t beaconFrames = static_cast<size_t>(SAMPLE_RATE * 1.5f);
        m_beaconHumClip.name = "Beacon_Harmonic_Hum";
        m_beaconHumClip.channels = 1;
        m_beaconHumClip.sampleRate = static_cast<int>(SAMPLE_RATE);
        m_beaconHumClip.samples.resize(beaconFrames);

        for (size_t i = 0; i < beaconFrames; ++i) {
            float t = static_cast<float>(i) * INV_SAMPLE_RATE;
            float tremolo = 0.85f + 0.15f * std::sin(TWO_PI * 3.5f * t);
            float f0 = std::sin(TWO_PI * 110.0f * t);
            float f1 = 0.48f * std::sin(TWO_PI * 220.0f * t);
            float f2 = 0.22f * std::sin(TWO_PI * 330.0f * t);
            float sample = (f0 + f1 + f2) * 0.35f * tremolo;
            m_beaconHumClip.samples[i] = static_cast<int16_t>(std::clamp(sample * 32767.0f, -32000.0f, 32000.0f));
        }
    }

    // 3. Clip de Paso Acústico / Impacto Seco (~0.18 s one-shot)
    {
        constexpr size_t stepFrames = static_cast<size_t>(SAMPLE_RATE * 0.18f);
        m_footstepClip.name = "Footstep_Impact";
        m_footstepClip.channels = 1;
        m_footstepClip.sampleRate = static_cast<int>(SAMPLE_RATE);
        m_footstepClip.samples.resize(stepFrames);

        for (size_t i = 0; i < stepFrames; ++i) {
            float t = static_cast<float>(i) * INV_SAMPLE_RATE;
            float envelope = std::exp(-28.0f * t);
            float pitch = 140.0f * std::exp(-35.0f * t) + 45.0f;
            float osc = std::sin(TWO_PI * pitch * t);
            float noise = prng.NextFloatSigned() * 0.22f;
            float sample = (osc + noise) * envelope * 0.75f;
            m_footstepClip.samples[i] = static_cast<int16_t>(std::clamp(sample * 32767.0f, -32000.0f, 32000.0f));
        }
    }

    // 4. Clip de Brisa de Viento Suave (~3.0 s en bucle continuo)
    {
        constexpr size_t windFrames = static_cast<size_t>(SAMPLE_RATE * 3.0f);
        m_windAmbienceClip.name = "Wind_Gentle_Breeze";
        m_windAmbienceClip.channels = 1;
        m_windAmbienceClip.sampleRate = static_cast<int>(SAMPLE_RATE);
        m_windAmbienceClip.samples.resize(windFrames);

        float lpf = 0.0f;
        for (size_t i = 0; i < windFrames; ++i) {
            float t = static_cast<float>(i) * INV_SAMPLE_RATE;
            float gust = 0.50f + 0.35f * std::sin(TWO_PI * 0.18f * t) + 0.15f * std::sin(TWO_PI * 0.55f * t);
            float white = prng.NextFloatSigned();
            lpf += 0.04f * (white - lpf);
            float sample = lpf * gust * 0.40f;
            m_windAmbienceClip.samples[i] = static_cast<int16_t>(std::clamp(sample * 32767.0f, -32000.0f, 32000.0f));
        }
    }
}

void AudioEngine3D::SetListener(const AudioListener3D& listener) {
    AudioManager::Instance().LockAudioDevice();
    m_listener = listener;

    // Normalizar vectores de orientación defensivamente
    if (m_listener.forward.Length() > 0.001f) {
        m_listener.forward = m_listener.forward.Normalized();
    } else {
        m_listener.forward = Point3D{0.0f, 0.0f, -1.0f};
    }

    if (m_listener.up.Length() > 0.001f) {
        m_listener.up = m_listener.up.Normalized();
    } else {
        m_listener.up = Point3D{0.0f, 1.0f, 0.0f};
    }

    // Re-evaluar ganancias, Doppler 3D y filtros en todas las voces 3D activas
    float maxDeviation = 0.0f;
    float trackedDoppler = 1.0f;
    for (size_t i = 0; i < MAX_VOICES; ++i) {
        if (m_voices[i].active && m_voices[i].is3D) {
            CalculateVoiceGains(m_voices[i], m_listener);
            if (m_voices[i].distanceToListener < m_voices[i].emitter.maxDistance) {
                const float dev = std::abs(m_voices[i].dopplerPitch - 1.0f);
                if (dev > maxDeviation) {
                    maxDeviation = dev;
                    trackedDoppler = m_voices[i].dopplerPitch;
                }
            }
        }
    }
    m_lastDopplerPitch = trackedDoppler;
    RebalanceVirtualVoices();
    AudioManager::Instance().UnlockAudioDevice();
}

void AudioEngine3D::SetBusVolume(AudioBus bus, float volume) {
    const size_t idx = static_cast<size_t>(bus);
    if (idx >= static_cast<size_t>(AudioBus::Count)) return;
    AudioManager::Instance().LockAudioDevice();
    m_busVolumes[idx] = std::clamp(volume, 0.0f, 1.5f);
    AudioManager::Instance().UnlockAudioDevice();
}

float AudioEngine3D::GetBusVolume(AudioBus bus) const {
    const size_t idx = static_cast<size_t>(bus);
    if (idx >= static_cast<size_t>(AudioBus::Count)) return 1.0f;
    AudioManager::Instance().LockAudioDevice();
    const float vol = m_busVolumes[idx];
    AudioManager::Instance().UnlockAudioDevice();
    return vol;
}

void AudioEngine3D::SetReverbWetMix(float wet) {
    AudioManager::Instance().LockAudioDevice();
    m_reverbWetMix = std::clamp(wet, 0.0f, 0.85f);
    AudioManager::Instance().UnlockAudioDevice();
}

float AudioEngine3D::GetReverbWetMix() const {
    AudioManager::Instance().LockAudioDevice();
    const float wet = m_reverbWetMix;
    AudioManager::Instance().UnlockAudioDevice();
    return wet;
}

float AudioEngine3D::GetActiveDuckingGain() const {
    AudioManager::Instance().LockAudioDevice();
    const float duck = m_duckingEnvelope;
    AudioManager::Instance().UnlockAudioDevice();
    return duck;
}

float AudioEngine3D::GetLastDopplerPitch() const {
    AudioManager::Instance().LockAudioDevice();
    const float doppler = m_lastDopplerPitch;
    AudioManager::Instance().UnlockAudioDevice();
    return doppler;
}

AudioListener3D AudioEngine3D::GetListener() const {
    AudioManager::Instance().LockAudioDevice();
    AudioListener3D copy = m_listener;
    AudioManager::Instance().UnlockAudioDevice();
    return copy;
}

void AudioEngine3D::SetSubmerged(bool isSubmerged) {
    AudioManager::Instance().LockAudioDevice();
    m_listener.isSubmerged = isSubmerged;
    for (size_t i = 0; i < MAX_VOICES; ++i) {
        if (m_voices[i].active && m_voices[i].is3D) {
            CalculateVoiceGains(m_voices[i], m_listener);
        }
    }
    AudioManager::Instance().UnlockAudioDevice();
}

bool AudioEngine3D::IsSubmerged() const {
    AudioManager::Instance().LockAudioDevice();
    bool sub = m_listener.isSubmerged;
    AudioManager::Instance().UnlockAudioDevice();
    return sub;
}

void AudioEngine3D::CalculateVoiceGains(AudioVoice& voice, const AudioListener3D& listener) {
    // 1. Vector Relativo Oyente -> Emisor
    Point3D delta = voice.emitter.position - listener.position;
    float dist = delta.Length();
    voice.distanceToListener = dist;

    // 2. Modelo Físico de Atenuación por Distancia (Inverse Distance Clamped Model)
    float distGain = 0.0f;
    const float minD = std::max(0.1f, voice.emitter.minDistance);
    const float maxD = std::max(minD + 0.1f, voice.emitter.maxDistance);
    const float rolloff = std::max(0.0f, voice.emitter.rolloffFactor);

    if (dist <= minD) {
        distGain = 1.0f;
    } else if (dist >= maxD) {
        distGain = 0.0f;
    } else {
        float num = minD;
        float denom = minD + rolloff * (dist - minD);
        float falloff = (maxD - dist) / (maxD - minD); // Extinción suave en el límite
        distGain = (num / denom) * falloff;
    }

    // 3. Paneo Azimutal Estéreo con Ley de Potencia Constante (Constant-Power Law) y Doppler 3D Físico
    float panL = 0.7071f;
    float panR = 0.7071f;
    float dopplerRatio = 1.0f;

    if (dist > 0.001f) {
        // Vector lateral derecho del oyente en espacio de mundo: R = Forward x Up
        Point3D right = Point3D::Cross(listener.forward, listener.up).Normalized();
        Point3D dirNorm = delta * (1.0f / dist);

        // Proyección sobre el eje horizontal lateral: [-1.0 = Izquierda pura, +1.0 = Derecha pura]
        float xLocal = Point3D::Dot(dirNorm, right);
        xLocal = std::clamp(xLocal, -1.0f, 1.0f);

        // Mapeo a ángulo theta in [0, PI/2]: panL = cos(theta), panR = sin(theta)
        // Garantiza matemáticamente que panL^2 + panR^2 = 1.0f (energía acústica constante)
        float theta = (xLocal + 1.0f) * 0.25f * PI;
        panL = std::cos(theta);
        panR = std::sin(theta);

        // Efecto Doppler físico 3D (velocidad del sonido c = 343 m/s)
        constexpr float SPEED_OF_SOUND = 343.0f;
        const float vL = Point3D::Dot(listener.velocity, dirNorm);
        const float vS = Point3D::Dot(voice.emitter.velocity, dirNorm);
        const float numD = std::max(1.0f, SPEED_OF_SOUND + vL);
        const float denD = std::max(1.0f, SPEED_OF_SOUND + vS);
        dopplerRatio = std::clamp(numD / denD, 0.5f, 2.0f);
    }

    voice.dopplerPitch = std::clamp(std::max(0.25f, voice.emitter.pitch) * dopplerRatio, 0.4f, 2.2f);

    // 4. Filtro Paso Bajo (LPF) para Oclusión Acústica y Medio Subacuático
    // Frecuencia de corte interpolada: 20 kHz (aire libre) a 750 Hz (muro de piedra / roca densa)
    float occ = std::clamp(voice.emitter.occlusion, 0.0f, 1.0f);
    float cutoffHz = 20000.0f * (1.0f - occ) + 750.0f * occ;

    // Si el oyente está sumergido, amortiguar drásticamente altas frecuencias (< 450 Hz)
    if (listener.isSubmerged) {
        cutoffHz = std::min(cutoffHz, 450.0f);
    }

    // Coeficiente discreto alpha de 1 polo: alpha = (2*pi*fc*dt) / (1 + 2*pi*fc*dt)
    if (cutoffHz >= 18000.0f) {
        voice.lpfAlpha = 1.0f; // Bypassed: respuesta en frecuencia transparente
    } else {
        float w = TWO_PI * cutoffHz * INV_SAMPLE_RATE;
        voice.lpfAlpha = std::clamp(w / (1.0f + w), 0.035f, 1.0f);
    }

    // Atenuación cromática adicional por oclusión de volumen (~ -5 dB al 100% de oclusión)
    float occVolMod = 1.0f - 0.45f * occ;
    if (listener.isSubmerged) {
        occVolMod *= 0.65f;
    }

    // 5. Ganancias Finales de Canal
    voice.targetGainL = std::clamp(voice.emitter.volume * distGain * panL * occVolMod, 0.0f, 1.5f);
    voice.targetGainR = std::clamp(voice.emitter.volume * distGain * panR * occVolMod, 0.0f, 1.5f);
    voice.perceivedLoudness = std::sqrt(voice.targetGainL * voice.targetGainL + voice.targetGainR * voice.targetGainR);
    // [OPT-249] Acoustic LOD: dormir emisores fuera de rango audible
    voice.isSleeping = (dist > voice.emitter.maxDistance * 1.15f) || (voice.perceivedLoudness < 0.002f);
}

void AudioEngine3D::RebalanceVirtualVoices() noexcept {
    // Seleccion determinista Zero-Alloc de las top MAX_PHYSICAL_VOICES (32) voces activas
    struct VoiceScore {
        uint16_t index{0};
        float score{0.0f};
    };
    std::array<VoiceScore, MAX_VIRTUAL_VOICES> candidates{};
    size_t candCount = 0;

    for (size_t i = 0; i < MAX_VIRTUAL_VOICES; ++i) {
        auto& v = m_voices[i];
        if (!v.active) continue;
        if (v.is3D && v.isSleeping) {
            v.isVirtualized = true;
            continue;
        }
        v.isVirtualized = true;
        const float score = static_cast<float>(v.priority) * 10.0f + v.perceivedLoudness * 100.0f;
        candidates[candCount++] = VoiceScore{static_cast<uint16_t>(i), score};
    }

    if (candCount <= MAX_PHYSICAL_VOICES) {
        for (size_t i = 0; i < candCount; ++i) {
            m_voices[candidates[i].index].isVirtualized = false;
        }
        return;
    }

    // Seleccion parcial de las 32 voces mas importantes (O(N * 32) sin heap)
    for (size_t k = 0; k < MAX_PHYSICAL_VOICES; ++k) {
        size_t best = k;
        for (size_t j = k + 1; j < candCount; ++j) {
            if (candidates[j].score > candidates[best].score) {
                best = j;
            }
        }
        if (best != k) {
            std::swap(candidates[k], candidates[best]);
        }
        m_voices[candidates[k].index].isVirtualized = false;
    }
}

size_t AudioEngine3D::AllocateVoice(uint8_t priority, float perceivedLoudness) {
    // Paso 1: Buscar ranura inactiva en el pool
    for (size_t i = 0; i < MAX_VOICES; ++i) {
        if (!m_voices[i].active) {
            return i;
        }
    }

    // Paso 2: Si todas están activas, ejecutar Algoritmo de Robo de Voces por Prioridad
    size_t candidateIdx = MAX_VOICES;
    uint8_t lowestPriority = 255;
    float lowestLoudness = 1e9f;

    for (size_t i = 0; i < MAX_VOICES; ++i) {
        const auto& v = m_voices[i];
        if (v.priority < lowestPriority) {
            lowestPriority = v.priority;
            lowestLoudness = v.perceivedLoudness;
            candidateIdx = i;
        } else if (v.priority == lowestPriority) {
            if (v.perceivedLoudness < lowestLoudness) {
                lowestLoudness = v.perceivedLoudness;
                candidateIdx = i;
            }
        }
    }

    // Solo robar si la voz candidata tiene menor o igual prioridad que el nuevo sonido solicitado
    if (candidateIdx < MAX_VOICES && lowestPriority <= priority) {
        m_stolenVoicesCounter++;
        return candidateIdx;
    }

    return MAX_VOICES; // Rechazar sonido (sin voces disponibles de menor prioridad)
}

SoundHandle AudioEngine3D::PlaySound3D(const SoundClip& clip, const AudioEmitter3D& emitter) {
    if (clip.samples.empty()) return SoundHandle{};

    AudioManager::Instance().LockAudioDevice();
    AudioVoice tempVoice{};
    tempVoice.emitter = emitter;
    CalculateVoiceGains(tempVoice, m_listener);

    size_t voiceIdx = AllocateVoice(emitter.priority, tempVoice.perceivedLoudness);
    if (voiceIdx >= MAX_VOICES) {
        AudioManager::Instance().UnlockAudioDevice();
        return SoundHandle{};
    }

    auto& voice = m_voices[voiceIdx];
    voice.active = true;
    voice.generation++;
    if (voice.generation == 0) voice.generation = 1; // Evitar 0
    voice.clip = &clip;
    voice.sampleOffset = 0;
    voice.samplePhase = 0.0f;
    voice.dopplerPitch = tempVoice.dopplerPitch;
    voice.isLooping = emitter.isLooping;
    voice.priority = emitter.priority;
    voice.bus = emitter.bus;
    voice.is3D = true;
    voice.emitter = emitter;

    // Asignar ganancias iniciales suaves
    voice.targetGainL = tempVoice.targetGainL;
    voice.targetGainR = tempVoice.targetGainR;
    voice.currentGainL = tempVoice.targetGainL;
    voice.currentGainR = tempVoice.targetGainR;
    voice.lpfAlpha = tempVoice.lpfAlpha;
    voice.lpfStateL = 0.0f;
    voice.lpfStateR = 0.0f;
    voice.distanceToListener = tempVoice.distanceToListener;
    voice.perceivedLoudness = tempVoice.perceivedLoudness;
    voice.isSleeping = tempVoice.isSleeping;
    voice.isVirtualized = false;

    RebalanceVirtualVoices();
    SoundHandle handle{static_cast<uint16_t>(voiceIdx), voice.generation};
    AudioManager::Instance().UnlockAudioDevice();
    return handle;
}

SoundHandle AudioEngine3D::PlaySound2D(const SoundClip& clip, float volume, bool loop, uint8_t priority, AudioBus bus) {
    if (clip.samples.empty()) return SoundHandle{};

    AudioManager::Instance().LockAudioDevice();
    size_t voiceIdx = AllocateVoice(priority, volume);
    if (voiceIdx >= MAX_VOICES) {
        AudioManager::Instance().UnlockAudioDevice();
        return SoundHandle{};
    }

    auto& voice = m_voices[voiceIdx];
    voice.active = true;
    voice.generation++;
    if (voice.generation == 0) voice.generation = 1;
    voice.clip = &clip;
    voice.sampleOffset = 0;
    voice.samplePhase = 0.0f;
    voice.dopplerPitch = 1.0f;
    voice.isLooping = loop;
    voice.priority = priority;
    voice.bus = bus;
    voice.is3D = false;

    float vol = std::clamp(volume, 0.0f, 1.5f);
    voice.targetGainL = vol;
    voice.targetGainR = vol;
    voice.currentGainL = vol;
    voice.currentGainR = vol;
    voice.lpfAlpha = 1.0f; // Bypassed
    voice.lpfStateL = 0.0f;
    voice.lpfStateR = 0.0f;
    voice.perceivedLoudness = vol;
    voice.isSleeping = false;
    voice.isVirtualized = false;

    RebalanceVirtualVoices();
    SoundHandle handle{static_cast<uint16_t>(voiceIdx), voice.generation};
    AudioManager::Instance().UnlockAudioDevice();
    return handle;
}

void AudioEngine3D::UpdateEmitter(SoundHandle handle, const AudioEmitter3D& emitter) {
    if (!handle.IsValid() || handle.voiceIndex >= MAX_VOICES) return;

    AudioManager::Instance().LockAudioDevice();
    auto& voice = m_voices[handle.voiceIndex];
    if (voice.active && voice.generation == handle.generation && voice.is3D) {
        voice.emitter = emitter;
        voice.isLooping = emitter.isLooping;
        voice.priority = emitter.priority;
        voice.bus = emitter.bus;
        CalculateVoiceGains(voice, m_listener);
    }
    AudioManager::Instance().UnlockAudioDevice();
}

void AudioEngine3D::StopSound(SoundHandle handle) {
    if (!handle.IsValid() || handle.voiceIndex >= MAX_VOICES) return;

    AudioManager::Instance().LockAudioDevice();
    auto& voice = m_voices[handle.voiceIndex];
    if (voice.active && voice.generation == handle.generation) {
        voice.active = false;
        voice.clip = nullptr;
    }
    AudioManager::Instance().UnlockAudioDevice();
}

void AudioEngine3D::StopAllSounds() {
    AudioManager::Instance().LockAudioDevice();
    for (size_t i = 0; i < MAX_VOICES; ++i) {
        m_voices[i].active = false;
        m_voices[i].clip = nullptr;
    }
    AudioManager::Instance().UnlockAudioDevice();
}

bool AudioEngine3D::IsPlaying(SoundHandle handle) const {
    if (!handle.IsValid() || handle.voiceIndex >= MAX_VOICES) return false;

    AudioManager::Instance().LockAudioDevice();
    const auto& voice = m_voices[handle.voiceIndex];
    bool playing = (voice.active && voice.generation == handle.generation);
    AudioManager::Instance().UnlockAudioDevice();
    return playing;
}

void AudioEngine3D::MixAudio(int16_t* outStereoStream, int numFrames, float masterVolume) {
    if (!outStereoStream || numFrames <= 0) return;

    // 1. Evaluar Sidechain Ducking: si un SFX de alta prioridad suena, atenuar BGM y Ambience
    bool hasDuckingTrigger = false;
    for (size_t v = 0; v < MAX_VOICES; ++v) {
        const auto& voice = m_voices[v];
        if (voice.active && !voice.isVirtualized && !voice.isSleeping &&
            voice.bus == AudioBus::SFX && voice.priority >= 60 &&
            (voice.currentGainL + voice.currentGainR) > 0.02f) {
            hasDuckingTrigger = true;
            break;
        }
    }

    const float targetDuck = hasDuckingTrigger ? 0.38f : 1.0f;
    const float blockDt = static_cast<float>(numFrames) * INV_SAMPLE_RATE;
    const float duckRate = (targetDuck < m_duckingEnvelope) ? 28.0f : 4.5f;
    const float duckAlpha = std::clamp(1.0f - std::exp(-duckRate * blockDt), 0.0f, 1.0f);
    m_duckingEnvelope += (targetDuck - m_duckingEnvelope) * duckAlpha;

    const float busMasterVol = m_busVolumes[static_cast<size_t>(AudioBus::Master)];
    const float reverbWet = std::clamp(m_reverbWetMix, 0.0f, 0.85f);
    const bool applyReverb = (reverbWet > 0.001f);

    // 2. Procesar en bloques fijos de hasta MIX_CHUNK_FRAMES sin asignaciones en heap
    int frameCursor = 0;
    while (frameCursor < numFrames) {
        const int chunkFrames = std::min<int>(numFrames - frameCursor, static_cast<int>(MIX_CHUNK_FRAMES));

        if (applyReverb) {
            std::fill_n(m_reverbSendL.data(), static_cast<size_t>(chunkFrames), 0.0f);
            std::fill_n(m_reverbSendR.data(), static_cast<size_t>(chunkFrames), 0.0f);
        }

        for (size_t v = 0; v < MAX_VOICES; ++v) {
            auto& voice = m_voices[v];
            if (!voice.active || !voice.clip || voice.clip->samples.empty()) continue;

            const auto& samples = voice.clip->samples;
            const size_t totalFrames = voice.clip->GetFrameCount();
            if (totalFrames == 0) {
                voice.active = false;
                continue;
            }
            const float totalFramesF = static_cast<float>(totalFrames);
            const float phaseStep = std::max(0.25f, voice.dopplerPitch);

            // [OPT-224 / OPT-249] Voces virtualizadas o en sueno acustico avanzan su reloj en O(1) sin DSP
            if (voice.isVirtualized || voice.isSleeping) {
                voice.samplePhase += phaseStep * static_cast<float>(chunkFrames);
                if (voice.samplePhase >= totalFramesF) {
                    if (voice.isLooping) {
                        voice.samplePhase = std::fmod(voice.samplePhase, totalFramesF);
                    } else {
                        voice.active = false;
                    }
                }
                continue;
            }

            const int channels = voice.clip->channels;
            const float alpha = voice.lpfAlpha;

            const size_t busIdx = std::min(static_cast<size_t>(voice.bus), static_cast<size_t>(AudioBus::Ambience));
            const bool duckedBus = (voice.bus == AudioBus::BGM || voice.bus == AudioBus::Ambience);
            const float busGain = busMasterVol * m_busVolumes[busIdx] * (duckedBus ? m_duckingEnvelope : 1.0f);

            for (int i = 0; i < chunkFrames; ++i) {
                // Suavizado exponencial continuo de ganancias (anti-zipper noise)
                voice.currentGainL += 0.005f * (voice.targetGainL - voice.currentGainL);
                voice.currentGainR += 0.005f * (voice.targetGainR - voice.currentGainR);

                if (voice.samplePhase >= totalFramesF) {
                    if (voice.isLooping) {
                        voice.samplePhase = std::fmod(voice.samplePhase, totalFramesF);
                    } else {
                        voice.active = false;
                        break;
                    }
                }

                const size_t idx0 = std::min(static_cast<size_t>(voice.samplePhase), totalFrames - 1);
                const float frac = voice.samplePhase - static_cast<float>(idx0);
                size_t idx1 = idx0 + 1;
                if (idx1 >= totalFrames) {
                    idx1 = voice.isLooping ? 0 : idx0;
                }

                float rawSampleL = 0.0f;
                float rawSampleR = 0.0f;

                if (channels == 1) {
                    const float s0 = static_cast<float>(samples[idx0]);
                    const float s1 = static_cast<float>(samples[idx1]);
                    rawSampleL = s0 + (s1 - s0) * frac;
                    rawSampleR = rawSampleL;
                } else {
                    const float s0L = static_cast<float>(samples[idx0 * 2]);
                    const float s1L = static_cast<float>(samples[idx1 * 2]);
                    const float s0R = static_cast<float>(samples[idx0 * 2 + 1]);
                    const float s1R = static_cast<float>(samples[idx1 * 2 + 1]);
                    rawSampleL = s0L + (s1L - s0L) * frac;
                    rawSampleR = s0R + (s1R - s0R) * frac;
                }

                voice.samplePhase += phaseStep;
                voice.sampleOffset = idx0;

                // Aplicar Filtro Paso Bajo (LPF) de 1 polo
                float filteredL = rawSampleL;
                float filteredR = rawSampleR;
                if (alpha < 0.999f) {
                    voice.lpfStateL += alpha * (rawSampleL - voice.lpfStateL);
                    voice.lpfStateR += alpha * (rawSampleR - voice.lpfStateR);
                    filteredL = voice.lpfStateL;
                    filteredR = voice.lpfStateR;
                }

                const float outL = filteredL * voice.currentGainL * busGain * masterVolume;
                const float outR = filteredR * voice.currentGainR * busGain * masterVolume;

                if (applyReverb) {
                    m_reverbSendL[i] += outL;
                    m_reverbSendR[i] += outR;
                }

                const int outIdx = (frameCursor + i) * 2;
                const int32_t curL = static_cast<int32_t>(outStereoStream[outIdx]);
                const int32_t curR = static_cast<int32_t>(outStereoStream[outIdx + 1]);

                outStereoStream[outIdx]     = static_cast<int16_t>(std::clamp(curL + static_cast<int32_t>(outL), -32768, 32767));
                outStereoStream[outIdx + 1] = static_cast<int16_t>(std::clamp(curR + static_cast<int32_t>(outR), -32768, 32767));
            }
        }

        // 3. Reverb Acustico Schroeder estereo (lineas comb primas decorrelacionadas con amortiguacion LPF)
        if (applyReverb) {
            constexpr size_t DELAY_LEN_L = 1553;
            constexpr size_t DELAY_LEN_R = 1913;
            for (int i = 0; i < chunkFrames; ++i) {
                const float tapL = m_reverbDelayL[m_reverbCursorL];
                const float tapR = m_reverbDelayR[m_reverbCursorR];

                m_reverbDampL = m_reverbDampL * 0.45f + tapL * 0.55f;
                m_reverbDampR = m_reverbDampR * 0.45f + tapR * 0.55f;

                const float fbL = m_reverbSendL[i] + 0.60f * (0.85f * m_reverbDampL + 0.15f * m_reverbDampR);
                const float fbR = m_reverbSendR[i] + 0.60f * (0.85f * m_reverbDampR + 0.15f * m_reverbDampL);

                m_reverbDelayL[m_reverbCursorL] = std::clamp(fbL, -32000.0f, 32000.0f);
                m_reverbDelayR[m_reverbCursorR] = std::clamp(fbR, -32000.0f, 32000.0f);

                m_reverbCursorL = (m_reverbCursorL + 1) % DELAY_LEN_L;
                m_reverbCursorR = (m_reverbCursorR + 1) % DELAY_LEN_R;

                const int outIdx = (frameCursor + i) * 2;
                const int32_t curL = static_cast<int32_t>(outStereoStream[outIdx]);
                const int32_t curR = static_cast<int32_t>(outStereoStream[outIdx + 1]);
                const int32_t wetL = static_cast<int32_t>(tapL * reverbWet);
                const int32_t wetR = static_cast<int32_t>(tapR * reverbWet);

                outStereoStream[outIdx]     = static_cast<int16_t>(std::clamp(curL + wetL, -32768, 32767));
                outStereoStream[outIdx + 1] = static_cast<int16_t>(std::clamp(curR + wetR, -32768, 32767));
            }
        }

        frameCursor += chunkFrames;
    }
}

AudioEngineTelemetry AudioEngine3D::GetTelemetry() const {
    AudioManager::Instance().LockAudioDevice();
    AudioEngineTelemetry telem{};
    telem.totalVoices = MAX_PHYSICAL_VOICES;
    telem.maxVirtualVoices = MAX_VIRTUAL_VOICES;
    telem.stolenVoicesTotal = m_stolenVoicesCounter;
    telem.listenerX = m_listener.position.x;
    telem.listenerY = m_listener.position.y;
    telem.listenerZ = m_listener.position.z;
    telem.listenerSubmerged = m_listener.isSubmerged;

    for (size_t i = 0; i < MAX_VOICES; ++i) {
        if (m_voices[i].active) {
            if (m_voices[i].isSleeping) {
                telem.sleepingVoices++;
            } else if (m_voices[i].isVirtualized) {
                telem.virtualizedVoices++;
            } else {
                telem.activeVoices++;
            }
            if (m_voices[i].is3D) {
                telem.maxOcclusionInScene = std::max(telem.maxOcclusionInScene, m_voices[i].emitter.occlusion);
            }
        }
    }
    AudioManager::Instance().UnlockAudioDevice();
    return telem;
}

} // namespace Neleo
