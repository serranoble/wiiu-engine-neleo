#pragma once

#include "Graphics/Math3D.hpp"
#include "Graphics/Color.hpp"
#include <cstdint>
#include <cmath>

namespace WiiU {

/**
 * @brief Generador de Números Pseudoaleatorios Determinista de Alto Rendimiento (PRNG).
 * 
 * Implementación canónica para motores de videojuegos basada en el Capítulo 4.6 de Jason Gregory
 * ("Game Engine Architecture", 3ª Edición, "Pseudo-Random Number Generation").
 * 
 * Características Arquitectónicas:
 * - Algoritmo Núcleo: PCG32 (Permuted Congruential Generator - XSH-RR 64/32) de Melissa O'Neill (2014).
 * - Período de $2^{64}$ estados independientes con soporte para $2^{63}$ secuencias/corrientes distintas.
 * - Determinismo Bit a Bit Estricto: Produce secuencias reproducibles idénticas en cualquier plataforma
 *   (Nintendo Wii U, Switch, PC), garantizando repeticiones de físicas, partículas y netcode replay.
 * - Desacoplamiento Absoluto (Cero Estado Global): Cada emisor o subsistema posee su propia instancia,
 *   erradicando la contaminación cruzada provocada por funciones estándar globales (`rand()`).
 * - Generación de Enteros Libre de Sesgo: Implementa el algoritmo de Daniel Lemire (2019) sin divisiones lentas.
 * - Distribuciones Geométricas 2D y 3D Analíticas: Superficie y volumen esférico sin distorsión polar,
 *   disco uniforme, dirección en cono tridimensional y distribución Gaussiana de Box-Muller.
 * - Cero alocaciones dinámicas (Hot-Path Invariant: 0 bytes heap a 60 FPS).
 */
class PRNG {
public:
    /**
     * @brief Constructor por defecto con semilla canónica de alta entropía.
     */
    constexpr PRNG() noexcept
        : m_state(0x853c49e6748fea9bULL), m_inc(0xda3e39cb94b95bdbULL), m_callCount(0) {}

    /**
     * @brief Constructor explícito parametrizado por semilla e identificador de secuencia.
     * @param seed Semilla inicial de 64 bits.
     * @param sequence Identificador de secuencia/corriente independiente (stream).
     */
    explicit PRNG(uint64_t seed, uint64_t sequence = 0xda3e39cb94b95bdbULL) noexcept {
        SetSeed(seed, sequence);
    }

    /**
     * @brief Reinicializa el generador con una semilla y secuencia determinadas.
     */
    void SetSeed(uint64_t seed, uint64_t sequence = 0xda3e39cb94b95bdbULL) noexcept;

    // =========================================================================
    // Generación de Enteros (Escalares y Rangos)
    // =========================================================================

    /**
     * @brief Retorna un entero uniforme sin signo de 32 bits en $[0, 2^{32}-1]$.
     */
    [[nodiscard]] uint32_t NextUInt32() noexcept;

    /**
     * @brief Retorna un entero uniforme de 32 bits en $[0, \text{maxExclusive})$ sin sesgo de módulo.
     * Algoritmo de Daniel Lemire (2019) casi libre de división.
     */
    [[nodiscard]] uint32_t NextUInt32(uint32_t maxExclusive) noexcept;

    /**
     * @brief Retorna un entero con signo en el rango cerrado $[minInclusive, maxInclusive]$.
     */
    [[nodiscard]] int32_t NextIntRange(int32_t minInclusive, int32_t maxInclusive) noexcept;

    /**
     * @brief Retorna un entero de 64 bits combinando dos generaciones de 32 bits.
     */
    [[nodiscard]] uint64_t NextUInt64() noexcept;

    // =========================================================================
    // Generación de Coma Flotante (Escalares y Estadísticas)
    // =========================================================================

    /**
     * @brief Retorna un número flotante uniforme en el intervalo semiabierto $[0.0f, 1.0f)$.
     * Mapeo de precisión simple de 24 bits de mantisa.
     */
    [[nodiscard]] float NextFloat01() noexcept;

    /**
     * @brief Retorna un número flotante uniforme en $[min, max)$.
     */
    [[nodiscard]] float NextFloatRange(float min, float max) noexcept;

