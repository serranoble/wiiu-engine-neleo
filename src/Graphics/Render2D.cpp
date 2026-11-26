#include "Graphics/Render2D.hpp"
#include "Core/Window.hpp"
#if defined(__WIIU__) || defined(__wiiu__)
#include "Graphics/Render2DGX2.hpp"
#include <gx2/context.h>
#include <gx2/registers.h>
#else
#include "Graphics/GL/Render2DGL.hpp"
#endif
#include <cstring>
#include <cctype>
#include <cmath>

namespace Neleo {

void Render2D::GetFont5x7(char c, uint8_t out[5]) {
    std::memset(out, 0, 5);
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    switch (c) {
        case '!': out[2] = 0x5F; break;
        case '.': out[1] = 0x60; out[2] = 0x60; break;
        case ',': out[1] = 0x40; out[2] = 0x60; out[3] = 0x20; break;
        case ':': out[1] = 0x36; out[2] = 0x36; break;
        case ';': out[1] = 0x40; out[2] = 0x56; break;
        case '%': out[0] = 0x23; out[1] = 0x13; out[2] = 0x08; out[3] = 0x64; out[4] = 0x62; break;
        case '|': out[2] = 0x7F; break;
        case '(': out[1] = 0x1C; out[2] = 0x22; out[3] = 0x41; break;
        case ')': out[1] = 0x41; out[2] = 0x22; out[3] = 0x1C; break;
        case '[': out[1] = 0x7F; out[2] = 0x41; out[3] = 0x41; break;
        case ']': out[1] = 0x41; out[2] = 0x41; out[3] = 0x7F; break;
        case '{': out[1] = 0x08; out[2] = 0x36; out[3] = 0x41; break;
        case '}': out[1] = 0x41; out[2] = 0x36; out[3] = 0x08; break;
        case '<': out[1] = 0x10; out[2] = 0x28; out[3] = 0x44; break;
        case '>': out[1] = 0x44; out[2] = 0x28; out[3] = 0x10; break;
        case '+': out[0] = 0x08; out[1] = 0x08; out[2] = 0x3E; out[3] = 0x08; out[4] = 0x08; break;
        case '-': out[0] = out[1] = out[2] = out[3] = out[4] = 0x08; break;
        case '*': out[0] = 0x14; out[1] = 0x08; out[2] = 0x3E; out[3] = 0x08; out[4] = 0x14; break;
        case '/': out[0] = 0x20; out[1] = 0x10; out[2] = 0x08; out[3] = 0x04; out[4] = 0x02; break;
        case '=': out[0] = out[1] = out[2] = out[3] = out[4] = 0x14; break;
        case '_': out[0] = out[1] = out[2] = out[3] = out[4] = 0x40; break;
        case '?': out[0] = 0x02; out[1] = 0x01; out[2] = 0x51; out[3] = 0x09; out[4] = 0x06; break;
        case '\'': out[2] = 0x07; break;
        case '"': out[1] = 0x07; out[3] = 0x07; break;
        case '&': out[0] = 0x36; out[1] = 0x49; out[2] = 0x55; out[3] = 0x22; out[4] = 0x50; break;
        case '#': out[0] = 0x14; out[1] = 0x7F; out[2] = 0x14; out[3] = 0x7F; out[4] = 0x14; break;
        case '@': out[0] = 0x3E; out[1] = 0x41; out[2] = 0x5D; out[3] = 0x55; out[4] = 0x1E; break;
        case '~': out[1] = 0x02; out[2] = 0x01; out[3] = 0x02; out[4] = 0x01; break;
        case '0': out[0]=0x3E; out[1]=0x51; out[2]=0x49; out[3]=0x45; out[4]=0x3E; break;
        case '1': out[1]=0x42; out[2]=0x7F; out[3]=0x40; break;
        case '2': out[0]=0x42; out[1]=0x61; out[2]=0x51; out[3]=0x49; out[4]=0x46; break;
        case '3': out[0]=0x21; out[1]=0x41; out[2]=0x45; out[3]=0x4B; out[4]=0x31; break;
        case '4': out[0]=0x18; out[1]=0x14; out[2]=0x12; out[3]=0x7F; out[4]=0x10; break;
        case '5': out[0]=0x27; out[1]=0x45; out[2]=0x45; out[3]=0x45; out[4]=0x39; break;
        case '6': out[0]=0x3C; out[1]=0x4A; out[2]=0x49; out[3]=0x49; out[4]=0x30; break;
        case '7': out[0]=0x01; out[1]=0x71; out[2]=0x09; out[3]=0x05; out[4]=0x03; break;
        case '8': out[0]=0x36; out[1]=0x49; out[2]=0x49; out[3]=0x49; out[4]=0x36; break;
        case '9': out[0]=0x06; out[1]=0x49; out[2]=0x49; out[3]=0x29; out[4]=0x1E; break;
        case 'A': out[0]=0x7C; out[1]=0x12; out[2]=0x11; out[3]=0x12; out[4]=0x7C; break;
        case 'B': out[0]=0x7F; out[1]=0x49; out[2]=0x49; out[3]=0x49; out[4]=0x36; break;
        case 'C': out[0]=0x3E; out[1]=0x41; out[2]=0x41; out[3]=0x41; out[4]=0x22; break;
        case 'D': out[0]=0x7F; out[1]=0x41; out[2]=0x41; out[3]=0x22; out[4]=0x1C; break;
        case 'E': out[0]=0x7F; out[1]=0x49; out[2]=0x49; out[3]=0x49; out[4]=0x41; break;
        case 'F': out[0]=0x7F; out[1]=0x09; out[2]=0x09; out[3]=0x09; out[4]=0x01; break;
        case 'G': out[0]=0x3E; out[1]=0x41; out[2]=0x49; out[3]=0x49; out[4]=0x7A; break;
        case 'H': out[0]=0x7F; out[1]=0x08; out[2]=0x08; out[3]=0x08; out[4]=0x7F; break;
        case 'I': out[1]=0x41; out[2]=0x7F; out[3]=0x41; break;
        case 'J': out[0]=0x20; out[1]=0x40; out[2]=0x41; out[3]=0x3F; out[4]=0x01; break;
        case 'K': out[0]=0x7F; out[1]=0x08; out[2]=0x14; out[3]=0x22; out[4]=0x41; break;
        case 'L': out[0]=0x7F; out[1]=0x40; out[2]=0x40; out[3]=0x40; out[4]=0x40; break;
        case 'M': out[0]=0x7F; out[1]=0x02; out[2]=0x0C; out[3]=0x02; out[4]=0x7F; break;
        case 'N': out[0]=0x7F; out[1]=0x04; out[2]=0x08; out[3]=0x10; out[4]=0x7F; break;
        case 'O': out[0]=0x3E; out[1]=0x41; out[2]=0x41; out[3]=0x41; out[4]=0x3E; break;
        case 'P': out[0]=0x7F; out[1]=0x09; out[2]=0x09; out[3]=0x09; out[4]=0x06; break;
        case 'Q': out[0]=0x3E; out[1]=0x41; out[2]=0x51; out[3]=0x21; out[4]=0x5E; break;
        case 'R': out[0]=0x7F; out[1]=0x09; out[2]=0x19; out[3]=0x29; out[4]=0x46; break;
        case 'S': out[0]=0x46; out[1]=0x49; out[2]=0x49; out[3]=0x49; out[4]=0x31; break;
        case 'T': out[0]=0x01; out[1]=0x01; out[2]=0x7F; out[3]=0x01; out[4]=0x01; break;
        case 'U': out[0]=0x3F; out[1]=0x40; out[2]=0x40; out[3]=0x40; out[4]=0x3F; break;
        case 'V': out[0]=0x1F; out[1]=0x20; out[2]=0x40; out[3]=0x20; out[4]=0x1F; break;
        case 'W': out[0]=0x3F; out[1]=0x40; out[2]=0x38; out[3]=0x40; out[4]=0x3F; break;
        case 'X': out[0]=0x63; out[1]=0x14; out[2]=0x08; out[3]=0x14; out[4]=0x63; break;
        case 'Y': out[0]=0x07; out[1]=0x08; out[2]=0x70; out[3]=0x08; out[4]=0x07; break;
        case 'Z': out[0]=0x61; out[1]=0x51; out[2]=0x49; out[3]=0x45; out[4]=0x43; break;
        default: break;
    }
}

void Render2D::Begin() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (void* ctx = Window::GetActiveNativeContextState()) {
        GX2SetContextState(static_cast<GX2ContextState*>(ctx));
    }
    if (void* cb = Window::GetActiveNativeColorBuffer()) {
        GX2SetColorBuffer(static_cast<GX2ColorBuffer*>(cb), GX2_RENDER_TARGET_0);
    }
    Render2DGX2::Instance().Begin();
#else
    Render2DGL::Instance().Begin();
#endif
}

