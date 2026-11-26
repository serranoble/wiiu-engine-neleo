#include "Scene/SceneLoading.hpp"
#include "Scene/SceneManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/StackString.hpp"
#include <cmath>
#include <algorithm>

namespace Neleo {

SceneLoading::SceneLoading() {
    m_statusText.Format("INICIALIZANDO MOTOR GRAFICO 3D WII U C++20...");
}

void SceneLoading::OnEnter() {
    m_progress = 0.0f;
    m_targetProgress = 0.0f;
    m_pulseAnim = 0.0f;
    m_finishTimer = 0.0f;
    m_statusText.Format("INICIALIZANDO MOTOR GRAFICO Y AUDIO ESPRESSO...");

    // Iniciar la carga asíncrona continua de assets
    AudioManager::Instance().StartAsyncInit();
}

void SceneLoading::Update(float dt) {
    m_pulseAnim += 5.0f * dt;

    const bool isFinished = AudioManager::Instance().IsLoadingFinished();
    // Si la carga en segundo plano terminó, el target es deterministamente 1.0f (100%)
    m_targetProgress = isFinished ? 1.0f : AudioManager::Instance().GetProgress();

    // Interpolación exponencial exacta e independiente del frame-rate: 1 - exp(-gamma * dt)
    float alpha = 1.0f - std::exp(-12.0f * dt);
    m_progress += (m_targetProgress - m_progress) * alpha;

    // Actualizar texto descriptivo en vivo sin alocaciones heap (0 heap a 60 FPS)
    char labelBuf[128]{};
    AudioManager::Instance().CopyCurrentAssetLabel(labelBuf, sizeof(labelBuf));
    m_statusText.Format("%s", labelBuf);

    // Transición fluida y garantizada hacia el Menú Principal
    if (isFinished) {
        m_finishTimer += dt;
        if (m_progress >= 0.95f || m_finishTimer >= 0.35f) {
            m_progress = 1.0f;
            SceneManager::Instance().NavigateToHomeMenu();
        }
    }
}

void SceneLoading::Render() {
    Render2D::Begin();

    // 1. Fondo Oscuro Futurista
    Render2D::DrawBox(0, 0, 1280, 720, ColorRGBA{5, 10, 20, 255}, ColorRGBA::Cyan());

    // Grid visual de fondo
    const ColorRGBA gridColor{15, 35, 60, 120};
    for (int x = 0; x < 1280; x += 64) {
        Render2D::DrawLine(x, 0, x, 720, gridColor, 1.0f);
    }
    for (int y = 0; y < 720; y += 64) {
        Render2D::DrawLine(0, y, 1280, y, gridColor, 1.0f);
    }

    // 2. Título Principal
    Render2D::DrawPixelString("WIIU Demos by Neleo", 412, 175, 4, ColorRGBA::Cyan());
    Render2D::DrawPixelString("MOTOR GRAFICO 3D & COMPUTACION EN TIEMPO REAL", 370, 225, 2, ColorRGBA::Yellow());

    // 3. Contenedor de la Barra de Carga
    constexpr int barX = 200;
    constexpr int barY = 360;
    constexpr int barW = 880;
    constexpr int barH = 50;
    Render2D::DrawBox(barX, barY, barW, barH, ColorRGBA{10, 20, 35, 230}, ColorRGBA::Cyan());

    // Relleno de la Barra de Carga con efecto Neón
    int fillWidth = static_cast<int>((barW - 8) * std::clamp(m_progress, 0.0f, 1.0f));
    if (fillWidth > 0) {
        uint8_t glowAlpha = static_cast<uint8_t>(200 + 55.0f * std::sin(m_pulseAnim));
        Render2D::DrawBox(barX + 4, barY + 4, fillWidth, barH - 8,
                          ColorRGBA{0, 180, 240, glowAlpha}, ColorRGBA::White());
    }

    // Porcentaje Numérico en Vivo
    int percent = static_cast<int>(m_progress * 100.0f);
    StackString<32> percentStr;
    percentStr.Format("%d %%", percent);
    Render2D::DrawPixelString(percentStr.string_view(), barX + barW / 2 - 30, barY + 14, 3, ColorRGBA::White());

    // 4. Texto de Estado de Carga
    Render2D::DrawPixelString(m_statusText.string_view(), 200, 430, 2, ColorRGBA{0, 240, 255, 255});

    // 5. Estadísticas de Lectura Continua de Bytes
    size_t loadedBytes = AudioManager::Instance().GetBytesLoaded();
    size_t totalBytes  = AudioManager::Instance().GetTotalBytes();
    float loadedMB = static_cast<float>(loadedBytes) / (1024.0f * 1024.0f);
    float totalMB  = static_cast<float>(totalBytes) / (1024.0f * 1024.0f);

    StackString<128> statsStr;
    statsStr.Format("STREAMING PROGRESIVO: %.2f MB / %.2f MB (%zu KB LEIDOS)",
                    loadedMB, totalMB, loadedBytes / 1024);
    Render2D::DrawPixelString(statsStr.string_view(), 200, 465, 2, ColorRGBA::Yellow());

    // 6. Pie de Página Informativo
    Render2D::DrawPixelString("DESARROLLADO CON LIBWUT, GX2 Y SNDCORE2 PARA NINTENDO WII U", 230, 660, 2, ColorRGBA{150, 170, 200, 255});

    Render2D::End();
}

} // namespace Neleo
