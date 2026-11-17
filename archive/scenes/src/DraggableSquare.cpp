#include "Scene/DraggableSquare.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include <gx2/draw.h>
#include <gx2/clear.h>

namespace WiiU {

DraggableSquare::DraggableSquare(float startX, float startY, float size)
    : m_x(startX), m_y(startY), m_size(size) {}

void DraggableSquare::Update(float dt, const TouchState& touchState, InputManager& input) {
    if (touchState.active) {
        if (!m_isDragging) {
            if (touchState.x >= m_x && touchState.x <= (m_x + m_size) &&
                touchState.y >= m_y && touchState.y <= (m_y + m_size)) {
                m_isDragging = true;
                m_grabOffsetX = touchState.x - m_x;
                m_grabOffsetY = touchState.y - m_y;
                input.TriggerAction("LAPIZ GAMEPAD");
            }
        } else {
            m_x = touchState.x - m_grabOffsetX;
            m_y = touchState.y - m_grabOffsetY;

            if (m_x < 0.0f) m_x = 0.0f;
            if (m_y < 0.0f) m_y = 0.0f;
            if (m_x > 1280.0f - m_size) m_x = 1280.0f - m_size;
            if (m_y > 720.0f - m_size) m_y = 720.0f - m_size;
        }
    } else {
        m_isDragging = false;
    }
}

void DraggableSquare::Render(SDL_Renderer* renderer) {
    SDL_FRect sqRect = { m_x, m_y, m_size, m_size };

    SDL_Vertex verts[8];
    static const int indices[12] = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };

    if (m_isDragging) {
        // Glow (Resplandor Verde Neón) + Cuadrado Principal (Amarillo Brillante)
        SDL_FRect glowRect = { sqRect.x - 6.0f, sqRect.y - 6.0f, sqRect.w + 12.0f, sqRect.h + 12.0f };
        SDL_Color glowCol = { 0, 255, 180, 160 };
        SDL_Color sqCol = { 255, 240, 150, 255 };

        verts[0] = { { glowRect.x, glowRect.y }, glowCol, { 0.0f, 0.0f } };
        verts[1] = { { glowRect.x + glowRect.w, glowRect.y }, glowCol, { 0.0f, 0.0f } };
        verts[2] = { { glowRect.x + glowRect.w, glowRect.y + glowRect.h }, glowCol, { 0.0f, 0.0f } };
        verts[3] = { { glowRect.x, glowRect.y + glowRect.h }, glowCol, { 0.0f, 0.0f } };

        verts[4] = { { sqRect.x, sqRect.y }, sqCol, { 0.0f, 0.0f } };
        verts[5] = { { sqRect.x + sqRect.w, sqRect.y }, sqCol, { 0.0f, 0.0f } };
        verts[6] = { { sqRect.x + sqRect.w, sqRect.y + sqRect.h }, sqCol, { 0.0f, 0.0f } };
        verts[7] = { { sqRect.x, sqRect.y + sqRect.h }, sqCol, { 0.0f, 0.0f } };

        SDL_RenderGeometry(renderer, nullptr, verts, 8, indices, 12);

        SDL_SetRenderDrawColor(renderer, 0, 255, 240, 255);
        SDL_RenderDrawRectF(renderer, &sqRect);

        Render2D::drawPixelString(renderer, "LAPIZ", static_cast<int>(sqRect.x + 15), static_cast<int>(sqRect.y + 35), 2, ColorRGBA{10, 10, 10, 255});
    } else {
        // Sombra Posterior + Cuadrado Principal (Blanco Sólido)
        SDL_FRect shadowRect = { sqRect.x + 4.0f, sqRect.y + 4.0f, sqRect.w, sqRect.h };
        SDL_Color shadowCol = { 20, 20, 30, 160 };
        SDL_Color sqCol = { 255, 255, 255, 255 };

        verts[0] = { { shadowRect.x, shadowRect.y }, shadowCol, { 0.0f, 0.0f } };
        verts[1] = { { shadowRect.x + shadowRect.w, shadowRect.y }, shadowCol, { 0.0f, 0.0f } };
        verts[2] = { { shadowRect.x + shadowRect.w, shadowRect.y + shadowRect.h }, shadowCol, { 0.0f, 0.0f } };
        verts[3] = { { shadowRect.x, shadowRect.y + shadowRect.h }, shadowCol, { 0.0f, 0.0f } };

        verts[4] = { { sqRect.x, sqRect.y }, sqCol, { 0.0f, 0.0f } };
        verts[5] = { { sqRect.x + sqRect.w, sqRect.y }, sqCol, { 0.0f, 0.0f } };
        verts[6] = { { sqRect.x + sqRect.w, sqRect.y + sqRect.h }, sqCol, { 0.0f, 0.0f } };
        verts[7] = { { sqRect.x, sqRect.y + sqRect.h }, sqCol, { 0.0f, 0.0f } };

        SDL_RenderGeometry(renderer, nullptr, verts, 8, indices, 12);

        SDL_SetRenderDrawColor(renderer, 30, 30, 40, 255);
        SDL_RenderDrawRectF(renderer, &sqRect);

        Render2D::drawPixelString(renderer, "TOCAME", static_cast<int>(sqRect.x + 10), static_cast<int>(sqRect.y + 35), 2, ColorRGBA{40, 40, 40, 255});
    }
}

} // namespace WiiU
