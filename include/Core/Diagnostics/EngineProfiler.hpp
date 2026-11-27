#pragma once

#include "Platform/PlatformClock.hpp"
#include <array>
#include <cstdint>

namespace Neleo {

/**
 * @brief [OPT-461] Zonas fijas de perfilado CPU Zero-Alloc (GEA Cap. 9.8).
 * Indizado directo O(1) sin hashing de strings ni asignaciones en heap.
 */
enum class ProfileZone : uint8_t {
    FrameUpdate = 0u,
    Render3D    = 1u,
    QueueFlush  = 2u,
    Render2D    = 3u,
    PostFX      = 4u,
    ShadowPass  = 5u,
    Count       = 6u
};

/**
 * @brief [OPT-461] Profiler CPU ligero y determinista (Wii U OSGetSystemTime / Win32 QPC).
 * Mantiene acumulador del frame actual, ultimo frame (us) y media movil exponencial (us).
 */
class EngineProfiler {
public:
    static constexpr uint32_t ZONE_COUNT = static_cast<uint32_t>(ProfileZone::Count);

    static EngineProfiler& Instance() noexcept {
        static EngineProfiler s_instance;
        return s_instance;
    }

    void BeginFrame() noexcept {
        const uint64_t freq = PlatformClock::GetPerformanceFrequency();
        const double ticksToUs = (freq > 0u) ? (1000000.0 / static_cast<double>(freq)) : 0.0;

        for (uint32_t i = 0u; i < ZONE_COUNT; ++i) {
            const float lastUs = static_cast<float>(static_cast<double>(m_currentTicks[i]) * ticksToUs);
            m_lastUs[i] = lastUs;
            if (m_smoothedUs[i] <= 0.001f) {
                m_smoothedUs[i] = lastUs;
            } else {
                m_smoothedUs[i] = m_smoothedUs[i] * 0.88f + lastUs * 0.12f;
            }
            m_currentTicks[i] = 0u;
        }
        ++m_frameIndex;
    }

    void ResetAll() noexcept {
        m_currentTicks.fill(0u);
        m_lastUs.fill(0.0f);
        m_smoothedUs.fill(0.0f);
        m_frameIndex = 0u;
    }

    void AddZoneTicks(ProfileZone zone, uint64_t ticks) noexcept {
        const uint32_t idx = static_cast<uint32_t>(zone);
        if (idx < ZONE_COUNT) {
            m_currentTicks[idx] += ticks;
        }
    }

    [[nodiscard]] float GetSmoothedUs(ProfileZone zone) const noexcept {
        const uint32_t idx = static_cast<uint32_t>(zone);
        return (idx < ZONE_COUNT) ? m_smoothedUs[idx] : 0.0f;
    }

    [[nodiscard]] float GetLastUs(ProfileZone zone) const noexcept {
        const uint32_t idx = static_cast<uint32_t>(zone);
        return (idx < ZONE_COUNT) ? m_lastUs[idx] : 0.0f;
    }

    [[nodiscard]] uint64_t GetCurrentTicks(ProfileZone zone) const noexcept {
        const uint32_t idx = static_cast<uint32_t>(zone);
        return (idx < ZONE_COUNT) ? m_currentTicks[idx] : 0u;
    }

    [[nodiscard]] uint32_t GetFrameIndex() const noexcept { return m_frameIndex; }

    [[nodiscard]] static const char* GetZoneName(ProfileZone zone) noexcept {
        switch (zone) {
            case ProfileZone::FrameUpdate: return "UPDATE";
            case ProfileZone::Render3D:    return "3D";
            case ProfileZone::QueueFlush:  return "FLUSH";
            case ProfileZone::Render2D:    return "2D";
            case ProfileZone::PostFX:      return "POSTFX";
            case ProfileZone::ShadowPass:  return "SHADOW";
            default:                       return "UNK";
        }
    }

private:
    EngineProfiler() noexcept = default;

    std::array<uint64_t, ZONE_COUNT> m_currentTicks{};
    std::array<float, ZONE_COUNT> m_lastUs{};
    std::array<float, ZONE_COUNT> m_smoothedUs{};
    uint32_t m_frameIndex{0u};
};

/**
 * @brief Guarda RAII Zero-Alloc para medir una zona de codigo en microsegundos.
 */
class ScopedProfileZone {
public:
    explicit ScopedProfileZone(ProfileZone zone) noexcept
        : m_zone(zone), m_startTicks(PlatformClock::GetPerformanceCounter()) {}

    ~ScopedProfileZone() noexcept {
        const uint64_t endTicks = PlatformClock::GetPerformanceCounter();
        const uint64_t delta = (endTicks >= m_startTicks) ? (endTicks - m_startTicks) : 0u;
        EngineProfiler::Instance().AddZoneTicks(m_zone, delta);
    }

    ScopedProfileZone(const ScopedProfileZone&) = delete;
    ScopedProfileZone& operator=(const ScopedProfileZone&) = delete;

private:
    ProfileZone m_zone;
    uint64_t m_startTicks;
};

#define NELEO_PROFILE_SCOPE_CONCAT_IMPL(a, b) a##b
#define NELEO_PROFILE_SCOPE_CONCAT(a, b) NELEO_PROFILE_SCOPE_CONCAT_IMPL(a, b)
#define NELEO_PROFILE_SCOPE(zone) ::Neleo::ScopedProfileZone NELEO_PROFILE_SCOPE_CONCAT(neleoProfScope_, __LINE__)(zone)

} // namespace Neleo
