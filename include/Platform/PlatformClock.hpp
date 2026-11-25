#pragma once

#include <cstdint>

namespace WiiU::PlatformClock {

/**
 * @brief Retorna la frecuencia del contador de alto rendimiento de la plataforma (ticks por segundo).
 */
[[nodiscard]] uint64_t GetPerformanceFrequency() noexcept;

/**
 * @brief Retorna el valor actual del contador monotónico de alta precisión de la plataforma.
 */
[[nodiscard]] uint64_t GetPerformanceCounter() noexcept;

/**
 * @brief Cede la ejecución del hilo actual de forma pasiva durante el número de milisegundos indicado.
 */
void SleepMs(uint32_t milliseconds) noexcept;

} // namespace WiiU::PlatformClock
