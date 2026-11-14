#pragma once

#include <SDL2/SDL.h>
#include "Graphics/Color.hpp"
#include <string>
#include <string_view>
#include <cstdint>

namespace WiiU {

class Render2D {
public:
    static void GetFont5x7(char c, uint8_t out[5]);
    static void DrawPixelChar(SDL_Renderer* renderer, char c, int x, int y, int scale, const ColorRGBA& color);
    static void DrawPixelString(SDL_Renderer* renderer, std::string_view text, int x, int y, int scale, const ColorRGBA& color);
    static void DrawShrinkingRing(SDL_Renderer* renderer, int cx, int cy, float radius, const ColorRGBA& color);
    static void DrawBox(SDL_Renderer* renderer, int x, int y, int w, int h, const ColorRGBA& fillColor, const ColorRGBA& borderColor);

    // Backward compatibility aliases
    static void getFont5x7(char c, uint8_t out[5]) { GetFont5x7(c, out); }
    static void drawPixelChar(SDL_Renderer* renderer, char c, int x, int y, int scale, const ColorRGBA& color) {
        DrawPixelChar(renderer, c, x, y, scale, color);
    }
    static void drawPixelString(SDL_Renderer* renderer, std::string_view text, int x, int y, int scale, const ColorRGBA& color) {
        DrawPixelString(renderer, text, x, y, scale, color);
    }
    static void drawShrinkingRing(SDL_Renderer* renderer, int cx, int cy, float radius, const ColorRGBA& color) {
        DrawShrinkingRing(renderer, cx, cy, radius, color);
    }
    static void drawBox(SDL_Renderer* renderer, int x, int y, int w, int h, const ColorRGBA& fillColor, const ColorRGBA& borderColor) {
        DrawBox(renderer, x, y, w, h, fillColor, borderColor);
    }
};

} // namespace WiiU
