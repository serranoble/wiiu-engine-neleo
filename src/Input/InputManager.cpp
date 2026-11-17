#include "Input/InputManager.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Audio/AudioManager.hpp"
#include <cmath>
#include <algorithm>

#if defined(__WIIU__) || defined(__wiiu__)
#include <proc_ui/procui.h>
#include <coreinit/systeminfo.h>

static uint32_t HomeButtonDeniedCallback(void*) {
    // Cuando Cafe OS deniega el Menú HOME (ej. ejecutando desde Homebrew Launcher),
    // presionar el botón HOME debe solicitar la salida limpia de la aplicación.
    WiiU::InputManager::Instance().RequestExit();
    return 0;
}
#endif

namespace WiiU {

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

    // Deshabilitar eventos de joystick en SDL2 para garantizar que WIIU_JoystickUpdate
    // no drene el búfer de VPADRead ni anule la pantalla táctil del GamePad
    SDL_JoystickEventState(SDL_IGNORE);
#endif
    RumbleManager::Instance().Init();
#if !defined(__WIIU__) && !defined(__wiiu__)
    OpenConnectedControllers();
#endif
}

void InputManager::OpenConnectedControllers() {
#if !defined(__WIIU__) && !defined(__wiiu__)
    int numJoysticks = SDL_NumJoysticks();
    for (int i = 0; i < numJoysticks; ++i) {
        if (SDL_IsGameController(i)) {
            SDL_GameControllerOpen(i);
        } else {
            SDL_JoystickOpen(i);
        }
    }
#endif
}

size_t InputManager::GetConnectedGamepadCount() const noexcept {
    size_t count = 0;
    for (size_t i = 0; i < MAX_PLAYERS; ++i) {
        if (m_gamepads[i].isConnected) {
            count++;
        }
    }
    return count > 0 ? count : 1;
}

void InputManager::TriggerAction(const std::string& sourceName) {
    if (m_actionTriggeredThisFrame) return;
    m_actionTriggeredThisFrame = true;

    AudioManager::Instance().PlayClick();
    m_buttonPressCount++;
    m_lastButtonName = sourceName;
    m_flashTimer = 0.5f;

    // Disparo háptico sutil al interactuar con la interfaz (Jason Gregory Cap. 8.2)
    RumbleManager::Instance().PlayPreset(PlayerIndex::Player1, RumblePreset::SubtleTick);
}

void InputManager::Update(bool& outRunning, float dt) {
    // 1. Bombear eventos SDL en todas las plataformas.
    // En Wii U, SDL_PollEvent invoca internamente WIIU_PumpEvents(), el cual llama a
    // ProcUIProcessMessages() para gestionar transiciones de Cafe OS (Menú HOME y salida).
    PollSDLEvents(outRunning);

    if (m_exitRequested) {
        outRunning = false;
    }

#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIInShutdown() || !ProcUIIsRunning() || !ProcUIInForeground()) {
        RumbleManager::Instance().StopAll();
        if (ProcUIInShutdown() || !ProcUIIsRunning()) {
            outRunning = false;
        }
        return;
    }
#endif

    m_actionTriggeredThisFrame = false;
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

    // 4. Muestrear hardware nativo Wii U (VPAD y KPAD) o periféricos PC (SDL2)
#if defined(__WIIU__) || defined(__wiiu__)
    PollWiiUHardware(dt);
#else
    PollSDLHardware(outRunning);
#endif

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
        if (pad.WasJustPressed(VirtualButton::Home)) {
            m_exitRequested = true;
            outRunning = false;
        }
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
        size_t slot = (m_gamepads[0].isConnected && m_gamepads[0].type == ControllerType::GamePadDRC)
                      ? std::min<size_t>(chan + 1, MAX_PLAYERS - 1)
                      : static_cast<size_t>(chan);

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

void InputManager::PollSDLEvents(bool& outRunning) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            m_exitRequested = true;
            outRunning = false;
        }
