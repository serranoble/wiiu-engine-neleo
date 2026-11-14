#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <type_traits>

namespace WiiU {

/**
 * @brief [OPT-219] Vector de capacidad fija alocado contiguamente en un Arena (SceneArena / LinearFrameArena).
 * 
 * Filosofía Data-Oriented Design (DOD):
 * - Memoria continua respaldada por una arena de memoria: CERO llamadas a malloc/free en el heap.
 * - Alineación estricta en silicio (configurable hasta 256 bytes para hardware GX2).
 * - Semántica de vector para inserción y acceso (push_back, operator[], data, size, empty, clear).
 * - Erradica por completo la fragmentación y la necesidad de invocar shrink_to_fit() en el heap.
 */
template <typename T>
class ArenaVector {
public:
    constexpr ArenaVector() noexcept = default;

    constexpr ArenaVector(T* memory, size_t capacity) noexcept
        : m_data(memory)
        , m_capacity(capacity)
        , m_count(0) {}

    void push_back(const T& item) noexcept {
        if (m_data && m_count < m_capacity) {
            m_data[m_count++] = item;
        }
    }

    template <typename... Args>
    T& emplace_back(Args&&... args) noexcept {
        if (m_data && m_count < m_capacity) {
            T* ptr = &m_data[m_count++];
            new (ptr) T(std::forward<Args>(args)...);
            return *ptr;
        }
        return m_data[0];
    }

    [[nodiscard]] constexpr size_t size() const noexcept { return m_count; }
    [[nodiscard]] constexpr size_t capacity() const noexcept { return m_capacity; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_count == 0; }

    [[nodiscard]] T* data() noexcept { return m_data; }
    [[nodiscard]] const T* data() const noexcept { return m_data; }

    [[nodiscard]] T* begin() noexcept { return m_data; }
    [[nodiscard]] const T* begin() const noexcept { return m_data; }
    [[nodiscard]] T* end() noexcept { return m_data + m_count; }
    [[nodiscard]] const T* end() const noexcept { return m_data + m_count; }

    T& operator[](size_t index) noexcept { return m_data[index]; }
    const T& operator[](size_t index) const noexcept { return m_data[index]; }

    void clear() noexcept { m_count = 0; }

private:
    T* m_data{nullptr};
    size_t m_capacity{0};
    size_t m_count{0};
};

} // namespace WiiU
