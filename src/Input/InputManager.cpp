#include "Input/InputManager.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Core/Config/CVarRegistry.hpp"
#include "Core/Window.hpp"
#include <cmath>
#include <algorithm>

#if defined(__WIIU__) || defined(__wiiu__)
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <proc_ui/procui.h>
#include <coreinit/systeminfo.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

static uint32_t HomeButtonDeniedCallback(void*) {
    // Cuando Cafe OS deniega el Menú HOME (ej. ejecutando desde Homebrew Launcher),
    // presionar el botón HOME debe solicitar la salida limpia de la aplicación.
    Neleo::InputManager::Instance().RequestExit();
    return 0;
}
#endif

namespace Neleo {

void InputManager::Init() {
#if defined(__WIIU__) || defined(__wiiu__)
    VPADInit();
    KPADInit();
    WPADEnableMotor(TRUE);
    WPADEnableURCC(TRUE);

    // Habilitar explícitamente el Menú del Botón HOME de Cafe OS (Aroma / Menú Wii U)
    OSEnableHomeButtonMenu(TRUE);

    // Registrar callback de respaldo si el Menú HOME está deshabilitado/denegado por el entorno
    ProcUIRegisterCallback(PROCUI_CALLBACK_HOME_BUTTON_DENIED, HomeButtonDeniedCallback, nullptr, 100);
#endif
    RumbleManager::Instance().Init();
}

InputManager::~InputManager() = default;

size_t InputManager::GetConnectedGamepadCount() const noexcept {
    size_t count = 0;
    for (size_t i = 0; i < MAX_PLAYERS; ++i) {
        if (m_gamepads[i].isConnected) {
            count++;
        }
    }
    return count > 0 ? count : 1;
}

void InputManager::TriggerAction(std::string_view sourceName) {
    if (m_actionTriggeredThisFrame) return;
    m_actionTriggeredThisFrame = true;

    if (m_actionSoundCallback) {
        m_actionSoundCallback(m_actionSoundUserData);
    }
    m_buttonPressCount++;
    if (sourceName.empty()) {
        m_lastButtonName.Clear();
    } else {
        m_lastButtonName.Format("%.*s", static_cast<int>(sourceName.size()), sourceName.data());
    }
    m_flashTimer = 0.5f;

    // Disparo háptico sutil al interactuar con la interfaz (Jason Gregory Cap. 8.2)
    RumbleManager::Instance().PlayPreset(PlayerIndex::Player1, RumblePreset::SubtleTick);
}

void InputManager::Update(bool& outRunning, float dt) {
    // 1. Bombear eventos de sistema/plataforma (ProcUI directo en Wii U o bucle Win32 en Windows).
    PollPlatformEvents(outRunning);

    if (m_exitRequested.load(std::memory_order_acquire)) {
        outRunning = false;
    }

#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIInShutdown() || !ProcUIIsRunning() || Window::IsExitingProcUI() ||
        !Window::HasForeground() || !ProcUIInForeground()) {
        RumbleManager::Instance().StopAll();
        if (ProcUIInShutdown() || !ProcUIIsRunning() || Window::IsExitingProcUI()) {
            outRunning = false;
        }
        return;
    }
#endif

    m_actionTriggeredThisFrame = false;
    m_controllerDisconnectedThisFrame = false;
    m_contextStack.ResetFrameConsumptions();
    m_buttonHeld = false;
    m_backRequested = false;
    m_pauseRequested = false;
    m_resourceMonitorRequested = false;
    m_buttonsHold = 0;
    m_buttonsTrigger = 0;
    m_touchState = TouchState{};
    m_leftStick = StickState{};
    m_rightStick = StickState{};

    if (m_flashTimer > 0.0f) {
        m_flashTimer -= dt;
        if (m_flashTimer < 0.0f) m_flashTimer = 0.0f;
    }

    if (m_touchDebounceTimer > 0.0f) {
        m_touchDebounceTimer -= dt;
        if (m_touchDebounceTimer < 0.0f) m_touchDebounceTimer = 0.0f;
    }

