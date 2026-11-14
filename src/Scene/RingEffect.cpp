#include "Scene/RingEffect.hpp"
#include <SDL2/SDL.h>
#include <gx2/draw.h>
#include <gx2/clear.h>
#include <cmath>
#include <vector>

namespace WiiU {

void RingEffect::Trigger() {
    m_active = true;
    m_timer = m_duration;
}

void RingEffect::Update(float dt) {
    if (m_active) {
        m_timer -= dt;
        if (m_timer <= 0.0f) {
            m_active = false;
            m_timer = 0.0f;
        }
    }
}

void RingEffect::Render(SDL_Renderer* renderer, int centerX, int centerY) {
    if (!m_active) return;

    float progress = m_timer / m_duration;
    float currentRadius = m_maxRadius * progress;
    if (currentRadius <= 2.0f) return;

    Uint8 alpha = static_cast<Uint8>(255 * progress);
    SDL_Color ringColor = { 0, 240, 255, alpha };

    constexpr int SEGMENTS = 32;

    static struct TrigTable {
        float cosTable[SEGMENTS];
        float sinTable[SEGMENTS];
        TrigTable() {
            constexpr float TWO_PI = 6.2831853f;
            for (int i = 0; i < SEGMENTS; ++i) {
                float angle = (static_cast<float>(i) / static_cast<float>(SEGMENTS)) * TWO_PI;
                cosTable[i] = std::cos(angle);
                sinTable[i] = std::sin(angle);
            }
        }
    } s_trig;

    float innerR = currentRadius - 3.0f;
    float outerR = currentRadius + 3.0f;
    if (innerR < 0.0f) innerR = 0.0f;

    SDL_Vertex verts[SEGMENTS * 2];
    int indices[SEGMENTS * 6];

    for (int i = 0; i < SEGMENTS; ++i) {
        float cosA = s_trig.cosTable[i];
        float sinA = s_trig.sinTable[i];

        verts[i * 2 + 0] = { { centerX + innerR * cosA, centerY + innerR * sinA }, ringColor, { 0.0f, 0.0f } };
        verts[i * 2 + 1] = { { centerX + outerR * cosA, centerY + outerR * sinA }, ringColor, { 0.0f, 0.0f } };

        int nextI = (i + 1) % SEGMENTS;
        int idx = i * 6;
        indices[idx + 0] = i * 2 + 0;
        indices[idx + 1] = i * 2 + 1;
        indices[idx + 2] = nextI * 2 + 1;

        indices[idx + 3] = i * 2 + 0;
        indices[idx + 4] = nextI * 2 + 1;
        indices[idx + 5] = nextI * 2 + 0;
    }

    // RENDERIZADO NATIVO GX2 DEL ANILLO (SINGLE DRAW CALL CON GEOMETRÍA EN ANILLO)
    SDL_RenderGeometry(renderer, nullptr, verts, SEGMENTS * 2, indices, SEGMENTS * 6);
}

} // namespace WiiU
