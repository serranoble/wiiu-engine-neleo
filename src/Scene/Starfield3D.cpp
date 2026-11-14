#include "Scene/Starfield3D.hpp"
#include <algorithm>

namespace WiiU {

Starfield3D::Starfield3D(int numStars, uint64_t seed)
    : m_initialSeed(seed), m_prng(seed) {
    m_stars.resize(numStars);
    m_vertexBatch.resize(numStars * 4);
    m_indexBatch.resize(numStars * 6);

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

void Starfield3D::Render(SDL_Renderer* renderer, int screenWidth, int screenHeight) {
    int visibleStars = 0;
    float centerX = screenWidth / 2.0f;
    float centerY = screenHeight / 2.0f;

    for (const auto& star : m_stars) {
        if (star.pos.z <= 0.1f) continue;

        float invZ = 1.0f / star.pos.z;
        float screenX = centerX + (star.pos.x * invZ) * 400.0f;
        float screenY = centerY - (star.pos.y * invZ) * 400.0f;

        if (screenX >= 0.0f && screenX < static_cast<float>(screenWidth) &&
            screenY >= 0.0f && screenY < static_cast<float>(screenHeight)) {
            float sz = star.size * (1.5f * invZ);
            if (sz < 1.0f) sz = 1.0f;
            if (sz > 6.0f) sz = 6.0f;

            // Brillo atenuado según la distancia Z (profundidad en 3D)
            Uint8 brightness = static_cast<Uint8>(std::min(255.0f, 255.0f * (1.0f - star.pos.z / 10.0f)));
            SDL_Color starCol = { brightness, static_cast<Uint8>(brightness * 0.95f), 255, 255 };

            int vertOffset = visibleStars * 4;
            m_vertexBatch[vertOffset + 0] = { { screenX, screenY }, starCol, { 0.0f, 0.0f } };
            m_vertexBatch[vertOffset + 1] = { { screenX + sz, screenY }, starCol, { 0.0f, 0.0f } };
            m_vertexBatch[vertOffset + 2] = { { screenX + sz, screenY + sz }, starCol, { 0.0f, 0.0f } };
            m_vertexBatch[vertOffset + 3] = { { screenX, screenY + sz }, starCol, { 0.0f, 0.0f } };

            int idxOffset = visibleStars * 6;
            m_indexBatch[idxOffset + 0] = vertOffset + 0;
            m_indexBatch[idxOffset + 1] = vertOffset + 1;
            m_indexBatch[idxOffset + 2] = vertOffset + 2;
            m_indexBatch[idxOffset + 3] = vertOffset + 0;
            m_indexBatch[idxOffset + 4] = vertOffset + 2;
            m_indexBatch[idxOffset + 5] = vertOffset + 3;

            visibleStars++;
        }
    }

    // 1 SOLA LLAMADA A LA GPU GX2 DE WII U PARA TODAS LAS ESTRELLAS 3D (SINGLE DRAW CALL)
    if (visibleStars > 0) {
        SDL_RenderGeometry(renderer, nullptr, m_vertexBatch.data(), visibleStars * 4, m_indexBatch.data(), visibleStars * 6);
    }
}

} // namespace WiiU