    /**
     * @brief Retorna un número flotante uniforme en el intervalo cerrado $[-1.0f, 1.0f]$.
     */
    [[nodiscard]] float NextFloatSigned() noexcept;

    /**
     * @brief Retorna un valor aleatorio centrado en `base` con dispersión simétrica $[base - variance, base + variance]$.
     */
    [[nodiscard]] float NextFloatVariance(float base, float variance) noexcept;

    /**
     * @brief Genera un valor con distribución Normal / Gaussiana $N(\mu, \sigma)$ mediante Box-Muller.
     * @param mean Media aritmética $\mu$ de la campana.
     * @param stdDev Desviación estándar $\sigma$.
     */
    [[nodiscard]] float NextGaussian(float mean = 0.0f, float stdDev = 1.0f) noexcept;

    // =========================================================================
    // Distribuciones Geométricas 2D y 3D (Jason Gregory Cap. 4.6 & Partículas)
    // =========================================================================

    /**
     * @brief Retorna un punto uniformemente distribuido sobre el perímetro de un círculo 2D.
     */
    [[nodiscard]] Point2D NextPointOnCircle(float radius = 1.0f) noexcept;

    /**
     * @brief Retorna un punto uniformemente distribuido dentro del área de un disco 2D.
     * Aplica compensación por raíz cuadrada $r = R\sqrt{u}$ para evitar acumulación en el origen.
     */
    [[nodiscard]] Point2D NextPointInCircle(float radius = 1.0f) noexcept;

    /**
     * @brief Retorna un punto uniformemente distribuido sobre la superficie esférica 3D ($S^2$).
     * Teorema de Arquímedes sin distorsión polar en los vértices zenit/nadir.
     */
    [[nodiscard]] Point3D NextPointOnSphere(float radius = 1.0f) noexcept;

    /**
     * @brief Retorna un punto uniformemente distribuido dentro del volumen esférico 3D ($B^3$).
     * Aplica corrección de volumen cúbico $r = R \cdot u^{1/3}$.
     */
    [[nodiscard]] Point3D NextPointInSphere(float radius = 1.0f) noexcept;

    /**
     * @brief Genera un vector director unitario uniformemente distribuido dentro de un cono 3D.
     * Arquetipo canónico de emisión angular para sistemas de partículas, antorchas y fuentes.
     * @param coneAxis Eje director normalizado del cono.
     * @param maxHalfAngleRad Ángulo de semi-apertura en radianes $[0, \pi]$.
     */
    [[nodiscard]] Point3D NextDirectionInCone(const Point3D& coneAxis, float maxHalfAngleRad) noexcept;

    /**
     * @brief Retorna un punto aleatorio dentro de una caja de colisión AABB 3D.
     */
    [[nodiscard]] Point3D NextPointInAABB(const Point3D& minCorner, const Point3D& maxCorner) noexcept;

    /**
     * @brief Genera un color variando los canales RGB respecto a un color base.
     */
    [[nodiscard]] ColorRGBA NextColor(const ColorRGBA& base, float variance) noexcept;

    /**
     * @brief Genera un color aleatorio vibrante con canal alfa controlado.
     */
    [[nodiscard]] ColorRGBA NextColorRGBA(uint8_t minAlpha = 255) noexcept;

    // =========================================================================
    // Telemetría y Estado Interno
    // =========================================================================

    [[nodiscard]] uint64_t GetState() const noexcept { return m_state; }
    [[nodiscard]] uint64_t GetInc() const noexcept { return m_inc; }
    void SetState(uint64_t state, uint64_t inc) noexcept {
        m_state = state;
        m_inc = inc | 1u;
        m_hasCachedGaussian = false;
    }

    [[nodiscard]] uint64_t GetCallCount() const noexcept { return m_callCount; }
    void ResetCallCount() noexcept { m_callCount = 0; }

private:
    uint64_t m_state{0x853c49e6748fea9bULL};
    uint64_t m_inc{0xda3e39cb94b95bdbULL};
    uint64_t m_callCount{0};

    // Cache para la transformación Box-Muller de dos valores
    float m_cachedGaussian{0.0f};
    bool  m_hasCachedGaussian{false};
};

} // namespace WiiU
