#pragma once

#include <cstddef>
#include <cstdint>

namespace WiiU {

/**
 * @brief Modos de entrada soportados por el teclado virtual del sistema.
 */
enum class KeyboardInputMode : uint8_t {
    FullText = 0, // Teclado completo (QWERTY)
    IPv4 = 1      // Teclado numérico con punto '.' (ideal para direcciones IP/DNS)
};

/**
 * @brief [Capa HAL Multi-Consola] Teclado Virtual Nativo del Sistema Operativo.
 *
 * En Nintendo Wii U (y Cemu) invoca el teclado oficial Cafe OS `nn::swkbd` renderizando
 * sobre el swapchain GX2 nativo de `Window` con guardas de ProcUI/Menú HOME.
 * En PC mantiene un retorno seguro no bloqueante.
 */
class SoftwareKeyboard {
public:
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

} // namespace WiiU
