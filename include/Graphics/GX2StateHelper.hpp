#pragma once

#include <SDL2/SDL.h>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/state.h>
#include <gx2/clear.h>
#include <gx2/draw.h>
#include <gx2/registers.h>
#include <gx2/context.h>
#include <gx2/enum.h>
#include <cstdint>

// Símbolo global exportado en libSDL2.a (SDL_shaders_wiiu.o)
extern "C" void WIIU_SDL_SelectShader(int shader);

namespace WiiU {

/**
 * @brief Estructura interna de dibujo del driver SDL2 para Wii U (mapeo validado con DWARF de SDL_render_wiiu).
 */
struct WIIU_DrawState_Internal {
    void* target;
    void* texture;
    SDL_Rect viewport;
    int viewportDirty;
    int drawableWidth;
    int drawableHeight;
    float projectionMatrix[16];
    int cliprectEnabled;
    int cliprectDirty;
    SDL_Rect cliprect;
    int blendMode;
    int shader;
};

/**
 * @brief Estructura interna de datos de renderizado de SDL2 Wii U.
 */
struct WIIU_RenderData_Internal {
    GX2ContextState* ctx;
    uint8_t padding[124];
    WIIU_DrawState_Internal drawState;
};

/**
 * @brief Restaura el pipeline de hardware GX2 al estado esperado por el backend 2D de SDL2.
 * 
 * Tras ejecutar shaders o comandos GX2 nativos directos (GX2RDrawIndexed, GX2DrawEx, etc.),
 * los registros de GPU Latte quedan vinculados a los shaders y búferes 3D de la escena.
 * Esta función:
 * 1. Desactiva el Depth Test y Depth Write para asegurar el dibujado 2D del HUD.
 * 2. Restaura el GX2ContextState original de SDL2.
 * 3. Invalida la caché interna de estados y shaders en el driverdata de SDL2.
 * 4. Re-vincula físicamente el shader de color 2D (colorShader) mediante WIIU_SDL_SelectShader(0).
 * 5. Envía un GX2Flush() sincronizado.
 */
inline void RestoreSDL2GX2State(SDL_Renderer* renderer) {
    if (!renderer) return;

    // Desactivar Z-Buffer para primitivas 2D superpuestas
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);

    // Acceder a driverdata en offset 592 de SDL_Renderer
    void** rendererDriverDataPtr = reinterpret_cast<void**>(reinterpret_cast<char*>(renderer) + 592);
    if (rendererDriverDataPtr && *rendererDriverDataPtr) {
        auto* renderData = static_cast<WIIU_RenderData_Internal*>(*rendererDriverDataPtr);
        if (renderData) {
            if (renderData->ctx) {
                GX2SetContextState(renderData->ctx);
            }

            // Forzar actualización de estados en el command queue de SDL2
            renderData->drawState.viewportDirty = 1;
            renderData->drawState.cliprectDirty = 1;
            renderData->drawState.blendMode = -1;
            renderData->drawState.shader = -1; // SHADER_INVALID

            // Re-vincular de inmediato colorShader (índice 0)
            WIIU_SDL_SelectShader(0);
        }
    }

    GX2Flush();
}

/**
 * @brief Obtiene el GX2ColorBuffer principal creado por el backend de SDL2 para Wii U.
 * 
 * En SDL2 Wii U:
 * - renderer->driverdata (offset 592) apunta a WIIU_RenderData.
 * - renderData->windowTex (offset 112 / 0x70) apunta a la estructura WIIU_TextureData.
 * - windowTex + 168 (0xA8) es la estructura GX2ColorBuffer que SDL_RenderPresent blitea al TV y DRC.
 */
inline GX2ColorBuffer* GetSDL2ColorBuffer(SDL_Renderer* renderer) {
    if (!renderer) return nullptr;

    void** rendererDriverDataPtr = reinterpret_cast<void**>(reinterpret_cast<char*>(renderer) + 592);
    if (!rendererDriverDataPtr || !*rendererDriverDataPtr) return nullptr;

    char* renderData = reinterpret_cast<char*>(*rendererDriverDataPtr);
    void** windowTexPtr = reinterpret_cast<void**>(renderData + 112);
    if (!windowTexPtr || !*windowTexPtr) return nullptr;

    char* windowTex = reinterpret_cast<char*>(*windowTexPtr);
    return reinterpret_cast<GX2ColorBuffer*>(windowTex + 168);
}

} // namespace WiiU

#else

namespace WiiU {
inline void RestoreSDL2GX2State(SDL_Renderer* /*renderer*/) {}
inline void* GetSDL2ColorBuffer(SDL_Renderer* /*renderer*/) { return nullptr; }
} // namespace WiiU

#endif
