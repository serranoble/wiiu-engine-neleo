#pragma once

#include <SDL2/SDL.h>
#if defined(__WIIU__) || defined(__wiiu__)
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#endif
#include "Input/VirtualGamepad.hpp"
#include "Input/ActionMapping.hpp"
#include "Input/RumbleManager.hpp"
#include <string>
#include <cstddef>

namespace WiiU {

// Estructura de retrocompatibilidad
struct StickState {
    float x{0.0f};
    float y{0.0f};
};

/**
 * @brief [Jason Gregory Cap. 8: "Human Interface Devices - HID"]
 * Gestor Central de Entrada de Hardware y Capa de Abstracción Multi-Mando (HAL).
 * 
 * Soporta de forma simultánea:
 * - Nintendo Wii U GamePad (VPAD con touch calibrado y sensores IMU)
 * - Wii U Pro Controller (WPAD_EXT_PRO_CONTROLLER)
 * - Wii Remote independiente en modo vertical u horizontal (WPAD_EXT_CORE)
 * - Wii Remote + Nunchuk (WPAD_EXT_NUNCHUK)
 * - Classic Controller / Classic Controller Pro (WPAD_EXT_CLASSIC)
 * - Teclado, ratón y mandos de desarrollo PC en SDL2
 * 
 * Proporciona filtrado de zona muerta radial continuo, flancos de subida y bajada,
 * y mapeo abstracto de acciones para desacoplar el hardware de la simulación 3D.
 */
class InputManager {
public:
    static constexpr size_t MAX_PLAYERS = 4;

    static InputManager& Instance() {
        static InputManager instance;
        return instance;
    }

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;
    InputManager(InputManager&&) = delete;
    InputManager& operator=(InputManager&&) = delete;

    void Init();
    void Update(bool& outRunning, float dt = 0.016f);

    // =========================================================================
    // API MODERNA (HAL & VIRTUAL GAMEPAD - JASON GREGORY CAP. 8.5)
    // =========================================================================
    [[nodiscard]] const VirtualGamepad& GetGamepad(size_t player = 0) const noexcept {
        return (player < MAX_PLAYERS) ? m_gamepads[player] : m_gamepads[0];
    }

    [[nodiscard]] VirtualGamepad& GetGamepad(size_t player = 0) noexcept {
        return (player < MAX_PLAYERS) ? m_gamepads[player] : m_gamepads[0];
    }

    [[nodiscard]] size_t GetConnectedGamepadCount() const noexcept;

    [[nodiscard]] float GetActionAxis(InputAction action, size_t player = 0) const noexcept {
        return ActionMap::GetAxis(GetGamepad(player), action);
    }

    [[nodiscard]] bool IsActionHeld(InputAction action, size_t player = 0) const noexcept {
        return ActionMap::IsActionHeld(GetGamepad(player), action);
    }

    [[nodiscard]] bool WasActionTriggered(InputAction action, size_t player = 0) const noexcept {
        return ActionMap::WasActionTriggered(GetGamepad(player), action);
    }

    // =========================================================================
    // MÉTODOS DE RETROCOMPATIBILIDAD (100% PRESERVADOS)
    // =========================================================================
    bool WasActionTriggeredThisFrame() const { return m_actionTriggeredThisFrame; }
    void TriggerAction(const std::string& sourceName);

    const TouchState& GetTouchState() const { return m_touchState; }
    const StickState& GetLeftStick() const { return m_leftStick; }
    const StickState& GetRightStick() const { return m_rightStick; }
    bool IsButtonHeld() const { return m_buttonHeld; }
    int GetButtonPressCount() const { return m_buttonPressCount; }
    const std::string& GetLastButtonName() const { return m_lastButtonName; }
    float GetFlashTimer() const { return m_flashTimer; }
    [[nodiscard]] bool IsBackRequested() const { return m_backRequested; }
    [[nodiscard]] bool IsPauseRequested() const noexcept { return m_pauseRequested; }
    void ConsumePauseRequest() noexcept { m_pauseRequested = false; }
    [[nodiscard]] bool IsResourceMonitorRequested() const noexcept { return m_resourceMonitorRequested; }
    void ConsumeResourceMonitorRequest() noexcept { m_resourceMonitorRequested = false; }
    [[nodiscard]] bool IsExitRequested() const noexcept { return m_exitRequested; }
    void RequestExit() noexcept { m_exitRequested = true; }
    void ConsumeExitRequest() noexcept { m_exitRequested = false; }
    [[nodiscard]] uint32_t GetButtonsHold() const noexcept { return m_buttonsHold; }
    [[nodiscard]] uint32_t GetButtonsTrigger() const noexcept { return m_buttonsTrigger; }
    [[nodiscard]] bool IsButtonHeld(uint32_t buttonMask) const noexcept { return (m_buttonsHold & buttonMask) != 0; }
    [[nodiscard]] bool IsButtonTriggered(uint32_t buttonMask) const noexcept { return (m_buttonsTrigger & buttonMask) != 0; }

private:
    InputManager() = default;
    ~InputManager() = default;

    void OpenConnectedControllers();
    void PollSDLEvents(bool& outRunning);
    void PollWiiUHardware(float dt);
    void PollSDLHardware(bool& outRunning);

    VirtualGamepad m_gamepads[MAX_PLAYERS]{};

    bool m_actionTriggeredThisFrame{false};
    bool m_backRequested{false};
    bool m_pauseRequested{false};
    bool m_resourceMonitorRequested{false};
    bool m_exitRequested{false};
    uint32_t m_buttonsHold{0};
    uint32_t m_buttonsTrigger{0};
    TouchState m_touchState{};
    StickState m_leftStick{};
    StickState m_rightStick{};
    bool m_buttonHeld{false};
    bool m_prevTouched{false};
    bool m_prevMousePressed{false};
    int m_buttonPressCount{0};
    std::string m_lastButtonName{"NINGUNO"};
    float m_flashTimer{0.0f};
    float m_touchDebounceTimer{0.0f};
};

} // namespace WiiU
