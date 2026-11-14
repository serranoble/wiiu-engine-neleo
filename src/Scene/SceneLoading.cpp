#include "Scene/SceneLoading.hpp"
#include "Scene/SceneManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/StackString.hpp"
#include <cmath>
#include <algorithm>

namespace WiiU {

SceneLoading::SceneLoading()
    : m_statusText("INICIALIZANDO MOTOR GRAFICO 3D WII U C++20...") {}

void SceneLoading::OnEnter() {
    m_progress = 0.0f;
    m_targetProgress = 0.0f;
    m_pulseAnim = 0.0f;
    m_finishTimer = 0.0f;
    m_statusText = "INICIALIZANDO MOTOR GRAFICO Y AUDIO ESPRESSO...";

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

    // Actualizar texto descriptivo en vivo de forma thread-safe
    m_statusText = AudioManager::Instance().GetCurrentAssetLoading();

    // Transición fluida y garantizada hacia el Menú Principal
    if (isFinished) {
        m_finishTimer += dt;
        if (m_progress >= 0.95f || m_finishTimer >= 0.35f) {
            m_progress = 1.0f;
            SceneManager::Instance().NavigateToHomeMenu();
        }
    }
}

void SceneLoading::Render(SDL_Renderer* renderer) {
    // 1. Fondo Oscuro Futurista
    Render2D::drawBox(renderer, 0, 0, 1280, 720, ColorRGBA{5, 10, 20, 255}, ColorRGBA::Cyan());

    // Grid visual de fondo
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 15, 35, 60, 120);
    for (int x = 0; x < 1280; x += 64) {
        SDL_RenderDrawLine(renderer, x, 0, x, 720);
    }
    for (int y = 0; y < 720; y += 64) {
        SDL_RenderDrawLine(renderer, 0, y, 1280, y);
    }

    // 2. Título Principal
    Render2D::drawPixelString(renderer, "WIIU Demos by Neleo", 412, 175, 4, ColorRGBA::Cyan());
    Render2D::drawPixelString(renderer, "MOTOR GRAFICO 3D & COMPUTACION EN TIEMPO REAL", 370, 225, 2, ColorRGBA::Yellow());

    // 3. Contenedor de la Barra de Carga
    SDL_Rect barBox = { 200, 360, 880, 50 };
    Render2D::drawBox(renderer, barBox.x, barBox.y, barBox.w, barBox.h, ColorRGBA{10, 20, 35, 230}, ColorRGBA::Cyan());

    // Relleno de la Barra de Carga con efecto Neón
    int fillWidth = static_cast<int>((barBox.w - 8) * std::clamp(m_progress, 0.0f, 1.0f));
    if (fillWidth > 0) {
        SDL_Rect fillRect = { barBox.x + 4, barBox.y + 4, fillWidth, barBox.h - 8 };
        Uint8 glowAlpha = static_cast<Uint8>(200 + 55.0f * std::sin(m_pulseAnim));
        Render2D::drawBox(renderer, fillRect.x, fillRect.y, fillRect.w, fillRect.h, 
                          ColorRGBA{0, 180, 240, glowAlpha}, ColorRGBA::White());
    }

    // Porcentaje Numérico en Vivo
    int percent = static_cast<int>(m_progress * 100.0f);
    StackString<32> percentStr;
    percentStr.Format("%d %%", percent);
    Render2D::drawPixelString(renderer, percentStr.string_view(), barBox.x + barBox.w / 2 - 30, barBox.y + 14, 3, ColorRGBA::White());

    // 4. Texto de Estado de Carga
    Render2D::drawPixelString(renderer, m_statusText, 200, 430, 2, ColorRGBA{0, 240, 255, 255});

    // 5. Estadísticas de Lectura Continua de Bytes
    size_t loadedBytes = AudioManager::Instance().GetBytesLoaded();
    size_t totalBytes  = AudioManager::Instance().GetTotalBytes();
    float loadedMB = static_cast<float>(loadedBytes) / (1024.0f * 1024.0f);
    float totalMB  = static_cast<float>(totalBytes) / (1024.0f * 1024.0f);

    StackString<128> statsStr;
    statsStr.Format("STREAMING PROGRESIVO: %.2f MB / %.2f MB (%zu KB LEIDOS)", 
                    loadedMB, totalMB, loadedBytes / 1024);
    Render2D::drawPixelString(renderer, statsStr.string_view(), 200, 465, 2, ColorRGBA::Yellow());

    // 6. Pie de Página Informativo
    Render2D::drawPixelString(renderer, "DESARROLLADO CON LIBWUT, LIBUTILS Y SDL2 PARA NINTENDO WII U", 250, 660, 2, ColorRGBA{150, 170, 200, 255});
}

} // namespace WiiU
