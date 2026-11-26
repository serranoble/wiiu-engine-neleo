#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace Neleo {

/**
 * @brief [Jason Gregory Cap. 8.1] Tipos de dispositivos de interfaz humana soportados.
 */
enum class ControllerType : uint8_t {
    Disconnected = 0,
    GamePadDRC = 1,          // Nintendo Wii U GamePad (VPAD)
    ProController = 2,       // Wii U Pro Controller (WPAD_EXT_PRO_CONTROLLER)
    WiiRemote = 3,           // Wii Remote estándar (WPAD_EXT_CORE) en modo vertical u horizontal
    WiiRemoteNunchuk = 4,    // Wii Remote + Nunchuk (WPAD_EXT_NUNCHUK)
    ClassicController = 5,   // Mando Clásico / Clásico Pro (WPAD_EXT_CLASSIC)
    KeyboardMouse = 6,       // Teclado y ratón nativos de escritorio (Win32/Desktop)
    NativeGamepad = 7        // Mando nativo de escritorio (XInput / ROG Ally / HID)
};

/**
 * @brief [Jason Gregory Cap. 8.4] Máscara canónica de botones virtuales de motor.
 * Estandariza la disposición física de botones Nintendo (A/B/X/Y, L/R/ZL/ZR, D-Pad, +, -).
 */
enum class VirtualButton : uint32_t {
    None        = 0,
    A           = (1u << 0),
    B           = (1u << 1),
    X           = (1u << 2),
    Y           = (1u << 3),
    L           = (1u << 4),
    R           = (1u << 5),
    ZL          = (1u << 6),
    ZR          = (1u << 7),
    Plus        = (1u << 8),   // Start / Pausa
    Minus       = (1u << 9),   // Select / Telemetría
    Home        = (1u << 10),  // Menú Home de la consola
    DPadUp      = (1u << 11),
    DPadDown    = (1u << 12),
    DPadLeft    = (1u << 13),
    DPadRight   = (1u << 14),
    StickL      = (1u << 15),  // L3 (clic en stick izquierdo)
    StickR      = (1u << 16),  // R3 (clic en stick derecho)
    Touch       = (1u << 17)   // Contacto en pantalla táctil resistiva
};

inline constexpr VirtualButton operator|(VirtualButton a, VirtualButton b) noexcept {
    return static_cast<VirtualButton>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr VirtualButton operator&(VirtualButton a, VirtualButton b) noexcept {
    return static_cast<VirtualButton>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr VirtualButton operator~(VirtualButton a) noexcept {
    return static_cast<VirtualButton>(~static_cast<uint32_t>(a));
}

/**
 * @brief Estado de stick analógico con zona muerta radial procesada.
 */
struct AnalogStick {
    float x{0.0f};          // [-1.0f, 1.0f] (Negativo = Izquierda, Positivo = Derecha)
    float y{0.0f};          // [-1.0f, 1.0f] (Negativo = Abajo, Positivo = Arriba)
    float magnitude{0.0f};  // [0.0f, 1.0f]  (Magnitud euclidiana filtrada)
};

/**
 * @brief Gatillos analógicos normalizados.
 */
struct AnalogTriggers {
    float left{0.0f};   // [0.0f, 1.0f] (Gatillo L analógico de Classic Controller o XInput)
    float right{0.0f};  // [0.0f, 1.0f] (Gatillo R analógico de Classic Controller o XInput)
};

/**
 * @brief Datos de orientación y aceleración de sensores inerciales (IMU).
 */
struct MotionState {
    float accX{0.0f};
    float accY{0.0f};
    float accZ{0.0f};
    float pitch{0.0f};
    float roll{0.0f};
    float yaw{0.0f};
    bool hasMotion{false};
};

/**
 * @brief Estado de la pantalla táctil resistiva del GamePad DRC o ratón PC.
 */
struct TouchState {
    bool active{false};
    bool justPressed{false};
    float x{0.0f}; // Coordenadas virtuales normalizadas a 1280x720
    float y{0.0f};
};

/**
 * @brief [Jason Gregory Cap. 8.2: "Input Processing - Radial Deadzone"]
 * Aplica zona muerta radial continua con remapeo suave de magnitud.
 * Evita la truncación diagonal de la zona muerta axial tradicional,
 * preservando la trayectoria circular y la fluidez analógica de los potenciómetros.
 */
inline AnalogStick ApplyRadialDeadzone(float rawX, float rawY, float deadzone = 0.18f, float maxVal = 1.0f) noexcept {
    float mag = std::sqrt(rawX * rawX + rawY * rawY);
    if (mag <= deadzone) {
        return {0.0f, 0.0f, 0.0f};
    }
    float legalRange = maxVal - deadzone;
    if (legalRange <= 0.0001f) {
        return {0.0f, 0.0f, 0.0f};
    }
    float normalizedMag = std::min(1.0f, (mag - deadzone) / legalRange);
    float scale = normalizedMag / mag;
    return {rawX * scale, rawY * scale, normalizedMag};
}

/**
 * @brief [Jason Gregory Cap. 8.5: "Input Engine Architecture - Gamepad Abstraction Layer"]
 * Representación canónica de un controlador virtual por jugador.
 */
struct VirtualGamepad {
    ControllerType type{ControllerType::Disconnected};
    bool isConnected{false};
    uint8_t batteryLevel{100};  // 0 a 100%
    uint32_t buttonsHold{0};    // Botones actualmente presionados
    uint32_t buttonsTrigger{0}; // Flanco de subida (presionados en este fotograma)
    uint32_t buttonsRelease{0}; // Flanco de bajada (liberados en este fotograma)
    AnalogStick leftStick;
    AnalogStick rightStick;
    AnalogTriggers triggers;
    MotionState motion;
    TouchState touch;

    [[nodiscard]] constexpr bool IsHeld(VirtualButton btn) const noexcept {
        return (buttonsHold & static_cast<uint32_t>(btn)) != 0;
    }

    [[nodiscard]] constexpr bool WasJustPressed(VirtualButton btn) const noexcept {
        return (buttonsTrigger & static_cast<uint32_t>(btn)) != 0;
    }

    [[nodiscard]] constexpr bool WasJustReleased(VirtualButton btn) const noexcept {
        return (buttonsRelease & static_cast<uint32_t>(btn)) != 0;
    }

    [[nodiscard]] constexpr bool IsAnyButtonHeld() const noexcept {
        return buttonsHold != 0;
    }

    [[nodiscard]] constexpr bool IsAnyButtonTriggered() const noexcept {
        return buttonsTrigger != 0;
    }
};

} // namespace Neleo
