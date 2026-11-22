#pragma once

#include "Graphics/Color.hpp"
#include <string_view>
#include <cstdint>

namespace WiiU {

class Render2D {
public:
    static void Begin();
    static void End();
    static void GetFont5x7(char c, uint8_t out[5]);
    static void DrawRect(int x, int y, int w, int h, const ColorRGBA& color);
    static void DrawLine(int x0, int y0, int x1, int y1, const ColorRGBA& color, float width = 1.5f);
    static void DrawPixelChar(char c, int x, int y, int scale, const ColorRGBA& color);
    static void DrawPixelString(std::string_view text, int x, int y, int scale, const ColorRGBA& color);
    static void DrawShrinkingRing(int cx, int cy, float radius, const ColorRGBA& color);
    static void DrawBox(int x, int y, int w, int h, const ColorRGBA& fillColor, const ColorRGBA& borderColor);

    // Aliases camelCase unificados
    static void getFont5x7(char c, uint8_t out[5]) { GetFont5x7(c, out); }
    static void drawRect(int x, int y, int w, int h, const ColorRGBA& color) {
        DrawRect(x, y, w, h, color);
    }
    static void drawLine(int x0, int y0, int x1, int y1, const ColorRGBA& color, float width = 1.5f) {
        DrawLine(x0, y0, x1, y1, color, width);
    }
    static void drawPixelChar(char c, int x, int y, int scale, const ColorRGBA& color) {
        DrawPixelChar(c, x, y, scale, color);
    }
    static void drawPixelString(std::string_view text, int x, int y, int scale, const ColorRGBA& color) {
        DrawPixelString(text, x, y, scale, color);
    }
    static void drawShrinkingRing(int cx, int cy, float radius, const ColorRGBA& color) {
        DrawShrinkingRing(cx, cy, radius, color);
    }
    static void drawBox(int x, int y, int w, int h, const ColorRGBA& fillColor, const ColorRGBA& borderColor) {
        DrawBox(x, y, w, h, fillColor, borderColor);
    }
};

} // namespace WiiU