    // 2. Actualizar subsistema de hápticos y temporizadores de vibración
    RumbleManager::Instance().Update(dt);

    // 3. Limpiar estados por cuadro de los gamepads virtuales
    for (size_t i = 0; i < MAX_PLAYERS; ++i) {
        m_gamepads[i].buttonsTrigger = 0;
        m_gamepads[i].buttonsRelease = 0;
        m_gamepads[i].touch = TouchState{};
    }

    // 4. Muestrear hardware nativo Wii U (VPAD y KPAD) o periféricos nativos de escritorio (Win32 + XInput)
#if defined(__WIIU__) || defined(__wiiu__)
    PollWiiUHardware(dt);
#else
    PollDesktopHardware(outRunning);
#endif

    // 4.5 [OPT-401] Guardia de desconexion HID asincrona e invalidacion limpia
    for (size_t i = 0; i < MAX_PLAYERS; ++i) {
        auto& pad = m_gamepads[i];
        if (m_prevConnected[i] && !pad.isConnected) {
            pad.buttonsHold = 0;
            pad.buttonsTrigger = 0;
            pad.buttonsRelease = 0;
            pad.leftStick = {};
            pad.rightStick = {};
            pad.triggers = {};
            pad.touch = {};
            pad.type = ControllerType::Disconnected;

            m_controllerDisconnectedThisFrame = true;
            m_lastDisconnectedPlayer = i;
            RumbleManager::Instance().StopRumble(static_cast<PlayerIndex>(i));

            const bool cvarAutoPause = CVarRegistry::Instance().GetBool("input.autopause_disconnect", true);
            if (m_autoPauseOnDisconnect && cvarAutoPause) {
                m_pauseRequested = true;
            }
            if (m_disconnectCallback) {
                m_disconnectCallback(i, m_disconnectUserData);
            }
        }
        m_prevConnected[i] = pad.isConnected;
    }

    // 5. Consolidar estado primario en Player 1 y variables heredadas
    const auto& pad0 = m_gamepads[0];
    m_buttonsHold = pad0.buttonsHold;
    m_buttonsTrigger = pad0.buttonsTrigger;
    m_leftStick = {pad0.leftStick.x, pad0.leftStick.y};
    m_rightStick = {pad0.rightStick.x, pad0.rightStick.y};
    m_touchState = pad0.touch;

    for (size_t i = 0; i < MAX_PLAYERS; ++i) {
        const auto& pad = m_gamepads[i];
        if (!pad.isConnected) continue;

        if (pad.IsAnyButtonHeld() || pad.leftStick.magnitude > 0.2f || pad.rightStick.magnitude > 0.2f) {
            m_buttonHeld = true;
        }
        if (pad.WasJustPressed(VirtualButton::B)) {
            m_backRequested = true;
        }
        if (pad.WasJustPressed(VirtualButton::Plus)) {
            m_pauseRequested = true;
        }
        if (pad.WasJustPressed(VirtualButton::Minus)) {
            m_resourceMonitorRequested = true;
        }
#if !defined(__WIIU__) && !defined(__wiiu__)
        if (pad.WasJustPressed(VirtualButton::Home)) {
            m_exitRequested.store(true, std::memory_order_release);
            outRunning = false;
        }
#endif
    }
}

