#include "Scene/Starfield3D.hpp"
#include "Graphics/Render2D.hpp"
#include <algorithm>

namespace WiiU {

Starfield3D::Starfield3D(int numStars, uint64_t seed)
    : m_initialSeed(seed), m_prng(seed) {
    m_stars.resize(numStars);

    for (auto& star : m_stars) {
        star.pos.x = m_prng.NextFloatRange(-10.0f, 10.0f);
        star.pos.y = m_prng.NextFloatRange(-10.0f, 10.0f);
        star.pos.z = m_prng.NextFloatRange(0.1f, 10.1f);
        star.speed = m_prng.NextFloatRange(1.5f, 3.5f);
        star.size  = m_prng.NextFloatRange(1.0f, 4.0f);
    }
}

void Starfield3D::ResetSeed(uint64_t seed) {
    m_initialSeed = seed;
    m_prng.SetSeed(seed);

    for (auto& star : m_stars) {
        star.pos.x = m_prng.NextFloatRange(-10.0f, 10.0f);
        star.pos.y = m_prng.NextFloatRange(-10.0f, 10.0f);
        star.pos.z = m_prng.NextFloatRange(0.1f, 10.1f);
        star.speed = m_prng.NextFloatRange(1.5f, 3.5f);
        star.size  = m_prng.NextFloatRange(1.0f, 4.0f);
    }
}

void Starfield3D::Update(float dt) {
    for (auto& star : m_stars) {
        star.pos.z -= star.speed * dt;
        if (star.pos.z <= 0.1f) {
            star.pos.z = 10.0f;
            star.pos.x = m_prng.NextFloatRange(-10.0f, 10.0f);
            star.pos.y = m_prng.NextFloatRange(-10.0f, 10.0f);
        }
    }
}

void Starfield3D::Render(int screenWidth, int screenHeight) {
    const float centerX = static_cast<float>(screenWidth) * 0.5f;
    const float centerY = static_cast<float>(screenHeight) * 0.5f;

    for (const auto& star : m_stars) {
        if (star.pos.z <= 0.1f) continue;

        const float invZ = 1.0f / star.pos.z;
        const float screenX = centerX + (star.pos.x * invZ) * 400.0f;
        const float screenY = centerY - (star.pos.y * invZ) * 400.0f;

        if (screenX >= 0.0f && screenX < static_cast<float>(screenWidth) &&
            screenY >= 0.0f && screenY < static_cast<float>(screenHeight)) {
            float sz = star.size * (1.5f * invZ);
            if (sz < 1.0f) sz = 1.0f;
            if (sz > 6.0f) sz = 6.0f;

            // Brillo atenuado según la distancia Z (profundidad en 3D)
            const uint8_t brightness = static_cast<uint8_t>(std::min(255.0f, 255.0f * (1.0f - star.pos.z / 10.0f)));
            const ColorRGBA starCol{
                brightness,
                static_cast<uint8_t>(brightness * 0.95f),
                255,
                255
            };
            const int rectSize = std::max(1, static_cast<int>(sz + 0.5f));
            Render2D::DrawRect(static_cast<int>(screenX), static_cast<int>(screenY), rectSize, rectSize, starCol);
        }
    }
}

} // namespace WiiU
