#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <string_view>

namespace Neleo {

/**
 * @brief Cadena de caracteres en memoria de pila (Stack-Allocated String) de tamaño fijo.
 * 
 * Cumple con la regla de cero asignaciones dinámicas (Zero Hot Allocations) en el hot path.
 * Implementa [OPT-56] para formateo seguro y eficiente de textos y telemetría de HUD.
 */
template <size_t Capacity = 256>
class StackString {
    static_assert(Capacity > 0, "StackString capacity must be greater than zero");

public:
    constexpr StackString() noexcept {
        m_buffer[0] = '\0';
    }

    StackString(const char* str) noexcept {
        Set(str);
    }

    void Clear() noexcept {
        m_buffer[0] = '\0';
        m_length = 0;
    }

#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    int Format(const char* fmt, ...) noexcept {
        if (!fmt) {
            Clear();
            return 0;
        }

        va_list args;
        va_start(args, fmt);
        int written = std::vsnprintf(m_buffer, Capacity, fmt, args);
        va_end(args);

        if (written < 0) {
            m_buffer[0] = '\0';
            m_length = 0;
            return 0;
        }

        m_length = (static_cast<size_t>(written) >= Capacity) ? (Capacity - 1) : static_cast<size_t>(written);
        m_buffer[m_length] = '\0';
        return static_cast<int>(m_length);
    }

    void Set(const char* str) noexcept {
        if (!str) {
            Clear();
            return;
        }
        size_t i = 0;
        while (i + 1 < Capacity && str[i] != '\0') {
            m_buffer[i] = str[i];
            ++i;
        }
        m_buffer[i] = '\0';
        m_length = i;
    }

    [[nodiscard]] const char* c_str() const noexcept { return m_buffer; }
    [[nodiscard]] const char* data() const noexcept { return m_buffer; }
    [[nodiscard]] size_t size() const noexcept { return m_length; }
    [[nodiscard]] size_t length() const noexcept { return m_length; }
    [[nodiscard]] bool empty() const noexcept { return m_length == 0; }
    [[nodiscard]] constexpr size_t capacity() const noexcept { return Capacity; }

    [[nodiscard]] std::string_view string_view() const noexcept {
        return std::string_view(m_buffer, m_length);
    }

private:
    char m_buffer[Capacity]{};
    size_t m_length{0};
};

} // namespace Neleo
