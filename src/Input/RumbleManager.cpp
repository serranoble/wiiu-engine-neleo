#include "Input/RumbleManager.hpp"
#include "Input/InputManager.hpp"
#include "Core/Config/EngineConfig.hpp"
#include <SDL2/SDL.h>

#if defined(__WIIU__) || defined(__wiiu__)
#include <vpad/input.h>
#include <padscore/wpad.h>
#endif

namespace WiiU {

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
            if (ch.remainingTime <= 0.0f || !rumbleEnabled) {
                ch.remainingTime = 0.0f;
                ch.intensity = 0.0f;
                SetHardwareRumble(static_cast<PlayerIndex>(i), 0.0f, false);
                ch.isActuatorOn = false;
            } else if (ch.isActuatorOn) {
                // [AUDITORÍA SILICIO]: Sostener el ciclo de trabajo del GamePad DRC (patrones de 160 ms de hardware)
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
    ch.remainingTime = 0.0f;
    ch.intensity = 0.0f;
    SetHardwareRumble(player, 0.0f, false);
    ch.isActuatorOn = false;
}

void RumbleManager::StopAll() noexcept {
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        StopRumble(static_cast<PlayerIndex>(i));
    }
}

bool RumbleManager::IsRumbling(PlayerIndex player) const noexcept {
    size_t idx = static_cast<size_t>(player);
    if (idx >= NUM_CHANNELS) return false;
    return m_channels[idx].isActuatorOn && m_channels[idx].remainingTime > 0.0f;
}

void RumbleManager::SetHardwareRumble(PlayerIndex player, float intensity, bool active) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (player == PlayerIndex::GamePad) {
        if (active && intensity > 0.0f) {
            alignas(32) uint8_t pattern[4];
            uint8_t byteVal = (intensity > 0.5f) ? 0xFF : 0xAA;
            pattern[0] = byteVal;
            pattern[1] = byteVal;
            pattern[2] = byteVal;
            pattern[3] = byteVal;
            VPADControlMotor(VPAD_CHAN_0, pattern, sizeof(pattern));
        } else {
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
                uint8_t byteVal = (intensity > 0.5f) ? 0xFF : 0xAA;
                pattern[0] = byteVal;
                pattern[1] = byteVal;
                pattern[2] = byteVal;
                pattern[3] = byteVal;
                VPADControlMotor(VPAD_CHAN_0, pattern, sizeof(pattern));
            } else {
                VPADStopMotor(VPAD_CHAN_0);
            }
        } else if (pad.isConnected) {
            // [AUDITORÍA SILICIO]: Evitar colisión de canales y corregir desfase de slots
            const bool drcActive = (InputManager::Instance().GetGamepad(0).type == ControllerType::GamePadDRC);
            int wpadChan = drcActive ? static_cast<int>(playerIdx) - 1 : static_cast<int>(playerIdx);
            if (wpadChan >= 0 && wpadChan < 4) {
                WPADControlMotor(static_cast<WPADChan>(wpadChan), active ? TRUE : FALSE);
            }
        }
    }
#endif

    // Soporte PC / SDL2
    int targetIdx = static_cast<int>(player);
    int numJoysticks = SDL_NumJoysticks();
    if (targetIdx < numJoysticks) {
        SDL_GameController* controller = SDL_GameControllerFromInstanceID(targetIdx);
        if (controller) {
            uint16_t freq = active ? static_cast<uint16_t>(intensity * 65535.0f) : 0;
            uint32_t ms = active ? 250 : 0;
            SDL_GameControllerRumble(controller, freq, freq, ms);
        }
    }
}

} // namespace WiiU
