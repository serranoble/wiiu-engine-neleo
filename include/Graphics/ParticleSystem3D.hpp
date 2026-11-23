#pragma once

#include "Graphics/Math3D.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace WiiU::Graphics {

/**
 * @brief Sistema de Partículas 3D Zero-Alloc (pool estático de 128 partículas) para fuego aditivo y humo.
 *        0 asignaciones dinámicas en Update/Render a 60 FPS (RTR4 Cap. 13).
 */
class ParticleSystem3D {
public:
    static constexpr uint32_t MAX_PARTICLES = 128u;

    struct Particle {
        Point3D pos{0.0f, 0.0f, 0.0f};
        Point3D vel{0.0f, 0.0f, 0.0f};
        float age{0.0f};
        float life{1.0f};
        float size{0.5f};
        bool isSmoke{false};
        bool active{false};
    };

    ParticleSystem3D() noexcept = default;

    void Reset(const Point3D& origin = Point3D{0.0f, 0.0f, 0.0f}) noexcept {
        m_origin = origin;
        m_spawnAccum = 0.0f;
        m_rngState = 0x9E3779B9u;
        m_activeCount = 0u;
        for (auto& p : m_pool) {
            p.active = false;
        }
    }

    void SetOrigin(const Point3D& origin) noexcept { m_origin = origin; }

    void Update(float dt, bool emitEnabled = true) noexcept {
        const float safeDt = (dt > 0.0f && dt < 0.1f) ? dt : 0.016667f;

        if (emitEnabled) {
            m_spawnAccum += safeDt * 56.0f; // ~56 partículas/seg (fuego + humo)
            while (m_spawnAccum >= 1.0f) {
                m_spawnAccum -= 1.0f;
                SpawnOne();
            }
        }

        uint32_t active = 0u;
        for (auto& p : m_pool) {
            if (!p.active) continue;
            p.age += safeDt;
            if (p.age >= p.life) {
                p.active = false;
                continue;
            }
            p.pos.x += p.vel.x * safeDt;
            p.pos.y += p.vel.y * safeDt;
            p.pos.z += p.vel.z * safeDt;
            if (p.isSmoke) {
                // Brisa suave y expansión del humo
                p.vel.x += 0.25f * safeDt;
                p.size += 0.55f * safeDt;
            } else {
                // Aceleración ascendente y contracción del fuego
                p.vel.y += 0.85f * safeDt;
                p.size *= (1.0f - 0.35f * safeDt);
                if (p.size < 0.08f) p.size = 0.08f;
            }
            ++active;
        }
        m_activeCount = active;
    }

    /**
     * @brief Construye en O(1) una matriz Billboard orientada a la cámara para renderizar un quad 2D en 3D.
     */
    [[nodiscard]] static Matrix4x4 MakeBillboardMatrix(const Point3D& pos,
                                                       const Point3D& camRight,
                                                       const Point3D& camUp,
                                                       const Point3D& camForward,
                                                       float scale) noexcept {
        Matrix4x4 m{};
        m.m[0]  = camRight.x * scale;   m.m[4]  = camUp.x * scale;   m.m[8]  = -camForward.x * scale; m.m[12] = pos.x;
        m.m[1]  = camRight.y * scale;   m.m[5]  = camUp.y * scale;   m.m[9]  = -camForward.y * scale; m.m[13] = pos.y;
        m.m[2]  = camRight.z * scale;   m.m[6]  = camUp.z * scale;   m.m[10] = -camForward.z * scale; m.m[14] = pos.z;
        m.m[3]  = 0.0f;                 m.m[7]  = 0.0f;              m.m[11] = 0.0f;                  m.m[15] = 1.0f;
        return m;
    }

    [[nodiscard]] const std::array<Particle, MAX_PARTICLES>& GetParticles() const noexcept { return m_pool; }
    [[nodiscard]] uint32_t GetActiveCount() const noexcept { return m_activeCount; }

private:
    [[nodiscard]] float NextRand01() noexcept {
        // Xorshift32 determinista Zero-Alloc
        uint32_t x = m_rngState;
        x ^= x << 13u;
        x ^= x >> 17u;
        x ^= x << 5u;
        m_rngState = x;
        return static_cast<float>(x & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    }

    void SpawnOne() noexcept {
        for (auto& p : m_pool) {
            if (p.active) continue;
            const bool smoke = (NextRand01() > 0.68f);
            const float angle = NextRand01() * 6.2831853f;
            const float rad = NextRand01() * (smoke ? 0.55f : 0.42f);

            p.active = true;
            p.isSmoke = smoke;
            p.age = 0.0f;
            p.life = smoke ? (1.6f + NextRand01() * 1.1f) : (0.55f + NextRand01() * 0.55f);
            p.size = smoke ? (0.65f + NextRand01() * 0.35f) : (0.52f + NextRand01() * 0.36f);
            p.pos = Point3D{
                m_origin.x + std::cos(angle) * rad,
                m_origin.y + (smoke ? 0.85f : 0.12f),
                m_origin.z + std::sin(angle) * rad
            };
            p.vel = Point3D{
                (NextRand01() - 0.5f) * (smoke ? 0.55f : 0.70f),
                smoke ? (1.25f + NextRand01() * 0.65f) : (2.25f + NextRand01() * 1.45f),
                (NextRand01() - 0.5f) * (smoke ? 0.55f : 0.70f)
            };
            return;
        }
    }

    Point3D m_origin{0.0f, 0.0f, 0.0f};
    std::array<Particle, MAX_PARTICLES> m_pool{};
    float m_spawnAccum{0.0f};
    uint32_t m_rngState{0x9E3779B9u};
    uint32_t m_activeCount{0u};
};

} // namespace WiiU::Graphics
