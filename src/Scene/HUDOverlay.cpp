#include "Scene/HUDOverlay.hpp"
#include "Scene/SceneManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Core/StackString.hpp"
#include <string>

namespace WiiU {

void HUDOverlay::Render(SDL_Renderer* renderer, const InputManager& input, float fps) {
    SDL_Rect hudBg = { 30, 30, 740, 140 };
    SDL_SetRenderDrawColor(renderer, 10, 20, 35, 230);
    SDL_RenderFillRect(renderer, &hudBg);

    ColorRGBA borderColor = (input.GetFlashTimer() > 0.0f || input.IsButtonHeld())
                            ? ColorRGBA{0, 255, 120, 255} 
                            : ColorRGBA{0, 200, 240, 255};

    SDL_SetRenderDrawColor(renderer, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
    SDL_RenderDrawRect(renderer, &hudBg);

    SDL_Rect hudInner = { 31, 31, 738, 138 };
    SDL_RenderDrawRect(renderer, &hudInner);

    SDL_Rect ledBox = { 50, 48, 24, 24 };
    ColorRGBA ledColor = (input.GetFlashTimer() > 0.0f || input.IsButtonHeld())
                         ? ColorRGBA{0, 255, 120, 255} 
                         : ColorRGBA{30, 80, 140, 255};
    SDL_SetRenderDrawColor(renderer, ledColor.r, ledColor.g, ledColor.b, ledColor.a);
    SDL_RenderFillRect(renderer, &ledBox);

    const bool fpsLimited = SceneManager::Instance().IsFPSLimitEnabled();
    StackString<128> titleStr;
    titleStr.Format("CONTROL 1 & TACTIL | FPS: %d %s",
                    static_cast<int>(fps + 0.5f),
                    fpsLimited ? "(60 FPS VSYNC)" : "(UNCAPPED)");
    Render2D::drawPixelString(renderer, titleStr.string_view(), 86, 50, 2, ColorRGBA::Yellow());

    StackString<128> countStr;
    countStr.Format("PULSACIONES: %d", input.GetButtonPressCount());
    Render2D::drawPixelString(renderer, countStr.string_view(), 50, 84, 2, ColorRGBA{0, 230, 255, 255});

    StackString<128> lastStr;
    lastStr.Format("ULTIMA ENTRADA: %s", input.GetLastButtonName().c_str());
    Render2D::drawPixelString(renderer, lastStr.string_view(), 340, 84, 2, ColorRGBA::White());

    Render2D::drawPixelString(renderer, "TOCA Y ARRASTRA EL CUADRADO BLANCO EN PANTALLA", 50, 114, 2, ColorRGBA{180, 180, 180, 255});
}

} // namespace WiiU