void InputManager::PollWiiUHardware(float dt) {
#if defined(__WIIU__) || defined(__wiiu__)
    (void)dt;
    const float deadzone = EngineConfig::Instance().GetControls().deadzoneRadius;

    // =========================================================================
    // A) NINTENDO WII U GAMAPAD (VPAD) -> ASIGNADO A PLAYER 0
    // =========================================================================
    VPADStatus vpadStatus[8];
    VPADReadError vpadErr = VPAD_READ_UNINITIALIZED;
    int32_t vread = VPADRead(VPAD_CHAN_0, vpadStatus, 8, &vpadErr);
    if (vread > 0 && vpadErr == VPAD_READ_SUCCESS) {
        const auto& latestStatus = vpadStatus[vread - 1];
        auto& pad0 = m_gamepads[0];
        pad0.type = ControllerType::GamePadDRC;
        pad0.isConnected = true;
        pad0.batteryLevel = static_cast<uint8_t>(std::clamp((latestStatus.battery * 100) / 6, 0, 100));

        uint32_t prevHold = pad0.buttonsHold;
        uint32_t newHold = 0;

        if (latestStatus.hold & VPAD_BUTTON_A)        newHold |= static_cast<uint32_t>(VirtualButton::A);
        if (latestStatus.hold & VPAD_BUTTON_B)        newHold |= static_cast<uint32_t>(VirtualButton::B);
        if (latestStatus.hold & VPAD_BUTTON_X)        newHold |= static_cast<uint32_t>(VirtualButton::X);
        if (latestStatus.hold & VPAD_BUTTON_Y)        newHold |= static_cast<uint32_t>(VirtualButton::Y);
        if (latestStatus.hold & VPAD_BUTTON_L)        newHold |= static_cast<uint32_t>(VirtualButton::L);
        if (latestStatus.hold & VPAD_BUTTON_R)        newHold |= static_cast<uint32_t>(VirtualButton::R);
        if (latestStatus.hold & VPAD_BUTTON_ZL)       newHold |= static_cast<uint32_t>(VirtualButton::ZL);
        if (latestStatus.hold & VPAD_BUTTON_ZR)       newHold |= static_cast<uint32_t>(VirtualButton::ZR);
        if (latestStatus.hold & VPAD_BUTTON_PLUS)     newHold |= static_cast<uint32_t>(VirtualButton::Plus);
        if (latestStatus.hold & VPAD_BUTTON_MINUS)    newHold |= static_cast<uint32_t>(VirtualButton::Minus);
        if (latestStatus.hold & VPAD_BUTTON_HOME)     newHold |= static_cast<uint32_t>(VirtualButton::Home);
        if (latestStatus.hold & VPAD_BUTTON_UP)       newHold |= static_cast<uint32_t>(VirtualButton::DPadUp);
        if (latestStatus.hold & VPAD_BUTTON_DOWN)     newHold |= static_cast<uint32_t>(VirtualButton::DPadDown);
        if (latestStatus.hold & VPAD_BUTTON_LEFT)     newHold |= static_cast<uint32_t>(VirtualButton::DPadLeft);
        if (latestStatus.hold & VPAD_BUTTON_RIGHT)    newHold |= static_cast<uint32_t>(VirtualButton::DPadRight);
        if (latestStatus.hold & VPAD_BUTTON_STICK_L)  newHold |= static_cast<uint32_t>(VirtualButton::StickL);
        if (latestStatus.hold & VPAD_BUTTON_STICK_R)  newHold |= static_cast<uint32_t>(VirtualButton::StickR);

        pad0.buttonsHold = newHold;
        pad0.buttonsTrigger = newHold & ~prevHold;
        pad0.buttonsRelease = prevHold & ~newHold;

        // Zona muerta radial continua de Jason Gregory
        pad0.leftStick = ApplyRadialDeadzone(latestStatus.leftStick.x, latestStatus.leftStick.y, deadzone);
        pad0.rightStick = ApplyRadialDeadzone(latestStatus.rightStick.x, latestStatus.rightStick.y, deadzone);

        pad0.triggers.left = (latestStatus.hold & VPAD_BUTTON_ZL) ? 1.0f : 0.0f;
        pad0.triggers.right = (latestStatus.hold & VPAD_BUTTON_ZR) ? 1.0f : 0.0f;

        // Sensores IMU (Acelerómetro y Giroscopio)
        pad0.motion.accX = latestStatus.accelorometer.acc.x;
        pad0.motion.accY = latestStatus.accelorometer.acc.y;
        pad0.motion.accZ = latestStatus.accelorometer.acc.z;
        pad0.motion.pitch = latestStatus.angle.x;
        pad0.motion.roll = latestStatus.angle.y;
        pad0.motion.yaw = latestStatus.angle.z;
        pad0.motion.hasMotion = true;

        // Pantalla táctil calibrada
        VPADTouchData calTouch;
        VPADGetTPCalibratedPointEx(VPAD_CHAN_0, VPAD_TP_1280X720, &calTouch, &latestStatus.tpNormal);
        if (calTouch.touched != 0) {
            pad0.touch.active = true;
            pad0.touch.x = static_cast<float>(calTouch.x);
            pad0.touch.y = static_cast<float>(calTouch.y);
            pad0.buttonsHold |= static_cast<uint32_t>(VirtualButton::Touch);

            if (!m_prevTouched && m_touchDebounceTimer <= 0.0f) {
                pad0.touch.justPressed = true;
                pad0.buttonsTrigger |= static_cast<uint32_t>(VirtualButton::Touch);
                m_touchDebounceTimer = 0.22f;
            }
            m_prevTouched = true;
        } else {
            if (m_prevTouched) {
                pad0.buttonsRelease |= static_cast<uint32_t>(VirtualButton::Touch);
            }
            m_prevTouched = false;
            pad0.touch.active = false;
        }
    } else if (vpadErr == VPAD_READ_INVALID_CONTROLLER) {
        // [AUDITORÍA SILICIO]: Desconexión real de GamePad DRC - Invalidar estado analógico y flags
        if (m_gamepads[0].type == ControllerType::GamePadDRC) {
            m_gamepads[0].isConnected = false;
            m_gamepads[0].type = ControllerType::Disconnected;
            m_gamepads[0].buttonsHold = 0;
            m_gamepads[0].leftStick = {};
            m_gamepads[0].rightStick = {};
            m_gamepads[0].triggers = {};
            m_gamepads[0].touch = {};
        }
    }

    // =========================================================================
    // B) PERIFÉRICOS KPAD (PRO CONTROLLER, WIIMOTE, NUNCHUK, CLÁSICO)
    // =========================================================================
    for (int chan = 0; chan < 4; ++chan) {
        KPADStatus kpadStatus[8];
        KPADError kpadErr = KPAD_ERROR_UNINITIALIZED;
        uint32_t kread = KPADReadEx(static_cast<KPADChan>(chan), kpadStatus, 8, &kpadErr);
        const size_t slot = (m_gamepads[0].isConnected && m_gamepads[0].type == ControllerType::GamePadDRC)
                            ? static_cast<size_t>(chan + 1)
                            : static_cast<size_t>(chan);
        if (slot >= MAX_PLAYERS) {
            continue;
        }

        if (kread > 0 && kpadErr == KPAD_ERROR_OK) {
            const auto& latestKpad = kpadStatus[kread - 1];
            auto& pad = m_gamepads[slot];
            pad.isConnected = true;
            uint32_t prevHold = pad.buttonsHold;
            uint32_t newHold = 0;

            switch (latestKpad.extensionType) {
                // 1. WII U PRO CONTROLLER
                case WPAD_EXT_PRO_CONTROLLER: {
                    pad.type = ControllerType::ProController;
                    if (latestKpad.pro.hold & WPAD_PRO_BUTTON_A)        newHold |= static_cast<uint32_t>(VirtualButton::A);
                    if (latestKpad.pro.hold & WPAD_PRO_BUTTON_B)        newHold |= static_cast<uint32_t>(VirtualButton::B);
                    if (latestKpad.pro.hold & WPAD_PRO_BUTTON_X)        newHold |= static_cast<uint32_t>(VirtualButton::X);
                    if (latestKpad.pro.hold & WPAD_PRO_BUTTON_Y)        newHold |= static_cast<uint32_t>(VirtualButton::Y);
                    if (latestKpad.pro.hold & WPAD_PRO_TRIGGER_L)       newHold |= static_cast<uint32_t>(VirtualButton::L);
                    if (latestKpad.pro.hold & WPAD_PRO_TRIGGER_R)       newHold |= static_cast<uint32_t>(VirtualButton::R);
                    if (latestKpad.pro.hold & WPAD_PRO_TRIGGER_ZL)      newHold |= static_cast<uint32_t>(VirtualButton::ZL);
                    if (latestKpad.pro.hold & WPAD_PRO_TRIGGER_ZR)      newHold |= static_cast<uint32_t>(VirtualButton::ZR);
                    if (latestKpad.pro.hold & WPAD_PRO_BUTTON_PLUS)     newHold |= static_cast<uint32_t>(VirtualButton::Plus);
                    if (latestKpad.pro.hold & WPAD_PRO_BUTTON_MINUS)    newHold |= static_cast<uint32_t>(VirtualButton::Minus);
                    if (latestKpad.pro.hold & WPAD_PRO_BUTTON_HOME)     newHold |= static_cast<uint32_t>(VirtualButton::Home);
                    if (latestKpad.pro.hold & WPAD_PRO_BUTTON_UP)       newHold |= static_cast<uint32_t>(VirtualButton::DPadUp);
                    if (latestKpad.pro.hold & WPAD_PRO_BUTTON_DOWN)     newHold |= static_cast<uint32_t>(VirtualButton::DPadDown);
                    if (latestKpad.pro.hold & WPAD_PRO_BUTTON_LEFT)     newHold |= static_cast<uint32_t>(VirtualButton::DPadLeft);
                    if (latestKpad.pro.hold & WPAD_PRO_BUTTON_RIGHT)    newHold |= static_cast<uint32_t>(VirtualButton::DPadRight);
                    if (latestKpad.pro.hold & WPAD_PRO_BUTTON_STICK_L)  newHold |= static_cast<uint32_t>(VirtualButton::StickL);
                    if (latestKpad.pro.hold & WPAD_PRO_BUTTON_STICK_R)  newHold |= static_cast<uint32_t>(VirtualButton::StickR);

                    pad.leftStick = ApplyRadialDeadzone(latestKpad.pro.leftStick.x, latestKpad.pro.leftStick.y, deadzone);
                    pad.rightStick = ApplyRadialDeadzone(latestKpad.pro.rightStick.x, latestKpad.pro.rightStick.y, deadzone);
                    pad.triggers.left = (latestKpad.pro.hold & WPAD_PRO_TRIGGER_ZL) ? 1.0f : 0.0f;
                    pad.triggers.right = (latestKpad.pro.hold & WPAD_PRO_TRIGGER_ZR) ? 1.0f : 0.0f;
                    break;
                }

                // 2. WII REMOTE + NUNCHUK (ESTÁNDAR O MOTION PLUS)
                case WPAD_EXT_NUNCHUK:
                case WPAD_EXT_MPLUS_NUNCHUK: {
                    pad.type = ControllerType::WiiRemoteNunchuk;
                    // Stick del Nunchuk con zona muerta radial
                    pad.leftStick = ApplyRadialDeadzone(latestKpad.nunchuk.stick.x, latestKpad.nunchuk.stick.y, deadzone);
                    if (latestKpad.nunchuk.hold & WPAD_NUNCHUK_BUTTON_Z) newHold |= static_cast<uint32_t>(VirtualButton::ZL);
                    if (latestKpad.nunchuk.hold & WPAD_NUNCHUK_BUTTON_C) newHold |= static_cast<uint32_t>(VirtualButton::L);

                    // Botones en el Wiimote (Mano derecha)
                    if (latestKpad.hold & WPAD_BUTTON_A)        newHold |= static_cast<uint32_t>(VirtualButton::A);
                    if (latestKpad.hold & WPAD_BUTTON_B)        newHold |= static_cast<uint32_t>(VirtualButton::B);
                    if (latestKpad.hold & WPAD_BUTTON_1)        newHold |= static_cast<uint32_t>(VirtualButton::X);
                    if (latestKpad.hold & WPAD_BUTTON_2)        newHold |= static_cast<uint32_t>(VirtualButton::Y);
                    if (latestKpad.hold & WPAD_BUTTON_PLUS)     newHold |= static_cast<uint32_t>(VirtualButton::Plus);
                    if (latestKpad.hold & WPAD_BUTTON_MINUS)    newHold |= static_cast<uint32_t>(VirtualButton::Minus);
                    if (latestKpad.hold & WPAD_BUTTON_HOME)     newHold |= static_cast<uint32_t>(VirtualButton::Home);
                    if (latestKpad.hold & WPAD_BUTTON_UP)       newHold |= static_cast<uint32_t>(VirtualButton::DPadUp);
                    if (latestKpad.hold & WPAD_BUTTON_DOWN)     newHold |= static_cast<uint32_t>(VirtualButton::DPadDown);
                    if (latestKpad.hold & WPAD_BUTTON_LEFT)     newHold |= static_cast<uint32_t>(VirtualButton::DPadLeft);
                    if (latestKpad.hold & WPAD_BUTTON_RIGHT)    newHold |= static_cast<uint32_t>(VirtualButton::DPadRight);
                    break;
                }

                // 3. CLASSIC CONTROLLER / CLASSIC CONTROLLER PRO (ESTÁNDAR O MOTION PLUS)
                case WPAD_EXT_CLASSIC:
                case WPAD_EXT_MPLUS_CLASSIC: {
                    pad.type = ControllerType::ClassicController;
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_A)     newHold |= static_cast<uint32_t>(VirtualButton::A);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_B)     newHold |= static_cast<uint32_t>(VirtualButton::B);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_X)     newHold |= static_cast<uint32_t>(VirtualButton::X);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_Y)     newHold |= static_cast<uint32_t>(VirtualButton::Y);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_L)     newHold |= static_cast<uint32_t>(VirtualButton::L);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_R)     newHold |= static_cast<uint32_t>(VirtualButton::R);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_ZL)    newHold |= static_cast<uint32_t>(VirtualButton::ZL);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_ZR)    newHold |= static_cast<uint32_t>(VirtualButton::ZR);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_PLUS)  newHold |= static_cast<uint32_t>(VirtualButton::Plus);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_MINUS) newHold |= static_cast<uint32_t>(VirtualButton::Minus);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_HOME)  newHold |= static_cast<uint32_t>(VirtualButton::Home);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_UP)    newHold |= static_cast<uint32_t>(VirtualButton::DPadUp);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_DOWN)  newHold |= static_cast<uint32_t>(VirtualButton::DPadDown);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_LEFT)  newHold |= static_cast<uint32_t>(VirtualButton::DPadLeft);
                    if (latestKpad.classic.hold & WPAD_CLASSIC_BUTTON_RIGHT) newHold |= static_cast<uint32_t>(VirtualButton::DPadRight);

                    pad.leftStick = ApplyRadialDeadzone(latestKpad.classic.leftStick.x, latestKpad.classic.leftStick.y, deadzone);
                    pad.rightStick = ApplyRadialDeadzone(latestKpad.classic.rightStick.x, latestKpad.classic.rightStick.y, deadzone);
                    pad.triggers.left = latestKpad.classic.leftTrigger;
                    pad.triggers.right = latestKpad.classic.rightTrigger;
                    break;
                }

                // 4. WII REMOTE INDEPENDIENTE (MODO HORIZONTAL ERGONÓMICO O MOTION PLUS)
                case WPAD_EXT_MPLUS:
                case WPAD_EXT_CORE:
                default: {
                    pad.type = ControllerType::WiiRemote;
                    // En modo horizontal: Botón 2 es Acción/Salto (A), Botón 1 es Secundario (B)
                    if (latestKpad.hold & WPAD_BUTTON_2)     newHold |= static_cast<uint32_t>(VirtualButton::A);
                    if (latestKpad.hold & WPAD_BUTTON_1)     newHold |= static_cast<uint32_t>(VirtualButton::B);
                    if (latestKpad.hold & WPAD_BUTTON_A)     newHold |= static_cast<uint32_t>(VirtualButton::X);
                    if (latestKpad.hold & WPAD_BUTTON_B)     newHold |= static_cast<uint32_t>(VirtualButton::Y);
                    if (latestKpad.hold & WPAD_BUTTON_PLUS)  newHold |= static_cast<uint32_t>(VirtualButton::Plus);
                    if (latestKpad.hold & WPAD_BUTTON_MINUS) newHold |= static_cast<uint32_t>(VirtualButton::Minus);
                    if (latestKpad.hold & WPAD_BUTTON_HOME)  newHold |= static_cast<uint32_t>(VirtualButton::Home);

                    // Orientación D-Pad horizontal: Arriba es Izquierda, Abajo es Derecha, etc.
                    float sx = 0.0f, sy = 0.0f;
                    if (latestKpad.hold & WPAD_BUTTON_RIGHT) { newHold |= static_cast<uint32_t>(VirtualButton::DPadUp);    sy += 1.0f; }
                    if (latestKpad.hold & WPAD_BUTTON_LEFT)  { newHold |= static_cast<uint32_t>(VirtualButton::DPadDown);  sy -= 1.0f; }
                    if (latestKpad.hold & WPAD_BUTTON_UP)    { newHold |= static_cast<uint32_t>(VirtualButton::DPadLeft);  sx -= 1.0f; }
                    if (latestKpad.hold & WPAD_BUTTON_DOWN)  { newHold |= static_cast<uint32_t>(VirtualButton::DPadRight); sx += 1.0f; }

                    pad.leftStick = ApplyRadialDeadzone(sx, sy, 0.0f);
                    break;
                }
            }

            pad.buttonsHold = newHold;
            pad.buttonsTrigger = newHold & ~prevHold;
            pad.buttonsRelease = prevHold & ~newHold;
        } else if (kpadErr == KPAD_ERROR_INVALID_CONTROLLER) {
            // [AUDITORÍA SILICIO]: Desconexión real de WPAD - Limpieza determinista
            if (m_gamepads[slot].type != ControllerType::GamePadDRC) {
                m_gamepads[slot].isConnected = false;
                m_gamepads[slot].buttonsHold = 0;
                m_gamepads[slot].leftStick = {};
                m_gamepads[slot].rightStick = {};
                m_gamepads[slot].triggers = {};
            }
        }
    }
