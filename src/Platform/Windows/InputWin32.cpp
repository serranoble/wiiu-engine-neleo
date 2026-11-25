#include "Input/InputManager.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Platform/Win32InputBridge.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>
#include <cstdint>

namespace WiiU {

namespace {

// Carga dinámica segura de XInput (prioriza xinput1_4.dll de Windows 8/10/11/ROG Ally y fallback xinput9_1_0.dll)
using PFNXINPUTGETSTATE = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);

PFNXINPUTGETSTATE ResolveXInputGetState() noexcept {
    static bool s_probed = false;
    static PFNXINPUTGETSTATE s_fn = nullptr;
    if (!s_probed) {
        s_probed = true;
        HMODULE mod = LoadLibraryW(L"xinput1_4.dll");
        if (!mod) {
            mod = LoadLibraryW(L"xinput9_1_0.dll");
        }
        if (mod) {
            FARPROC raw = GetProcAddress(mod, "XInputGetState");
            if (raw) {
                s_fn = reinterpret_cast<PFNXINPUTGETSTATE>(reinterpret_cast<void*>(raw));
            }
        }
    }
    return s_fn;
}

inline float NormalizeXInputAxis(SHORT val) noexcept {
    return (val < 0)
        ? (static_cast<float>(val) / 32768.0f)
        : (static_cast<float>(val) / 32767.0f);
}

} // namespace