#if !defined(__WIIU__) && !defined(__wiiu__)
        if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_JOYDEVICEADDED) {
            OpenConnectedControllers();
        }
        auto& pad0 = m_gamepads[0];
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
            SDL_Keycode k = event.key.keysym.sym;
            if (k == SDLK_p || k == SDLK_PAUSE || k == SDLK_ESCAPE) {
                m_pauseRequested = true;
                pad0.buttonsTrigger |= static_cast<uint32_t>(VirtualButton::Plus);
            } else if (k == SDLK_MINUS || k == SDLK_KP_MINUS || k == SDLK_TAB) {
                m_resourceMonitorRequested = true;
                pad0.buttonsTrigger |= static_cast<uint32_t>(VirtualButton::Minus);
            } else if (k == SDLK_b || k == SDLK_BACKSPACE) {
                m_backRequested = true;
                pad0.buttonsTrigger |= static_cast<uint32_t>(VirtualButton::B);
            } else if (k == SDLK_RETURN || k == SDLK_SPACE) {
                pad0.buttonsTrigger |= static_cast<uint32_t>(VirtualButton::A);
            } else if (k == SDLK_HOME) {
                m_exitRequested = true;
                outRunning = false;
                pad0.buttonsTrigger |= static_cast<uint32_t>(VirtualButton::Home);
            }
            if (k != SDLK_UP && k != SDLK_DOWN && k != SDLK_LEFT && k != SDLK_RIGHT &&
                k != SDLK_i && k != SDLK_j && k != SDLK_k && k != SDLK_l) {
                TriggerAction("TECLADO");
            }
        }
        if (event.type == SDL_CONTROLLERBUTTONDOWN) {
            TriggerAction("MANDO BOTON");
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                m_backRequested = true;
                pad0.buttonsTrigger |= static_cast<uint32_t>(VirtualButton::B);
            }
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                pad0.buttonsTrigger |= static_cast<uint32_t>(VirtualButton::A);
            }
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                m_pauseRequested = true;
                pad0.buttonsTrigger |= static_cast<uint32_t>(VirtualButton::Plus);
            }
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                m_resourceMonitorRequested = true;
                pad0.buttonsTrigger |= static_cast<uint32_t>(VirtualButton::Minus);
            }
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
                m_exitRequested = true;
                outRunning = false;
                pad0.buttonsTrigger |= static_cast<uint32_t>(VirtualButton::Home);
            }
        }
        if (event.type == SDL_JOYBUTTONDOWN) {
            TriggerAction("JOYSTICK BOTON");
        }
        if (event.type == SDL_JOYHATMOTION && event.jhat.value != SDL_HAT_CENTERED) {
            TriggerAction("CRUZETA DPAD");
        }
        if (event.type == SDL_JOYAXISMOTION) {
            const float deadzone = EngineConfig::Instance().GetControls().deadzoneRadius;
            float val = static_cast<float>(event.jaxis.value) / 32767.0f;
            float lx = pad0.leftStick.x, ly = pad0.leftStick.y;
            float rx = pad0.rightStick.x, ry = pad0.rightStick.y;
            if (event.jaxis.axis == SDL_CONTROLLER_AXIS_LEFTX || event.jaxis.axis == 0)  lx = val;
            if (event.jaxis.axis == SDL_CONTROLLER_AXIS_LEFTY || event.jaxis.axis == 1)  ly = -val;
            if (event.jaxis.axis == SDL_CONTROLLER_AXIS_RIGHTX || event.jaxis.axis == 2) rx = val;
            if (event.jaxis.axis == SDL_CONTROLLER_AXIS_RIGHTY || event.jaxis.axis == 3) ry = -val;

            pad0.leftStick = ApplyRadialDeadzone(lx, ly, deadzone);
            pad0.rightStick = ApplyRadialDeadzone(rx, ry, deadzone);
        }
#endif
    }
}

void InputManager::PollSDLHardware(bool& outRunning) {
    (void)outRunning;
#if !defined(__WIIU__) && !defined(__wiiu__)
    auto& pad0 = m_gamepads[0];

    // =========================================================================
    // RATÓN PC (SDL2)
    // =========================================================================
    int mouseX = 0, mouseY = 0;
    Uint32 mouseButtons = SDL_GetMouseState(&mouseX, &mouseY);
    if (mouseButtons & SDL_BUTTON(SDL_BUTTON_LEFT)) {
        pad0.touch.active = true;
        pad0.touch.x = static_cast<float>(mouseX);
        pad0.touch.y = static_cast<float>(mouseY);
        pad0.buttonsHold |= static_cast<uint32_t>(VirtualButton::Touch);

        if (!m_prevMousePressed && !pad0.touch.justPressed && m_touchDebounceTimer <= 0.0f) {
            pad0.touch.justPressed = true;
            pad0.buttonsTrigger |= static_cast<uint32_t>(VirtualButton::Touch);
            m_touchDebounceTimer = 0.22f;
            TriggerAction("RATON TOUCH");
        }
        m_prevMousePressed = true;
    } else {
        if (m_prevMousePressed) {
            pad0.buttonsRelease |= static_cast<uint32_t>(VirtualButton::Touch);
        }
        m_prevMousePressed = false;
    }

    // =========================================================================
    // TECLADO PC (SDL2): WASD, Flechas, Q/E, ESPACIO, ENTER
    // =========================================================================
    const Uint8* keyState = SDL_GetKeyboardState(NULL);
    if (keyState) {
        float kLeftX = 0.0f, kLeftY = 0.0f;
        if (keyState[SDL_SCANCODE_D]) kLeftX += 1.0f;
        if (keyState[SDL_SCANCODE_A]) kLeftX -= 1.0f;
        if (keyState[SDL_SCANCODE_W]) kLeftY += 1.0f;
        if (keyState[SDL_SCANCODE_S]) kLeftY -= 1.0f;
        pad0.leftStick = ApplyRadialDeadzone(kLeftX, kLeftY, 0.0f);

        float kRightX = 0.0f, kRightY = 0.0f;
        if (keyState[SDL_SCANCODE_RIGHT] || keyState[SDL_SCANCODE_L]) kRightX += 1.0f;
        if (keyState[SDL_SCANCODE_LEFT]  || keyState[SDL_SCANCODE_J]) kRightX -= 1.0f;
        if (keyState[SDL_SCANCODE_UP]    || keyState[SDL_SCANCODE_I]) kRightY += 1.0f;
        if (keyState[SDL_SCANCODE_DOWN]  || keyState[SDL_SCANCODE_K]) kRightY -= 1.0f;
        pad0.rightStick = ApplyRadialDeadzone(kRightX, kRightY, 0.0f);

        if (keyState[SDL_SCANCODE_Q] || keyState[SDL_SCANCODE_Z]) {
            pad0.buttonsHold |= static_cast<uint32_t>(VirtualButton::ZL);
        } else {
            pad0.buttonsHold &= ~static_cast<uint32_t>(VirtualButton::ZL);
        }
        if (keyState[SDL_SCANCODE_E] || keyState[SDL_SCANCODE_X]) {
            pad0.buttonsHold |= static_cast<uint32_t>(VirtualButton::ZR);
        } else {
            pad0.buttonsHold &= ~static_cast<uint32_t>(VirtualButton::ZR);
        }
        if (keyState[SDL_SCANCODE_R]) pad0.buttonsTrigger |= static_cast<uint32_t>(VirtualButton::StickR);
    }
#endif
}

} // namespace WiiU
