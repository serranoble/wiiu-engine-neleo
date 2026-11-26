#pragma once

#include <algorithm>
#include <cstdint>

namespace Neleo {

/**
 * @brief [Hito 133] Reloj Jerarquico de Simulacion en Tiempo Real (Jason Gregory, Cap. 7).
 *
 * Separa estrictamente el reloj de hardware (`RealTime`) del reloj de simulacion de gameplay
 * (`GameplayTime`) y del acumulador de paso fijo para fisica (`Physics`), habilitando:
 * - Bullet-Time / Slow-Motion y Fast-Forward mediante `timeScale`.
 * - Hit-Stop / Freeze-Frame tras impactos o eventos clave sin congelar UI ni camara.
 * - Single-Step Frame Advance determinista de 16.67 ms para depuracion cuadro a cuadro.
 * - Factor de interpolacion `alpha` en `[0, 1]` para suavizado visual entre pasos fijos.
 *
 * Estructura sin asignaciones dinamicas en heap (`0 alloc/frame`).
 */
class GameClock {
public:
    static constexpr float FIXED_PHYSICS_DT = 1.0f / 60.0f;
    static constexpr float MAX_RAW_DT = 0.1f;
    static constexpr uint32_t MAX_PHYSICS_STEPS = 4;

    constexpr GameClock() noexcept = default;

    void Reset() noexcept {
        m_realTimeTotal = 0.0f;
        m_gameplayTimeTotal = 0.0f;
        m_realDelta = 0.0f;
        m_gameplayDelta = 0.0f;
        m_timeScale = 1.0f;
        m_hitStopTimer = 0.0f;
        m_physicsAccumulator = 0.0f;
        m_interpolationAlpha = 0.0f;
        m_physicsStepCount = 0;
        m_frameCount = 0;
        m_isPaused = false;
        m_singleStepRequested = false;
    }

    /**
     * @brief Avanza el reloj jerarquico un frame de renderizado.
     * @param rawHardwareDeltaSec Delta de hardware crudo en segundos.
     */
    void Tick(float rawHardwareDeltaSec) noexcept {
        m_realDelta = std::clamp(rawHardwareDeltaSec, 0.0f, MAX_RAW_DT);
        m_realTimeTotal += m_realDelta;
        ++m_frameCount;

        if (m_singleStepRequested) {
            m_singleStepRequested = false;
            m_gameplayDelta = FIXED_PHYSICS_DT;
            m_gameplayTimeTotal += m_gameplayDelta;
            m_physicsStepCount = 1;
            m_physicsAccumulator = 0.0f;
            m_interpolationAlpha = 1.0f;
            return;
        }

        if (m_isPaused) {
            m_gameplayDelta = 0.0f;
            m_physicsStepCount = 0;
            return;
        }

        if (m_hitStopTimer > 0.0f) {
            m_hitStopTimer = std::max(0.0f, m_hitStopTimer - m_realDelta);
            m_gameplayDelta = 0.0f;
            m_physicsStepCount = 0;
            return;
        }

        m_gameplayDelta = m_realDelta * m_timeScale;
        m_gameplayTimeTotal += m_gameplayDelta;
        m_physicsAccumulator += m_gameplayDelta;

        m_physicsStepCount = 0;
        while (m_physicsAccumulator >= FIXED_PHYSICS_DT && m_physicsStepCount < MAX_PHYSICS_STEPS) {
            m_physicsAccumulator -= FIXED_PHYSICS_DT;
            ++m_physicsStepCount;
        }

        if (m_physicsStepCount >= MAX_PHYSICS_STEPS && m_physicsAccumulator > FIXED_PHYSICS_DT) {
            m_physicsAccumulator = FIXED_PHYSICS_DT * 0.5f;
        }

        m_interpolationAlpha = std::clamp(m_physicsAccumulator / FIXED_PHYSICS_DT, 0.0f, 1.0f);
    }

    void SetTimeScale(float scale) noexcept {
        m_timeScale = std::clamp(scale, 0.05f, 4.0f);
    }

    [[nodiscard]] float GetTimeScale() const noexcept {
        return m_timeScale;
    }

    void TriggerHitStop(float durationSec) noexcept {
        if (durationSec > m_hitStopTimer) {
            m_hitStopTimer = std::clamp(durationSec, 0.0f, 1.0f);
        }
    }

    [[nodiscard]] bool IsInHitStop() const noexcept {
        return m_hitStopTimer > 0.0f;
    }

    [[nodiscard]] float GetHitStopRemaining() const noexcept {
        return m_hitStopTimer;
    }

    void SetPaused(bool paused) noexcept {
        m_isPaused = paused;
    }

    [[nodiscard]] bool IsPaused() const noexcept {
        return m_isPaused;
    }

    /**
     * @brief Pausa la simulacion y programa la ejecucion exacta de un paso fijo (16.67 ms).
     */
    void RequestSingleStep() noexcept {
        m_isPaused = true;
        m_singleStepRequested = true;
    }

    [[nodiscard]] float GetRealDelta() const noexcept {
        return m_realDelta;
    }

    [[nodiscard]] float GetGameplayDelta() const noexcept {
        return m_gameplayDelta;
    }

    [[nodiscard]] float GetRealTimeTotal() const noexcept {
        return m_realTimeTotal;
    }

    [[nodiscard]] float GetGameplayTimeTotal() const noexcept {
        return m_gameplayTimeTotal;
    }

    [[nodiscard]] float GetInterpolationAlpha() const noexcept {
        return m_interpolationAlpha;
    }

    [[nodiscard]] uint32_t GetPhysicsStepCount() const noexcept {
        return m_physicsStepCount;
    }

    [[nodiscard]] uint64_t GetFrameCount() const noexcept {
        return m_frameCount;
    }

private:
    float m_realTimeTotal{0.0f};
    float m_gameplayTimeTotal{0.0f};
    float m_realDelta{0.0f};
    float m_gameplayDelta{0.0f};
    float m_timeScale{1.0f};
    float m_hitStopTimer{0.0f};
    float m_physicsAccumulator{0.0f};
    float m_interpolationAlpha{0.0f};
    uint32_t m_physicsStepCount{0};
    uint64_t m_frameCount{0};
    bool m_isPaused{false};
    bool m_singleStepRequested{false};
};

} // namespace Neleo
