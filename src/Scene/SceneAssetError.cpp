#include "Scene/SceneAssetError.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include <cmath>
#include <algorithm>

namespace WiiU {

SceneAssetError::SceneAssetError(std::string targetSceneName, std::vector<std::string> missingAssets)
    : m_targetSceneName(std::move(targetSceneName)),
      m_missingAssets(std::move(missingAssets)),
      m_acceptButton(360, 560, 560, 50, "[ ACEPTAR Y VOLVER AL MENU ]", 3) {
    
    m_acceptButton.SetColors(
        ColorRGBA{170, 30, 30, 230},   // Normal Fill
        ColorRGBA{230, 50, 50, 245},   // Hover Fill
        ColorRGBA{255, 190, 0, 255},   // Normal Border
        ColorRGBA{255, 255, 255, 255}, // Hover Border
        ColorRGBA::White()             // Text Color
    );
}

void SceneAssetError::OnEnter() {
    m_animTime = 0.0f;
    AudioManager::Instance().PlayClick();
}

void SceneAssetError::Update(float dt) {
    m_animTime += dt;

    const auto& touch = InputManager::Instance().GetTouchState();

    if (m_acceptButton.Update(touch, "ACEPTAR ERROR RECURSOS") ||
        InputManager::Instance().WasActionTriggeredThisFrame()) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().NavigateToHomeMenu();
    }
}

void SceneAssetError::Render(SDL_Renderer* renderer) {
    // 1. Fondo Oscuro Atenuado
    Render2D::drawBox(renderer, 0, 0, 1280, 720, ColorRGBA{10, 14, 24, 255}, ColorRGBA{30, 45, 70, 255});

    // Cuadrícula sutil
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 20, 30, 45, 100);
    for (int x = 0; x < 1280; x += 64) {
        SDL_RenderDrawLine(renderer, x, 0, x, 720);
    }
    for (int y = 0; y < 720; y += 64) {
        SDL_RenderDrawLine(renderer, 0, y, 1280, y);
    }

    // 2. Ventana Modal Central (960 x 570)
    SDL_Rect modalRect = { 160, 60, 960, 570 };
    Render2D::drawBox(renderer, modalRect.x, modalRect.y, modalRect.w, modalRect.h,
                      ColorRGBA{18, 24, 38, 250}, ColorRGBA{240, 60, 60, 255});

    // 3. Banner Superior Rojo de Alerta
    SDL_Rect headerRect = { modalRect.x, modalRect.y, modalRect.w, 54 };
    Render2D::drawBox(renderer, headerRect.x, headerRect.y, headerRect.w, headerRect.h,
                      ColorRGBA{180, 25, 25, 240}, ColorRGBA{255, 80, 80, 255});
    Render2D::drawPixelString(renderer, "[!] ERROR: RECURSOS NO ENCONTRADOS", modalRect.x + 18, modalRect.y + 14, 3, ColorRGBA::White());

    // 4. Mensaje Principal
    Render2D::drawPixelString(renderer, "NO SE PUDO INICIAR LA ESCENA:", modalRect.x + 35, modalRect.y + 75, 2, ColorRGBA{200, 215, 230, 255});
    Render2D::drawPixelString(renderer, m_targetSceneName, modalRect.x + 35, modalRect.y + 105, 3, ColorRGBA::Yellow());

    Render2D::drawPixelString(renderer, "LOS SIGUIENTES ARCHIVOS NO FUERON ENCONTRADOS EN EL SISTEMA:",
                              modalRect.x + 35, modalRect.y + 155, 2, ColorRGBA::White());

    // 5. Caja de Lista de Assets Faltantes
    SDL_Rect listRect = { modalRect.x + 35, modalRect.y + 185, modalRect.w - 70, 210 };
    Render2D::drawBox(renderer, listRect.x, listRect.y, listRect.w, listRect.h,
                      ColorRGBA{8, 12, 20, 240}, ColorRGBA{200, 50, 50, 180});

    int lineY = listRect.y + 18;
    for (size_t i = 0; i < m_missingAssets.size() && i < 5; ++i) {
        std::string item = "[-] ASSET REQUERIDO: " + m_missingAssets[i];
        Render2D::drawPixelString(renderer, item, listRect.x + 20, lineY, 2, ColorRGBA{255, 110, 110, 255});
        lineY += 36;
    }

    if (m_missingAssets.size() > 5) {
        std::string moreText = "... Y " + std::to_string(m_missingAssets.size() - 5) + " ARCHIVOS ADICIONALES";
        Render2D::drawPixelString(renderer, moreText, listRect.x + 20, lineY, 2, ColorRGBA{255, 170, 170, 255});
    }

    // 6. Indicaciones de Solución
    Render2D::drawPixelString(renderer, "SOLUCION: VERIFICA QUE LOS ARCHIVOS ESTEN EN 'content/' O 'assets/'",
                              modalRect.x + 35, modalRect.y + 415, 2, ColorRGBA::Cyan());
    Render2D::drawPixelString(renderer, "JUNTO AL BINARIO DEL MOTOR (.WUHB / .RPX) Y RECOMPILA EL PROYECTO.",
                              modalRect.x + 35, modalRect.y + 445, 2, ColorRGBA{0, 210, 240, 255});

    // 7. Botón Interactivo de Aceptar
    m_acceptButton.Render(renderer);
}

} // namespace WiiU