void Render2D::End() {
#if defined(__WIIU__) || defined(__wiiu__)
    Render2DGX2::Instance().End();
#else
    Render2DGL::Instance().End();
#endif
}

void Render2D::DrawRect(int x, int y, int w, int h, const ColorRGBA& color) {
#if defined(__WIIU__) || defined(__wiiu__)
    Render2DGX2::Instance().DrawRect(static_cast<float>(x), static_cast<float>(y),
                                     static_cast<float>(w), static_cast<float>(h), color);
#else
    Render2DGL::Instance().DrawRect(static_cast<float>(x), static_cast<float>(y),
                                    static_cast<float>(w), static_cast<float>(h), color);
#endif
}

void Render2D::DrawLine(int x0, int y0, int x1, int y1, const ColorRGBA& color, float width) {
#if defined(__WIIU__) || defined(__wiiu__)
    Render2DGX2::Instance().DrawLine(static_cast<float>(x0), static_cast<float>(y0),
                                     static_cast<float>(x1), static_cast<float>(y1),
                                     width, color);
#else
    Render2DGL::Instance().DrawLine(static_cast<float>(x0), static_cast<float>(y0),
                                    static_cast<float>(x1), static_cast<float>(y1),
                                    width, color);
#endif
}

void Render2D::DrawPixelChar(char c, int x, int y, int scale, const ColorRGBA& color) {
    char buf[2]{c, '\0'};
    DrawPixelString(std::string_view(buf, 1), x, y, scale, color);
}

