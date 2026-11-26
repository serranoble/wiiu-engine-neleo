#include "Scene/SceneAssetError.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Core/StackString.hpp"

namespace Neleo {

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

void SceneAssetError::Render() {
    Render2D::Begin();

    // 1. Fondo Oscuro Atenuado
    Render2D::DrawBox(0, 0, 1280, 720, ColorRGBA{10, 14, 24, 255}, ColorRGBA{30, 45, 70, 255});

    // Cuadrícula sutil
    const ColorRGBA gridColor{20, 30, 45, 100};
    for (int x = 0; x < 1280; x += 64) {
        Render2D::DrawLine(x, 0, x, 720, gridColor, 1.0f);
    }
    for (int y = 0; y < 720; y += 64) {
        Render2D::DrawLine(0, y, 1280, y, gridColor, 1.0f);
    }

    // 2. Ventana Modal Central (960 x 570)
    constexpr int modalX = 160;
    constexpr int modalY = 60;
    constexpr int modalW = 960;
    constexpr int modalH = 570;
    Render2D::DrawBox(modalX, modalY, modalW, modalH,
                      ColorRGBA{18, 24, 38, 250}, ColorRGBA{240, 60, 60, 255});

    // 3. Banner Superior Rojo de Alerta
    Render2D::DrawBox(modalX, modalY, modalW, 54,
                      ColorRGBA{180, 25, 25, 240}, ColorRGBA{255, 80, 80, 255});
    Render2D::DrawPixelString("[!] ERROR: RECURSOS NO ENCONTRADOS", modalX + 18, modalY + 14, 3, ColorRGBA::White());

    // 4. Mensaje Principal
    Render2D::DrawPixelString("NO SE PUDO INICIAR LA ESCENA:", modalX + 35, modalY + 75, 2, ColorRGBA{200, 215, 230, 255});
    Render2D::DrawPixelString(m_targetSceneName, modalX + 35, modalY + 105, 3, ColorRGBA::Yellow());

    Render2D::DrawPixelString("LOS SIGUIENTES ARCHIVOS NO FUERON ENCONTRADOS EN EL SISTEMA:",
                              modalX + 35, modalY + 155, 2, ColorRGBA::White());

    // 5. Caja de Lista de Assets Faltantes
    constexpr int listX = modalX + 35;
    constexpr int listY = modalY + 185;
    constexpr int listW = modalW - 70;
    constexpr int listH = 210;
    Render2D::DrawBox(listX, listY, listW, listH,
                      ColorRGBA{8, 12, 20, 240}, ColorRGBA{200, 50, 50, 180});

    int lineY = listY + 18;
    for (size_t i = 0; i < m_missingAssets.size() && i < 5; ++i) {
        StackString<128> item;
        item.Format("[-] ASSET REQUERIDO: %s", m_missingAssets[i].c_str());
        Render2D::DrawPixelString(item.string_view(), listX + 20, lineY, 2, ColorRGBA{255, 110, 110, 255});
        lineY += 36;
    }

    if (m_missingAssets.size() > 5) {
        StackString<128> moreText;
        moreText.Format("... Y %zu ARCHIVOS ADICIONALES", m_missingAssets.size() - 5);
        Render2D::DrawPixelString(moreText.string_view(), listX + 20, lineY, 2, ColorRGBA{255, 170, 170, 255});
    }

    // 6. Indicaciones de Solución
    Render2D::DrawPixelString("SOLUCION: VERIFICA QUE LOS ARCHIVOS ESTEN EN 'content/' O 'assets/'",
                              modalX + 35, modalY + 415, 2, ColorRGBA::Cyan());
    Render2D::DrawPixelString("JUNTO AL BINARIO DEL MOTOR (.WUHB / .RPX) Y RECOMPILA EL PROYECTO.",
                              modalX + 35, modalY + 445, 2, ColorRGBA{0, 210, 240, 255});

    // 7. Botón Interactivo de Aceptar
    m_acceptButton.Render();

    Render2D::End();
}

} // namespace Neleo
