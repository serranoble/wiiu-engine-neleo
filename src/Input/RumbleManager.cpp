#include "Input/RumbleManager.hpp"
#include "Input/InputManager.hpp"
#include "Core/Config/EngineConfig.hpp"

#if defined(__WIIU__) || defined(__wiiu__)
#include <vpad/input.h>
#include <padscore/wpad.h>
#elif defined(_WIN32)
namespace Neleo::Platform {
void SetWin32HardwareRumble(int playerIdx, float intensity, bool active) noexcept;
}
#endif

namespace Neleo {

void RumbleManager::Init() {
    if (m_initialized) return;

#if defined(__WIIU__) || defined(__wiiu__)
    WPADEnableMotor(TRUE);
    WPADEnableURCC(TRUE);
#endif

    m_initialized = true;
}

void RumbleManager::Update(float dt) {
    if (!m_initialized) Init();

    const bool rumbleEnabled = EngineConfig::Instance().GetControls().rumbleEnabled;

    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        auto& ch = m_channels[i];
        if (ch.remainingTime > 0.0f) {
            ch.remainingTime -= dt;
            ch.pulseInterval -= dt;
            if (ch.remainingTime <= 0.0f || !rumbleEnabled) {
                ch.remainingTime = 0.0f;
                ch.pulseInterval = 0.0f;
                ch.intensity = 0.0f;
                SetHardwareRumble(static_cast<PlayerIndex>(i), 0.0f, false);
                ch.isActuatorOn = false;
            } else if (ch.isActuatorOn && ch.pulseInterval <= 0.0f) {
                // Sostener el ciclo de trabajo del GamePad DRC cada 160 ms sin saturar el bus IPC en cada frame
                ch.pulseInterval = 0.16f;
                SetHardwareRumble(static_cast<PlayerIndex>(i), ch.intensity, true);
            }
        }
    }
}

void RumbleManager::TriggerRumble(PlayerIndex player, float intensity, float durationSeconds) {
    if (!EngineConfig::Instance().GetControls().rumbleEnabled) return;
    if (durationSeconds <= 0.0f || intensity <= 0.0f) return;

    size_t idx = static_cast<size_t>(player);
    if (idx >= NUM_CHANNELS) return;

    float finalIntensity = std::clamp(intensity * EngineConfig::Instance().GetControls().rumbleIntensity, 0.0f, 1.0f);
    if (finalIntensity <= 0.0f) return;

    auto& ch = m_channels[idx];
    ch.remainingTime = std::max(ch.remainingTime, durationSeconds);
    ch.intensity = finalIntensity;
    ch.pulseInterval = 0.16f;

    SetHardwareRumble(player, ch.intensity, true);
    ch.isActuatorOn = true;
}

void RumbleManager::PlayPreset(PlayerIndex player, RumblePreset preset) {
    switch (preset) {
        case RumblePreset::SubtleTick:
            TriggerRumble(player, 0.25f, 0.08f);
            break;
        case RumblePreset::JumpLand:
            TriggerRumble(player, 0.50f, 0.15f);
            break;
        case RumblePreset::DamageImpact:
            TriggerRumble(player, 0.85f, 0.35f);
            break;
        case RumblePreset::HeavyExplosion:
            TriggerRumble(player, 1.00f, 0.60f);
            break;
    }
}

void RumbleManager::StopRumble(PlayerIndex player) noexcept {
    size_t idx = static_cast<size_t>(player);
    if (idx >= NUM_CHANNELS) return;

    auto& ch = m_channels[idx];
    const bool wasOn = ch.isActuatorOn;
    ch.remainingTime = 0.0f;
    ch.pulseInterval = 0.0f;
    ch.intensity = 0.0f;
    ch.isActuatorOn = false;

    if (wasOn && m_initialized) {
        SetHardwareRumble(player, 0.0f, false);
    }
}

void RumbleManager::StopAll() noexcept {
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        StopRumble(static_cast<PlayerIndex>(i));
    }
}

void RumbleManager::Shutdown() noexcept {
    StopAll();
    m_initialized = false;
}

bool RumbleManager::IsRumbling(PlayerIndex player) const noexcept {
    size_t idx = static_cast<size_t>(player);
    if (idx >= NUM_CHANNELS) return false;
    return m_channels[idx].isActuatorOn && m_channels[idx].remainingTime > 0.0f;
}

void RumbleManager::SetHardwareRumble(PlayerIndex player, float intensity, bool active) noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    auto calculateBytePattern = [](float val) noexcept -> uint8_t {
        if (val <= 0.35f) return 0x11;      // 30% suave (25% duty cycle)
        if (val <= 0.75f) return 0xEE;      // 70% medio (75% duty cycle)
        return 0xFF;                        // 100% fuerte (potencia continua)
    };

    const auto& pad0 = InputManager::Instance().GetGamepad(0);
    const bool slot0IsDRC = (pad0.isConnected && pad0.type == ControllerType::GamePadDRC);

    auto shouldStopDRC = [this, slot0IsDRC]() noexcept -> bool {
        bool ch0Active = slot0IsDRC && m_channels[0].isActuatorOn && m_channels[0].remainingTime > 0.0f;
        bool ch4Active = m_channels[4].isActuatorOn && m_channels[4].remainingTime > 0.0f;
        return !ch0Active && !ch4Active;
    };

    if (player == PlayerIndex::GamePad) {
        if (active && intensity > 0.0f) {
            alignas(32) uint8_t pattern[4];
            uint8_t byteVal = calculateBytePattern(intensity);
            pattern[0] = byteVal;
            pattern[1] = byteVal;
            pattern[2] = byteVal;
            pattern[3] = byteVal;
            VPADControlMotor(VPAD_CHAN_0, pattern, static_cast<uint8_t>(sizeof(pattern) * 8));
        } else if (shouldStopDRC()) {
            VPADStopMotor(VPAD_CHAN_0);
        }
        return;
    }

    size_t playerIdx = static_cast<size_t>(player);
    if (playerIdx < InputManager::MAX_PLAYERS) {
        const auto& pad = InputManager::Instance().GetGamepad(playerIdx);
        if (pad.type == ControllerType::GamePadDRC) {
            if (active && intensity > 0.0f) {
                alignas(32) uint8_t pattern[4];
                uint8_t byteVal = calculateBytePattern(intensity);
                pattern[0] = byteVal;
                pattern[1] = byteVal;
                pattern[2] = byteVal;
                pattern[3] = byteVal;
                VPADControlMotor(VPAD_CHAN_0, pattern, static_cast<uint8_t>(sizeof(pattern) * 8));
            } else if (shouldStopDRC()) {
                VPADStopMotor(VPAD_CHAN_0);
            }
        } else if (pad.isConnected) {
            // [AUDITORÍA SILICIO]: Evitar colisión de canales y corregir desfase de slots
            const bool drcActive = (pad0.isConnected && pad0.type == ControllerType::GamePadDRC);
            int wpadChan = drcActive ? static_cast<int>(playerIdx) - 1 : static_cast<int>(playerIdx);
            if (wpadChan >= 0 && wpadChan < 4) {
                WPADControlMotor(static_cast<WPADChan>(wpadChan), active ? TRUE : FALSE);
            }
        }
    }
#elif defined(_WIN32)
    const int targetIdx = (player == PlayerIndex::GamePad) ? 0 : static_cast<int>(player);
    Platform::SetWin32HardwareRumble(targetIdx, intensity, active);
#else
    (void)player; (void)intensity; (void)active;
#endif
}

} // namespace Neleo