#endif
}

void InputManager::PollPlatformEvents(bool& outRunning) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!ProcUIIsRunning() || Window::IsExitingProcUI()) {
        m_exitRequested.store(true, std::memory_order_release);
        outRunning = false;
        return;
    }

    ProcUIStatus status = ProcUIProcessMessages(TRUE);
    while (status == PROCUI_STATUS_RELEASE_FOREGROUND || status == PROCUI_STATUS_IN_BACKGROUND) {
        if (status == PROCUI_STATUS_RELEASE_FOREGROUND) {
            RumbleManager::Instance().StopAll();
            Window::ReleaseForeground();
            ProcUIDrawDoneRelease();
        } else {
            OSSleepTicks(OSMillisecondsToTicks(16));
        }

        if (!ProcUIIsRunning()) {
            Window::NotifyExitingProcUI();
            m_exitRequested.store(true, std::memory_order_release);
            outRunning = false;
            return;
        }
        // Siguiente llamada obligatoria a ProcUIProcessMessages(TRUE) para completar transición HOME Menu
        status = ProcUIProcessMessages(TRUE);
    }

    if (status == PROCUI_STATUS_EXITING) {
        Window::NotifyExitingProcUI();
        m_exitRequested.store(true, std::memory_order_release);
        outRunning = false;
        return;
    }

    if (status == PROCUI_STATUS_IN_FOREGROUND && !Window::HasForeground()) {
        Window::AcquireForeground();
    }
#else
    (void)outRunning;
    Window::PumpPlatformEvents();
#endif
}

#if defined(__WIIU__) || defined(__wiiu__)
void InputManager::PollDesktopHardware(bool& outRunning) {
    (void)outRunning;
}
#endif

} // namespace Neleo