void InputManager::PollDesktopHardware(bool& outRunning) {
    auto& bridge = Platform::GetWin32InputBridge();
    if (bridge.quitRequested) {
        m_exitRequested.store(true, std::memory_order_release);
        outRunning = false;
        bridge.ConsumeFrameEdges();
        return;
    }

    const float deadzone = EngineConfig::Instance().GetControls().deadzoneRadius;
    const PFNXINPUTGETSTATE xinputGetState = ResolveXInputGetState();
    const uint32_t prevHold0 = m_gamepads[0].buttonsHold;
    bool xinput0Connected = false;

    // 1. Muestrear hasta 4 mandos nativos XInput (ROG Ally integrado + mandos Xbox/USB)
    for (DWORD i = 0u; i < static_cast<DWORD>(MAX_PLAYERS); ++i) {
        auto& pad = m_gamepads[i];
        const uint32_t prevHold = pad.buttonsHold;

        XINPUT_STATE state{};
        const bool connected = (xinputGetState != nullptr) && (xinputGetState(i, &state) == ERROR_SUCCESS);
        if (connected) {
            if (i == 0u) {
                xinput0Connected = true;
            }
            pad.isConnected = true;
            pad.type = ControllerType::NativeGamepad;
            pad.batteryLevel = 100u;

            uint32_t newHold = 0u;
            const WORD w = state.Gamepad.wButtons;
            if (w & XINPUT_GAMEPAD_A)              newHold |= static_cast<uint32_t>(VirtualButton::A);
            if (w & XINPUT_GAMEPAD_B)              newHold |= static_cast<uint32_t>(VirtualButton::B);
            if (w & XINPUT_GAMEPAD_X)              newHold |= static_cast<uint32_t>(VirtualButton::X);
            if (w & XINPUT_GAMEPAD_Y)              newHold |= static_cast<uint32_t>(VirtualButton::Y);
            if (w & XINPUT_GAMEPAD_LEFT_SHOULDER)  newHold |= static_cast<uint32_t>(VirtualButton::L);
            if (w & XINPUT_GAMEPAD_RIGHT_SHOULDER) newHold |= static_cast<uint32_t>(VirtualButton::R);
            if (w & XINPUT_GAMEPAD_START)          newHold |= static_cast<uint32_t>(VirtualButton::Plus);
            if (w & XINPUT_GAMEPAD_BACK)           newHold |= static_cast<uint32_t>(VirtualButton::Minus);
            if (w & XINPUT_GAMEPAD_DPAD_UP)        newHold |= static_cast<uint32_t>(VirtualButton::DPadUp);
            if (w & XINPUT_GAMEPAD_DPAD_DOWN)      newHold |= static_cast<uint32_t>(VirtualButton::DPadDown);
            if (w & XINPUT_GAMEPAD_DPAD_LEFT)      newHold |= static_cast<uint32_t>(VirtualButton::DPadLeft);
            if (w & XINPUT_GAMEPAD_DPAD_RIGHT)     newHold |= static_cast<uint32_t>(VirtualButton::DPadRight);
            if (w & XINPUT_GAMEPAD_LEFT_THUMB)     newHold |= static_cast<uint32_t>(VirtualButton::StickL);
            if (w & XINPUT_GAMEPAD_RIGHT_THUMB)    newHold |= static_cast<uint32_t>(VirtualButton::StickR);
            if (state.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
                newHold |= static_cast<uint32_t>(VirtualButton::ZL);
            }
            if (state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
                newHold |= static_cast<uint32_t>(VirtualButton::ZR);
            }

            pad.buttonsHold = newHold;
            pad.buttonsTrigger = newHold & ~prevHold;
            pad.buttonsRelease = prevHold & ~newHold;
            pad.leftStick = ApplyRadialDeadzone(
                NormalizeXInputAxis(state.Gamepad.sThumbLX),
                NormalizeXInputAxis(state.Gamepad.sThumbLY),
                deadzone);
            pad.rightStick = ApplyRadialDeadzone(
                NormalizeXInputAxis(state.Gamepad.sThumbRX),
                NormalizeXInputAxis(state.Gamepad.sThumbRY),
                deadzone);
            pad.triggers.left = static_cast<float>(state.Gamepad.bLeftTrigger) / 255.0f;
            pad.triggers.right = static_cast<float>(state.Gamepad.bRightTrigger) / 255.0f;

            if (pad.buttonsTrigger != 0u) {
                TriggerAction("XINPUT");
            }
        } else {
            pad.isConnected = false;
            pad.buttonsHold = 0u;
            pad.buttonsTrigger = 0u;
            pad.buttonsRelease = 0u;
            pad.leftStick = {};
            pad.rightStick = {};
            pad.triggers = {};
        }
    }

    // 2. Combinar Teclado y Ratón/Táctil Win32 sobre Player 1 (pad0)
    auto& pad0 = m_gamepads[0];
    if (!xinput0Connected) {
        pad0.isConnected = true;
        pad0.type = ControllerType::KeyboardMouse;
        pad0.batteryLevel = 100u;
    }

    uint32_t kbHold = 0u;
    uint32_t kbTrig = 0u;
    auto mapKey = [&](uint8_t vk, VirtualButton btn) {
        const uint32_t mask = static_cast<uint32_t>(btn);
        if (bridge.keysDown[vk]) kbHold |= mask;
        if (bridge.keysPressed[vk]) kbTrig |= mask;
    };

    mapKey(VK_RETURN, VirtualButton::A);
    mapKey(VK_SPACE,  VirtualButton::A);
    mapKey('B',       VirtualButton::B);
    mapKey(VK_BACK,   VirtualButton::B);
    mapKey(VK_ESCAPE, VirtualButton::B);
    mapKey('P',       VirtualButton::Plus);
    mapKey(VK_TAB,    VirtualButton::Minus);
    mapKey(VK_OEM_MINUS, VirtualButton::Minus);
    mapKey('X',       VirtualButton::X);
    mapKey('Y',       VirtualButton::Y);
    mapKey('Q',       VirtualButton::ZL);
    mapKey('Z',       VirtualButton::ZL);
    mapKey('E',       VirtualButton::ZR);
    mapKey('R',       VirtualButton::StickR);
    mapKey(VK_UP,     VirtualButton::DPadUp);
    mapKey(VK_DOWN,   VirtualButton::DPadDown);
    mapKey(VK_LEFT,   VirtualButton::DPadLeft);
    mapKey(VK_RIGHT,  VirtualButton::DPadRight);

    if (bridge.keysPressed[VK_HOME]) {
        m_exitRequested.store(true, std::memory_order_release);
        outRunning = false;
    }
    if (kbTrig != 0u) {
        TriggerAction("TECLADO");
    }

    pad0.buttonsHold |= kbHold;

    // Sticks desde teclado (reiniciar a 0 al soltar cuando no hay XInput en Player 0)
    float kLeftX = 0.0f, kLeftY = 0.0f;
    if (bridge.keysDown['D']) kLeftX += 1.0f;
    if (bridge.keysDown['A']) kLeftX -= 1.0f;
    if (bridge.keysDown['W']) kLeftY += 1.0f;
    if (bridge.keysDown['S']) kLeftY -= 1.0f;
    if (!xinput0Connected || kLeftX != 0.0f || kLeftY != 0.0f) {
        pad0.leftStick = ApplyRadialDeadzone(kLeftX, kLeftY, 0.0f);
    }

    float kRightX = 0.0f, kRightY = 0.0f;
    if (bridge.keysDown['L']) kRightX += 1.0f;
    if (bridge.keysDown['J']) kRightX -= 1.0f;
    if (bridge.keysDown['I']) kRightY += 1.0f;
    if (bridge.keysDown['K']) kRightY -= 1.0f;
    if (!xinput0Connected || kRightX != 0.0f || kRightY != 0.0f) {
        pad0.rightStick = ApplyRadialDeadzone(kRightX, kRightY, 0.0f);
    }

    // Ratón / Pantalla Táctil normalizada a 1280x720
    if (bridge.mouseLeftDown) {
        pad0.touch.active = true;
        pad0.touch.x = bridge.pointerX;
        pad0.touch.y = bridge.pointerY;
        pad0.buttonsHold |= static_cast<uint32_t>(VirtualButton::Touch);

        if ((bridge.mouseLeftPressed || !m_prevMousePressed) && m_touchDebounceTimer <= 0.0f) {
            pad0.touch.justPressed = true;
            m_touchDebounceTimer = 0.16f;
            TriggerAction("TACTIL");
        }
        m_prevMousePressed = true;
    } else {
        m_prevMousePressed = false;
    }

    pad0.buttonsTrigger = (pad0.buttonsHold & ~prevHold0) | kbTrig |
                          (pad0.touch.justPressed ? static_cast<uint32_t>(VirtualButton::Touch) : 0u);
    pad0.buttonsRelease = prevHold0 & ~pad0.buttonsHold;

    bridge.ConsumeFrameEdges();
}

} // namespace WiiU
