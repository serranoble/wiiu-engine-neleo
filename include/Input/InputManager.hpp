#pragma once

#include "Core/StackString.hpp"
#include "Input/VirtualGamepad.hpp"
#include "Input/ActionMapping.hpp"
#include "Input/InputContextStack.hpp"
#include "Input/RumbleManager.hpp"
#include <atomic>
#include <array>
#include <cstddef>
#include <string_view>

namespace Neleo {

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
 * - Teclado, ratón, táctil y mandos XInput nativos en Windows / ROG Ally
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
    // [OPT-400] ENRUTAMIENTO JERARQUICO POR CONTEXTO DE ENTRADA
    // =========================================================================
    [[nodiscard]] InputContextStack& GetContextStack() noexcept { return m_contextStack; }
    [[nodiscard]] const InputContextStack& GetContextStack() const noexcept { return m_contextStack; }

    [[nodiscard]] float GetActionAxisInContext(InputContext ctx, InputAction action, size_t player = 0) const noexcept {
        return m_contextStack.IsActionAllowed(ctx, action) ? GetActionAxis(action, player) : 0.0f;
    }

    [[nodiscard]] bool IsActionHeldInContext(InputContext ctx, InputAction action, size_t player = 0) const noexcept {
        return m_contextStack.IsActionAllowed(ctx, action) && IsActionHeld(action, player);
    }

    [[nodiscard]] bool WasActionTriggeredInContext(InputContext ctx, InputAction action, size_t player = 0) const noexcept {
        return m_contextStack.IsActionAllowed(ctx, action) && WasActionTriggered(action, player);
    }

    // =========================================================================
    // [OPT-401] GUARDIA DE DESCONEXION HID Y AUTO-PAUSA
    // =========================================================================
    using DisconnectCallback = void (*)(size_t playerIndex, void* userData);

    void SetAutoPauseOnDisconnect(bool enabled) noexcept { m_autoPauseOnDisconnect = enabled; }
    [[nodiscard]] bool IsAutoPauseOnDisconnectEnabled() const noexcept { return m_autoPauseOnDisconnect; }
    void SetDisconnectCallback(DisconnectCallback cb, void* userData = nullptr) noexcept {
        m_disconnectCallback = cb;
        m_disconnectUserData = userData;
    }
    [[nodiscard]] bool WasControllerDisconnectedThisFrame() const noexcept { return m_controllerDisconnectedThisFrame; }
    [[nodiscard]] size_t GetLastDisconnectedPlayer() const noexcept { return m_lastDisconnectedPlayer; }

    using ActionSoundCallback = void(*)(void* userData);
    void SetActionSoundCallback(ActionSoundCallback cb, void* userData = nullptr) noexcept {
        m_actionSoundCallback = cb;
        m_actionSoundUserData = userData;
    }

    // =========================================================================
    // MÉTODOS DE RETROCOMPATIBILIDAD (100% PRESERVADOS)
    // =========================================================================
    [[nodiscard]] bool WasActionTriggeredThisFrame() const noexcept { return m_actionTriggeredThisFrame; }
    void TriggerAction(std::string_view sourceName);

    [[nodiscard]] const TouchState& GetTouchState() const noexcept { return m_touchState; }
    [[nodiscard]] const StickState& GetLeftStick() const noexcept { return m_leftStick; }
    [[nodiscard]] const StickState& GetRightStick() const noexcept { return m_rightStick; }
    [[nodiscard]] bool IsButtonHeld() const noexcept { return m_buttonHeld; }
    [[nodiscard]] int GetButtonPressCount() const noexcept { return m_buttonPressCount; }
    [[nodiscard]] const StackString<32>& GetLastButtonName() const noexcept { return m_lastButtonName; }
    [[nodiscard]] float GetFlashTimer() const noexcept { return m_flashTimer; }
    [[nodiscard]] bool IsBackRequested() const noexcept { return m_backRequested; }
    [[nodiscard]] bool IsPauseRequested() const noexcept { return m_pauseRequested; }
    void ConsumePauseRequest() noexcept { m_pauseRequested = false; }
    [[nodiscard]] bool IsResourceMonitorRequested() const noexcept { return m_resourceMonitorRequested; }
    void ConsumeResourceMonitorRequest() noexcept { m_resourceMonitorRequested = false; }
    [[nodiscard]] bool IsExitRequested() const noexcept { return m_exitRequested.load(std::memory_order_acquire); }
    void RequestExit() noexcept { m_exitRequested.store(true, std::memory_order_release); }
    void ConsumeExitRequest() noexcept { m_exitRequested.store(false, std::memory_order_release); }
    [[nodiscard]] uint32_t GetButtonsHold() const noexcept { return m_buttonsHold; }
    [[nodiscard]] uint32_t GetButtonsTrigger() const noexcept { return m_buttonsTrigger; }
    [[nodiscard]] bool IsButtonHeld(uint32_t buttonMask) const noexcept { return (m_buttonsHold & buttonMask) != 0; }
    [[nodiscard]] bool IsButtonTriggered(uint32_t buttonMask) const noexcept { return (m_buttonsTrigger & buttonMask) != 0; }

    // Limpia flancos de disparo (triggers/releases/touch.justPressed) sin perder el estado mantenido (buttonsHold)
    void ClearFrameTriggers() noexcept {
        for (size_t p = 0u; p < MAX_PLAYERS; ++p) {
            m_gamepads[p].buttonsTrigger = 0u;
            m_gamepads[p].buttonsRelease = 0u;
            m_gamepads[p].touch.justPressed = false;
        }
        m_buttonsTrigger = 0u;
        m_touchState.justPressed = false;
        m_actionTriggeredThisFrame = false;
        m_backRequested = false;
        m_pauseRequested = false;
    }

private:
    InputManager() = default;
    ~InputManager();

    void PollPlatformEvents(bool& outRunning);
    void PollWiiUHardware(float dt);
    void PollDesktopHardware(bool& outRunning);

    VirtualGamepad m_gamepads[MAX_PLAYERS]{};
    bool m_prevConnected[MAX_PLAYERS]{};
    InputContextStack m_contextStack{};
    bool m_autoPauseOnDisconnect{true};
    bool m_controllerDisconnectedThisFrame{false};
    size_t m_lastDisconnectedPlayer{0};
    DisconnectCallback m_disconnectCallback{nullptr};
    void* m_disconnectUserData{nullptr};
    ActionSoundCallback m_actionSoundCallback{nullptr};
    void* m_actionSoundUserData{nullptr};

    bool m_actionTriggeredThisFrame{false};
    bool m_backRequested{false};
    bool m_pauseRequested{false};
    bool m_resourceMonitorRequested{false};
    std::atomic<bool> m_exitRequested{false};
    uint32_t m_buttonsHold{0};
    uint32_t m_buttonsTrigger{0};
    TouchState m_touchState{};
    StickState m_leftStick{};
    StickState m_rightStick{};
    bool m_buttonHeld{false};
    bool m_prevTouched{false};
    bool m_prevMousePressed{false};
    int m_buttonPressCount{0};
    StackString<32> m_lastButtonName{"NINGUNO"};
    float m_flashTimer{0.0f};
    float m_touchDebounceTimer{0.0f};
};

} // namespace Neleo
