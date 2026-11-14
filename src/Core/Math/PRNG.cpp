#include "Core/Math/PRNG.hpp"
#include <algorithm>

namespace WiiU {

constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float PI     = 3.14159265358979323846f;

void PRNG::SetSeed(uint64_t seed, uint64_t sequence) noexcept {
    m_state = 0U;
    m_inc = (sequence << 1u) | 1u;
    m_callCount = 0;
    m_hasCachedGaussian = false;

    // Algoritmo de arranque PCG: dos iteraciones de mezcla
    (void)NextUInt32();
    m_state += seed;
    (void)NextUInt32();
}

uint32_t PRNG::NextUInt32() noexcept {
    ++m_callCount;
    const uint64_t oldstate = m_state;
    // Multiplicador LCG de 64 bits de Donald Knuth
    m_state = oldstate * 6364136223846793005ULL + m_inc;

    // Permutación XSH-RR (xorshift high, random rotate)
    const uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
    const uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}

uint32_t PRNG::NextUInt32(uint32_t maxExclusive) noexcept {
    if (maxExclusive <= 1u) return 0u;

    // Algoritmo casi libre de división de Daniel Lemire (2019)
    uint64_t x = NextUInt32();
    uint64_t m = x * static_cast<uint64_t>(maxExclusive);
    uint32_t l = static_cast<uint32_t>(m);

    if (l < maxExclusive) {
        uint32_t t = -maxExclusive;
        if (t >= maxExclusive) {
            t -= maxExclusive;
            if (t >= maxExclusive) {
                t %= maxExclusive;
            }
        }
        while (l < t) {
            x = NextUInt32();
            m = x * static_cast<uint64_t>(maxExclusive);
            l = static_cast<uint32_t>(m);
        }
    }
    return static_cast<uint32_t>(m >> 32u);
}

int32_t PRNG::NextIntRange(int32_t minInclusive, int32_t maxInclusive) noexcept {
    if (minInclusive >= maxInclusive) return minInclusive;
    const uint32_t range = static_cast<uint32_t>(maxInclusive - minInclusive + 1);
    return minInclusive + static_cast<int32_t>(NextUInt32(range));
}

uint64_t PRNG::NextUInt64() noexcept {
    const uint64_t hi = static_cast<uint64_t>(NextUInt32());
    const uint64_t lo = static_cast<uint64_t>(NextUInt32());
    return (hi << 32u) | lo;
}

float PRNG::NextFloat01() noexcept {
    // 24 bits de mantisa normalizada uniforme en [0.0f, 1.0f)
    return static_cast<float>(NextUInt32() >> 8u) * (1.0f / 16777216.0f);
}

float PRNG::NextFloatRange(float min, float max) noexcept {
    if (min >= max) return min;
    return min + NextFloat01() * (max - min);
}

float PRNG::NextFloatSigned() noexcept {
    // Rango simétrico [-1.0f, 1.0f]
    return NextFloat01() * 2.0f - 1.0f;
}

float PRNG::NextFloatVariance(float base, float variance) noexcept {
    if (variance <= 0.0f) return base;
    return base + NextFloatSigned() * variance;
}

float PRNG::NextGaussian(float mean, float stdDev) noexcept {
    if (m_hasCachedGaussian) {
        m_hasCachedGaussian = false;
        return mean + m_cachedGaussian * stdDev;
    }

    // Transformación polar Box-Muller (1958)
    float u1 = NextFloat01();
    while (u1 <= 1e-7f) {
        u1 = NextFloat01();
    }
    const float u2 = NextFloat01();

    const float radius = std::sqrt(-2.0f * std::log(u1));
    const float theta  = TWO_PI * u2;

    const float z0 = radius * std::cos(theta);
    const float z1 = radius * std::sin(theta);

    m_cachedGaussian = z1;
    m_hasCachedGaussian = true;

    return mean + z0 * stdDev;
}

Point2D PRNG::NextPointOnCircle(float radius) noexcept {
    const float theta = NextFloat01() * TWO_PI;
    return Point2D{
        radius * std::cos(theta),
        radius * std::sin(theta),
        0.0f
    };
}

Point2D PRNG::NextPointInCircle(float radius) noexcept {
    // Raíz cuadrada para densidad de área espacial uniforme
    const float r = radius * std::sqrt(NextFloat01());
    const float theta = NextFloat01() * TWO_PI;
    return Point2D{
        r * std::cos(theta),
        r * std::sin(theta),
        0.0f
    };
}

Point3D PRNG::NextPointOnSphere(float radius) noexcept {
    // Muestreo analítico de Arquímedes sin sesgo de apiñamiento polar
    const float z = NextFloatSigned();
    const float theta = NextFloat01() * TWO_PI;
    const float rCyl = std::sqrt(std::max(0.0f, 1.0f - z * z));

    return Point3D{
        radius * rCyl * std::cos(theta),
        radius * rCyl * std::sin(theta),
        radius * z
    };
}

Point3D PRNG::NextPointInSphere(float radius) noexcept {
    // Corrección cúbica para volumen homogéneo en 3D
    const float r = radius * std::cbrt(NextFloat01());
    const Point3D surfacePt = NextPointOnSphere(1.0f);
    return Point3D{
        surfacePt.x * r,
        surfacePt.y * r,
        surfacePt.z * r
    };
}

Point3D PRNG::NextDirectionInCone(const Point3D& coneAxis, float maxHalfAngleRad) noexcept {
    const float clampedAngle = std::clamp(maxHalfAngleRad, 0.0f, PI);
    const float cosMin = std::cos(clampedAngle);

    // Muestreo del casquete esférico alrededor del eje canónico +Z
    const float cosTheta = NextFloatRange(cosMin, 1.0f);
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = NextFloat01() * TWO_PI;

    const Point3D localDir{
        sinTheta * std::cos(phi),
        sinTheta * std::sin(phi),
        cosTheta
    };

    const Point3D normAxis = coneAxis.Normalized();

    // Si el eje es casi idéntico al eje Z canónico
    if (normAxis.z > 0.999f) {
        return localDir;
    }
    if (normAxis.z < -0.999f) {
        return Point3D{localDir.x, -localDir.y, -localDir.z};
    }

    // Construcción de base ortonormal
    const Point3D ref = (std::abs(normAxis.x) > 0.9f) ? Point3D{0.0f, 1.0f, 0.0f} : Point3D{1.0f, 0.0f, 0.0f};
    const Point3D u = Point3D::Cross(ref, normAxis).Normalized();
    const Point3D v = Point3D::Cross(normAxis, u);

    return Point3D{
        u.x * localDir.x + v.x * localDir.y + normAxis.x * localDir.z,
        u.y * localDir.x + v.y * localDir.y + normAxis.y * localDir.z,
        u.z * localDir.x + v.z * localDir.y + normAxis.z * localDir.z
    }.Normalized();
}

Point3D PRNG::NextPointInAABB(const Point3D& minCorner, const Point3D& maxCorner) noexcept {
    return Point3D{
        NextFloatRange(minCorner.x, maxCorner.x),
        NextFloatRange(minCorner.y, maxCorner.y),
        NextFloatRange(minCorner.z, maxCorner.z)
    };
}

ColorRGBA PRNG::NextColor(const ColorRGBA& base, float variance) noexcept {
    auto jitter = [this, variance](uint8_t channel) -> uint8_t {
        const float val = static_cast<float>(channel) + NextFloatSigned() * variance * 255.0f;
        return static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f));
    };

    return ColorRGBA{
        jitter(base.r),
        jitter(base.g),
        jitter(base.b),
        base.a
    };
}

ColorRGBA PRNG::NextColorRGBA(uint8_t minAlpha) noexcept {
    return ColorRGBA{
        static_cast<uint8_t>(NextUInt32(256)),
        static_cast<uint8_t>(NextUInt32(256)),
        static_cast<uint8_t>(NextUInt32(256)),
        static_cast<uint8_t>(NextIntRange(minAlpha, 255))
    };
}

ColorRGBA ColorRGBA::Random(PRNG& prng) {
    return prng.NextColorRGBA();
}

} // namespace WiiU
