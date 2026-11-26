#pragma once

#include <cstddef>
#include <cstdint>

namespace Neleo {

/**
 * @brief Modos de entrada soportados por el teclado virtual del sistema.
 */
enum class KeyboardInputMode : uint8_t {
    FullText = 0, // Teclado completo (QWERTY)
    IPv4 = 1      // Teclado numérico con punto '.' (ideal para direcciones IP/DNS)
};

/**
 * @brief [Capa HAL Multi-Consola] Teclado Virtual Modal Multiplataforma.
 *
 * - En Nintendo Wii U (y Cemu) invoca el teclado oficial Cafe OS `nn::swkbd` sobre GX2.
 * - En Windows x64 / ASUS ROG Ally abre un teclado virtual modal in-engine Zero-Alloc
 *   (compatible con teclado físico, mando XInput, ratón y pantalla táctil).
 */
class SoftwareKeyboard {
public:
    /**
     * @brief Valida si un carácter ASCII es aceptado bajo el modo de entrada indicado.
     */
    [[nodiscard]] static constexpr bool IsCharAllowed(char ch, KeyboardInputMode mode) noexcept {
        if (mode == KeyboardInputMode::IPv4) {
            return (ch >= '0' && ch <= '9') || (ch == '.');
        }
        return (ch >= 0x20 && ch <= 0x7E);
    }

    /**
     * @brief Abre el teclado virtual modal del sistema.
     * @param hintText Texto de ayuda / título en UTF-8/ASCII (puede ser nullptr).
     * @param initialText Texto inicial precargado en UTF-8/ASCII (puede ser nullptr).
     * @param outText Búfer de salida ASCII/UTF-8 (terminado en '\0').
     * @param outCapacity Capacidad total en bytes de outText (incluyendo '\0').
     * @param mode Modo de teclado (FullText o IPv4 Numpad).
     * @param maxLength Longitud máxima de caracteres permitidos (por defecto 32).
     * @return true si el usuario confirmó con OK y se escribió texto válido; false si canceló o salió por HOME.
     */
    static bool Show(const char* hintText,
                     const char* initialText,
                     char* outText,
                     size_t outCapacity,
                     KeyboardInputMode mode = KeyboardInputMode::FullText,
                     int32_t maxLength = 32) noexcept;
};

} // namespace Neleo
