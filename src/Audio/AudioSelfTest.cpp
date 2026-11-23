#include "Audio/AudioSelfTest.hpp"
#include "Audio/AudioEngine3D.hpp"
#include <cmath>
#include <cstdint>

namespace WiiU::Audio {

namespace {

[[nodiscard]] bool IsFiniteVec3(const Point3D& p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

} // namespace

void AudioSelfTest::RunAll(Diagnostics::EngineSelfTestReport& outReport) {
    auto& engine = AudioEngine3D::Instance();

    // 1. Telemetría de Voice Pool y Buses de submezcla dentro de límites
    const auto telem = engine.GetTelemetry();
    {
        const float sfxVol = engine.GetBusVolume(AudioBus::SFX);
        const float bgmVol = engine.GetBusVolume(AudioBus::BGM);
        const float duck = engine.GetActiveDuckingGain();
        const bool ok = (telem.activeVoices <= AudioEngine3D::MAX_PHYSICAL_VOICES) &&
                        std::isfinite(sfxVol) && (sfxVol >= 0.0f) && (sfxVol <= 2.0f) &&
                        std::isfinite(bgmVol) && (bgmVol >= 0.0f) && (bgmVol <= 2.0f) &&
                        std::isfinite(duck) && (duck >= 0.0f) && (duck <= 1.5f);
        outReport.Record("AUDIO", "AUDIO_3D_TELEMETRY", ok,
                         ok ? "VOICE POOL + BUSES OK" : "FALLO TELEMETRIA AUDIO3D");
    }

    // 2. Estado DSP thread-safe (reverb/doppler/handle inválido/listener sin tocar MixAudio en vivo)
    {
        const float reverbWet = engine.GetReverbWetMix();
        const float doppler = engine.GetLastDopplerPitch();
        const bool dspParamsOk = std::isfinite(reverbWet) && (reverbWet >= 0.0f) && (reverbWet <= 1.0f) &&
                                 std::isfinite(doppler) && (doppler >= 0.25f) && (doppler <= 4.0f) &&
                                 !engine.IsPlaying(SoundHandle{});

        const AudioListener3D prevListener = engine.GetListener();
        bool listenerOk = IsFiniteVec3(prevListener.position) &&
                          IsFiniteVec3(prevListener.forward) &&
                          IsFiniteVec3(prevListener.up);

        // Solo probar round-trip de SetListener si no hay voces 3D activas (0 perturbación al hilo AX)
        if (listenerOk && telem.activeVoices == 0u) {
            AudioListener3D probe = prevListener;
            probe.position = Point3D{3.5f, 1.5f, -2.0f};
            engine.SetListener(probe);
            const AudioListener3D readBack = engine.GetListener();
            engine.SetListener(prevListener);

            listenerOk = (std::fabs(readBack.position.x - 3.5f) < 0.01f) &&
                         (std::fabs(readBack.position.y - 1.5f) < 0.01f) &&
                         (std::fabs(readBack.position.z + 2.0f) < 0.01f);
        }

        const bool ok = dspParamsOk && listenerOk;
        outReport.Record("AUDIO", "AUDIO_3D_LISTENER_STATE", ok,
                         ok ? "DSP/LISTENER THREAD-SAFE OK" : "FALLO AUDIO3D STATE");
    }
}

} // namespace WiiU::Audio