void Render2D::DrawPixelString(std::string_view text, int x, int y, int scale, const ColorRGBA& color) {
    if (scale <= 0 || text.empty()) return;
#if defined(__WIIU__) || defined(__wiiu__)
    Render2DGX2::Instance().DrawText(text, static_cast<float>(x), static_cast<float>(y),
                                     static_cast<float>(scale), color);
#else
    Render2DGL::Instance().DrawText(text, static_cast<float>(x), static_cast<float>(y),
                                    static_cast<float>(scale), color);
#endif
}

void Render2D::DrawShrinkingRing(int cx, int cy, float radius, const ColorRGBA& color) {
    if (radius <= 1.0f) return;
    constexpr int SEGMENTS = 24;
    constexpr float TWO_PI = 2.0f * 3.14159265f;
    for (int i = 0; i < SEGMENTS; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(SEGMENTS)) * TWO_PI;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(SEGMENTS)) * TWO_PI;
        const float x0 = static_cast<float>(cx) + radius * std::cos(a0);
        const float y0 = static_cast<float>(cy) + radius * std::sin(a0);
        const float x1 = static_cast<float>(cx) + radius * std::cos(a1);
        const float y1 = static_cast<float>(cy) + radius * std::sin(a1);
#if defined(__WIIU__) || defined(__wiiu__)
        Render2DGX2::Instance().DrawLine(x0, y0, x1, y1, 2.0f, color);
#else
        Render2DGL::Instance().DrawLine(x0, y0, x1, y1, 2.0f, color);
#endif
    }
}

void Render2D::DrawBox(int x, int y, int w, int h, const ColorRGBA& fillColor, const ColorRGBA& borderColor) {
#if defined(__WIIU__) || defined(__wiiu__)
    Render2DGX2::Instance().DrawBox(static_cast<float>(x), static_cast<float>(y),
                                    static_cast<float>(w), static_cast<float>(h),
                                    fillColor, borderColor, 2.0f);
#else
    Render2DGL::Instance().DrawBox(static_cast<float>(x), static_cast<float>(y),
                                   static_cast<float>(w), static_cast<float>(h),
                                   fillColor, borderColor, 2.0f);
#endif
}

} // namespace Neleo
