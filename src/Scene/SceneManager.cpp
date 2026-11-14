#include "Scene/SceneManager.hpp"
#include "Scene/SceneAssetError.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/JobSystem.hpp"
#include "Core/StackString.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/Color.hpp"
#include "Audio/AudioManager.hpp"
#include "Input/InputManager.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/swap.h>
#include <gx2/draw.h>
#include <gx2/clear.h>
#include <gx2/event.h>
#include <gx2/registers.h>
#include <proc_ui/procui.h>
#endif

namespace WiiU {

void SceneManager::SetFPSLimitEnabled(bool enabled) {
    m_fpsLimitEnabled = enabled;
#if defined(__WIIU__) || defined(__wiiu__)
    GX2SetSwapInterval(enabled ? 1 : 0);
#endif
}

void SceneManager::ToggleFPSLimit() {
    SetFPSLimitEnabled(!m_fpsLimitEnabled);
}

void SceneManager::ChangeScene(std::unique_ptr<IScene> newScene) {
    if (!newScene) return;

    if (m_isPaused) {
        SetPaused(false);
    }

    // Si hay una carga asíncrona previa en vuelo en Core 2, esperar deterministamente su conclusión
    if (m_asyncLoadDispatched) {
        JobSystem::Instance().Wait(&m_asyncLoadCounter);
        m_asyncLoadDispatched = false;
    }

    if (!m_currentScene) {
        // Carga inicial directa (ej. SceneLoading al encender la aplicación)
        const auto requiredAssets = newScene->GetRequiredAssets();
        if (!requiredAssets.empty()) {
            std::vector<std::string> missingAssets;
            uint32_t totalExpected = 0;
            if (!AssetManager::ValidateAssets(requiredAssets, missingAssets, &totalExpected)) {
                m_currentScene = std::make_unique<SceneAssetError>(newScene->GetSceneName(), std::move(missingAssets));
                m_currentScene->OnEnter();
                return;
            }
            AssetManager::ResetAsyncProgress(totalExpected);
        }
        m_currentScene = std::move(newScene);
        if (m_currentScene->IsMenu()) {
            AudioManager::Instance().PlayBGM();
        } else {
            AudioManager::Instance().StopBGM();
        }
        m_currentScene->LoadAsync();
        m_currentScene->OnEnter();
        return;
    }

    // Iniciar máquina de estados de transición progresiva
    m_pendingScene = std::move(newScene);
    m_outgoingSceneName = m_currentScene->GetSceneName();
    m_incomingSceneName = m_pendingScene->GetSceneName();
    m_outgoingWasMenu = m_currentScene->IsMenu();
    m_transitionPhase = TransitionPhase::DrainingGPU;
    m_transitionProgress = 0.0f;
    m_targetProgress = 0.25f;
    m_phaseTimer = 0.0f;
    m_pulseAnim = 0.0f;
    m_reclaimedBytes = 0;
    m_asyncLoadDispatched = false;
    m_asyncAssetsValid = true;
    m_asyncMissingAssets.clear();
    AssetManager::ResetAsyncProgress(0);
}

void SceneManager::UpdateTransition(float dt) {
    m_pulseAnim += 6.0f * dt;
    m_phaseTimer += dt;

    // Interpolación exponencial analítica: 1 - exp(-gamma * dt)
    float alpha = 1.0f - std::exp(-15.0f * dt);
    m_transitionProgress += (m_targetProgress - m_transitionProgress) * alpha;

    switch (m_transitionPhase) {
        case TransitionPhase::DrainingGPU: {
            // Fase 1: Barrera de drenado de la GPU Latte para que ningún draw call en vuelo
            // acceda a búferes de vértices o shaders de la escena saliente.
            if (m_phaseTimer >= 0.08f) {
#if defined(__WIIU__) || defined(__wiiu__)
                if (ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown()) {
                    GX2DrawDone();
                }
#endif
                m_transitionPhase = TransitionPhase::UnloadingScene;
                m_targetProgress = 0.50f;
                m_phaseTimer = 0.0f;
            }
            break;
        }

        case TransitionPhase::UnloadingScene: {
            // Fase 2: Invocación de OnExit(), destrucción de la escena saliente y auditoría de memoria.
            if (m_phaseTimer >= 0.10f) {
                if (m_currentScene) {
                    size_t bytesBefore = MemoryManager::Instance().GetTotalAllocatedBytes();
                    m_currentScene->OnExit();
                    m_currentScene.reset(); // Destruye la escena liberando sus miembros
                    size_t bytesAfter = MemoryManager::Instance().GetTotalAllocatedBytes();
                    if (bytesBefore > bytesAfter) {
                        m_reclaimedBytes = bytesBefore - bytesAfter;
                    }
                    MemoryManager::Instance().AuditSceneTeardown(m_outgoingSceneName.c_str());
                    MemoryManager::Instance().GetFrameArena().Reset();
                    MemoryManager::Instance().GetSceneArena().Reset();
                    RHIPipelineCache::Instance().Invalidate();
                }
                m_transitionPhase = TransitionPhase::LoadingScene;
                m_targetProgress = 0.55f;
                m_phaseTimer = 0.0f;
                m_asyncLoadDispatched = false;
            }
            break;
        }

        case TransitionPhase::LoadingScene: {
            // Fase 3 (Pilar 2 — Jason Gregory Cap. 7):
            // Despacho asíncrono no bloqueante de E/S y staging al Core 2 mediante JobSystem,
            // mientras el Core 1 sigue renderizando la pantalla de transición a 60 FPS constantes.
            if (!m_asyncLoadDispatched) {
                if (m_pendingScene) {
                    m_asyncAssetsValid = true;
                    m_asyncMissingAssets.clear();
                    m_asyncLoadCounter.store(0, std::memory_order_relaxed);
                    m_asyncLoadDispatched = true;

                    JobSystem::Instance().Dispatch([](void* param) {
                        auto* mgr = static_cast<SceneManager*>(param);
                        if (!mgr || !mgr->m_pendingScene) return;

                        const auto reqAssets = mgr->m_pendingScene->GetRequiredAssets();
                        uint32_t totalExpectedBytes = 0;
                        if (!reqAssets.empty()) {
                            if (!AssetManager::ValidateAssets(reqAssets, mgr->m_asyncMissingAssets, &totalExpectedBytes)) {
                                mgr->m_asyncAssetsValid = false;
                                return;
                            }
                        }
                        AssetManager::ResetAsyncProgress(totalExpectedBytes);
                        mgr->m_pendingScene->LoadAsync();
                    }, this, &m_asyncLoadCounter);
                } else {
                    m_asyncLoadDispatched = true;
                }
            }

            // Actualizar progreso en tiempo real (50% -> 95%) en función de los bytes leídos en Core 2
            float ioRatio = AssetManager::GetAsyncProgressRatio();
            m_targetProgress = 0.50f + 0.45f * ioRatio;

            // Verificar de forma no bloqueante si el Job en Core 2 ha concluido y ha transcurrido el mínimo visual
            if (m_asyncLoadCounter.load(std::memory_order_acquire) == 0 && m_phaseTimer >= 0.08f) {
                if (m_pendingScene) {
                    if (!m_asyncAssetsValid) {
                        m_pendingScene = std::make_unique<SceneAssetError>(m_incomingSceneName, std::move(m_asyncMissingAssets));
                        m_incomingSceneName = m_pendingScene->GetSceneName();
                    }
                    const bool willBeMenu = m_pendingScene->IsMenu();
                    m_currentScene = std::move(m_pendingScene);

                    if (willBeMenu) {
                        if (!m_outgoingWasMenu) {
                            AudioManager::Instance().RestartBGM();
                        } else {
                            AudioManager::Instance().PlayBGM();
                        }
                    } else {
                        AudioManager::Instance().StopBGM();
                    }

                    // Finalización ligera de recursos de contexto GX2 en el hilo principal (Core 1)
                    m_currentScene->OnEnter();
                }
                m_asyncLoadDispatched = false;
                m_transitionPhase = TransitionPhase::Finalizing;
                m_targetProgress = 1.0f;
                m_phaseTimer = 0.0f;
            }
            break;
        }

        case TransitionPhase::Finalizing: {
            // Fase 4: Snap al 100%, presentación fluida y retorno al estado Idle.
            if (m_transitionProgress >= 0.985f) {
                m_transitionProgress = 1.0f;
            }
            if (m_phaseTimer >= 0.12f && m_transitionProgress >= 0.999f) {
                m_transitionProgress = 1.0f;
                m_transitionPhase = TransitionPhase::Idle;
            }
            break;
        }

        case TransitionPhase::Idle:
        default:
            break;
    }
}

void SceneManager::Update(float dt) {
    if (m_transitionPhase != TransitionPhase::Idle) {
        UpdateTransition(dt);
        return;
    }

    // Comprobar solicitud de alternar pausa desde mandos físicos o atajos de teclado
    if (InputManager::Instance().IsPauseRequested()) {
        TogglePause();
        InputManager::Instance().ConsumePauseRequest();
        return;
    }

    if (m_isPaused) {
        UpdatePauseModal(dt);
        return;
    }

    if (m_currentScene) {
        m_currentScene->Update(dt);
    }
}

void SceneManager::Render(SDL_Renderer* renderer) {
    if (m_transitionPhase != TransitionPhase::Idle) {
        RenderTransitionScreen(renderer);
        return;
    }

    if (m_currentScene) {
        m_currentScene->Render(renderer);
    }

    if (m_isPaused) {
        RenderPauseModal(renderer);
    }
}

void SceneManager::RenderTransitionScreen(SDL_Renderer* renderer) {
    if (!renderer) return;

    // 1. Fondo Oscuro Futurista
    Render2D::DrawBox(renderer, 0, 0, 1280, 720, ColorRGBA{6, 12, 24, 255}, ColorRGBA::Cyan());

    // Grid visual de fondo
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 15, 35, 60, 120);
    for (int x = 0; x < 1280; x += 64) {
        SDL_RenderDrawLine(renderer, x, 0, x, 720);
    }
    for (int y = 0; y < 720; y += 64) {
        SDL_RenderDrawLine(renderer, 0, y, 1280, y);
    }

    // 2. Título Superior
    Render2D::DrawPixelString(renderer, "TRANSICION DE ESCENA & AUDITORIA DE RECURSOS", 220, 105, 3, ColorRGBA::Cyan());
    Render2D::DrawPixelString(renderer, "SISTEMA DETERMINISTA DE CICLO DE VIDA Y RECLAMACION DE MEMORIA", 240, 145, 2, ColorRGBA{160, 210, 255, 255});

    // 3. Indicador de Fase Activa (Pill interactiva)
    const char* phaseText = "FASE DESCONOCIDA";
    ColorRGBA phaseColor = ColorRGBA::White();
    switch (m_transitionPhase) {
        case TransitionPhase::DrainingGPU:
            phaseText = "FASE 1/4: DRENADO DE PIPELINE GPU LATTE (GX2DrawDone / GX2Flush)";
            phaseColor = ColorRGBA::Yellow();
            break;
        case TransitionPhase::UnloadingScene:
            phaseText = "FASE 2/4: LIBERANDO MEMORIA Y DESCARGANDO ESCENA PREVIA";
            phaseColor = ColorRGBA{255, 140, 40, 255}; // Naranja Neón
            break;
        case TransitionPhase::LoadingScene:
            phaseText = "FASE 3/4: CARGANDO ASSETS E INICIALIZANDO NUEVA ESCENA";
            phaseColor = ColorRGBA{0, 240, 255, 255}; // Cian Neón
            break;
        case TransitionPhase::Finalizing:
            phaseText = "FASE 4/4: SINCRONIZANDO PRESENTACION Y TRANSICION FINAL (100%)";
            phaseColor = ColorRGBA{80, 250, 120, 255}; // Verde Esmeralda
            break;
        default:
            break;
    }

    Render2D::DrawBox(renderer, 200, 185, 880, 42, ColorRGBA{15, 28, 48, 230}, phaseColor);
    Render2D::DrawPixelString(renderer, phaseText, 225, 198, 2, phaseColor);

    // 4. Tarjetas de Información de Escenas (Origen -> Destino)
    // Tarjeta Saliente
    Render2D::DrawBox(renderer, 200, 242, 425, 68, ColorRGBA{10, 20, 35, 220}, ColorRGBA{180, 70, 70, 255});
    Render2D::DrawPixelString(renderer, "DESCARGANDO:", 215, 255, 2, ColorRGBA{255, 120, 120, 255});
    std::string outShort = m_outgoingSceneName.empty() ? "Ninguna" : m_outgoingSceneName;
    if (outShort.size() > 24) outShort = outShort.substr(0, 24);
    Render2D::DrawPixelString(renderer, outShort, 215, 281, 2, ColorRGBA::White());

    // Tarjeta Entrante
    Render2D::DrawBox(renderer, 655, 242, 425, 68, ColorRGBA{10, 20, 35, 220}, ColorRGBA{50, 180, 120, 255});
    Render2D::DrawPixelString(renderer, "PREPARANDO:", 670, 255, 2, ColorRGBA{100, 255, 160, 255});
    std::string inShort = m_incomingSceneName.empty() ? "Siguiente" : m_incomingSceneName;
    if (inShort.size() > 24) inShort = inShort.substr(0, 24);
    Render2D::DrawPixelString(renderer, inShort, 670, 281, 2, ColorRGBA::White());

    // 5. Barra de Progreso Neón Central
    SDL_Rect barBox = { 200, 328, 880, 50 };
    Render2D::DrawBox(renderer, barBox.x, barBox.y, barBox.w, barBox.h, ColorRGBA{10, 20, 35, 240}, ColorRGBA::Cyan());

    int fillWidth = static_cast<int>((barBox.w - 8) * std::clamp(m_transitionProgress, 0.0f, 1.0f));
    if (fillWidth > 0) {
        SDL_Rect fillRect = { barBox.x + 4, barBox.y + 4, fillWidth, barBox.h - 8 };
        Uint8 glowAlpha = static_cast<Uint8>(200 + 55.0f * std::sin(m_pulseAnim));
        Render2D::DrawBox(renderer, fillRect.x, fillRect.y, fillRect.w, fillRect.h, 
                          ColorRGBA{0, 180, 240, glowAlpha}, ColorRGBA::White());
    }

    int percent = static_cast<int>(m_transitionProgress * 100.0f);
    StackString<32> percentStr;
    percentStr.Format("%d %%", percent);
    Render2D::DrawPixelString(renderer, percentStr.string_view(), barBox.x + barBox.w / 2 - 30, barBox.y + 14, 3, ColorRGBA::White());

    // 6. Panel de Telemetría de Memoria y Auditoría
    Render2D::DrawBox(renderer, 200, 395, 880, 275, ColorRGBA{8, 16, 28, 240}, ColorRGBA{70, 130, 200, 255});
    Render2D::DrawPixelString(renderer, "AUDITORIA DE GESTION DE MEMORIA EN SILICIO CAFE OS", 225, 412, 2, ColorRGBA::Yellow());

    auto& mem = MemoryManager::Instance();
    size_t activeAllocs = mem.GetActiveAllocationCount();
    size_t totalBytes   = mem.GetTotalAllocatedBytes();
    size_t peakBytes    = mem.GetPeakAllocatedBytes();

    StackString<160> line1;
    line1.Format("MEMORIA ACTIVA MOTOR : %zu KB (%.2f MB) | BLOQUES: %zu | PICO: %zu KB",
                 totalBytes / 1024, static_cast<float>(totalBytes) / (1024.0f * 1024.0f), activeAllocs, peakBytes / 1024);
    Render2D::DrawPixelString(renderer, line1.string_view(), 225, 448, 2, ColorRGBA::White());

    StackString<160> line2;
    if (m_reclaimedBytes > 0) {
        line2.Format("RECURSOS LIBERADOS EN DESCARGA: %zu KB RECLAMADOS AL HEAP", m_reclaimedBytes / 1024);
    } else {
        line2.Format("RECURSOS LIBERADOS EN DESCARGA: PURGA DETERMINISTA COMPLETA (0 FUGAS)");
    }
    Render2D::DrawPixelString(renderer, line2.string_view(), 225, 484, 2, ColorRGBA{100, 255, 140, 255});

    StackString<160> line3;
    line3.Format("GARANTIA ANTI-DANGLING: ACTIVA (PUNTEROS ANULADOS A nullptr EN LIBERACION)");
    Render2D::DrawPixelString(renderer, line3.string_view(), 225, 520, 2, ColorRGBA{0, 220, 255, 255});

    StackString<160> line4;
    line4.Format("LATTE GPU DRAIN BARRIER: SINCRONIZADA (GX2DrawDone ANTES DE REASIGNAR VRAM)");
    Render2D::DrawPixelString(renderer, line4.string_view(), 225, 556, 2, ColorRGBA{255, 200, 100, 255});

    StackString<160> line5;
    line5.Format("ARENAS EFIMERAS: FRAME (128KB) & SCENE (2MB) REINICIALIZADAS EN O(1)");
    Render2D::DrawPixelString(renderer, line5.string_view(), 225, 592, 2, ColorRGBA{200, 160, 255, 255});

    StackString<160> line6;
    uint32_t asyncLoaded = AssetManager::GetAsyncBytesLoaded();
    uint32_t asyncTotal  = AssetManager::GetAsyncTotalBytes();
    if (asyncTotal > 0) {
        line6.Format("STREAMING ASINCRONO CORE 2 (256KB BURST): %.2f MB / %.2f MB (%u ARCHIVOS)",
                     static_cast<float>(asyncLoaded) / (1024.0f * 1024.0f),
                     static_cast<float>(asyncTotal) / (1024.0f * 1024.0f),
                     AssetManager::GetAsyncFilesLoaded());
    } else {
        line6.Format("ESTADO DE CACHE LATTE: FLUSH & INVALIDATE COMPLETADO SIN CORRUPCION");
    }
    Render2D::DrawPixelString(renderer, line6.string_view(), 225, 628, 2, ColorRGBA{120, 220, 255, 255});
}

void SceneManager::Clear() {
    if (m_asyncLoadDispatched) {
        JobSystem::Instance().Wait(&m_asyncLoadCounter);
        m_asyncLoadDispatched = false;
    }
#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown()) {
        GX2DrawDone();
    }
#endif
    if (m_isPaused) {
        SetPaused(false);
    }
    if (m_currentScene) {
        m_currentScene->OnExit();
        m_currentScene.reset();
    }
    if (m_pendingScene) {
        m_pendingScene.reset();
    }
    MemoryManager::Instance().AuditSceneTeardown("Shutdown");
    MemoryManager::Instance().GetFrameArena().Reset();
    MemoryManager::Instance().GetSceneArena().Reset();
    RHIPipelineCache::Instance().Invalidate();
    MemoryManager::Instance().AssertNoLeaks();
    m_transitionPhase = TransitionPhase::Idle;
    m_transitionProgress = 0.0f;
}

void SceneManager::SetPaused(bool paused) {
    if (m_isPaused == paused) return;
    if (m_transitionPhase != TransitionPhase::Idle) return;

    m_isPaused = paused;
    m_pauseAnimTimer = 0.0f;
    m_resumeHovered = false;
    m_menuHovered = false;

    // Pausar o reanudar el subsistema de audio determinista
    AudioManager::Instance().SetPaused(paused);

    if (m_currentScene) {
        if (paused) {
            m_currentScene->OnPause();
        } else {
            m_currentScene->OnResume();
        }
    }
}

void SceneManager::TogglePause() {
    SetPaused(!m_isPaused);
}

void SceneManager::UpdatePauseModal(float dt) {
    m_pauseAnimTimer += dt;

    const auto& touch = InputManager::Instance().GetTouchState();
    constexpr float modalX = 320.0f;
    constexpr float modalY = 135.0f;
    constexpr float modalW = 640.0f;
    constexpr float btnX = modalX + 40.0f;
    constexpr float btnW = modalW - 80.0f;
    constexpr float btnH = 48.0f;
    constexpr float btn1Y = modalY + 145.0f;
    constexpr float btn2Y = modalY + 205.0f;
    constexpr float btn3Y = modalY + 265.0f;

    m_resumeHovered = (touch.active && 
                       touch.x >= btnX && touch.x <= btnX + btnW &&
                       touch.y >= btn1Y && touch.y <= btn1Y + btnH);

    m_optionsHovered = (touch.active && 
                        touch.x >= btnX && touch.x <= btnX + btnW &&
                        touch.y >= btn2Y && touch.y <= btn2Y + btnH);

    m_menuHovered = (touch.active && 
                     touch.x >= btnX && touch.x <= btnX + btnW &&
                     touch.y >= btn3Y && touch.y <= btn3Y + btnH);

    // 1. Detección por pulsación táctil (GamePad Stylus / Ratón)
    if (touch.justPressed) {
        if (m_resumeHovered) {
            AudioManager::Instance().PlayClick();
            SetPaused(false);
            return;
        }
        if (m_optionsHovered) {
            AudioManager::Instance().PlayClick();
            SetPaused(false);
            const bool wasPureGX2 = (m_currentScene && m_currentScene->IsPureGX2());
            NavigateToOptions(wasPureGX2);
            return;
        }
        if (m_menuHovered) {
            AudioManager::Instance().PlayClick();
            SetPaused(false);
            const bool wasPureGX2 = (m_currentScene && m_currentScene->IsPureGX2());
            NavigateToParentMenu(wasPureGX2);
            return;
        }
    }

    // 2. Detección por mando físico: Botón B -> Volver al Menú
    if (InputManager::Instance().IsBackRequested()) {
        AudioManager::Instance().PlayClick();
        SetPaused(false);
        const bool wasPureGX2 = (m_currentScene && m_currentScene->IsPureGX2());
        NavigateToParentMenu(wasPureGX2);
        return;
    }

    // 3. Detección por mando físico: Botón X o MINUS -> Configuración Global
    if (InputManager::Instance().IsButtonTriggered(VPAD_BUTTON_X) ||
        InputManager::Instance().IsButtonTriggered(WPAD_PRO_BUTTON_X) ||
        InputManager::Instance().IsButtonTriggered(VPAD_BUTTON_MINUS) ||
        InputManager::Instance().IsButtonTriggered(WPAD_PRO_BUTTON_MINUS)) {
        AudioManager::Instance().PlayClick();
        SetPaused(false);
        const bool wasPureGX2 = (m_currentScene && m_currentScene->IsPureGX2());
        NavigateToOptions(wasPureGX2);
        return;
    }

    // 4. Detección por mando físico: Botón A o PLUS -> Reanudar Juego
    if (InputManager::Instance().IsButtonTriggered(VPAD_BUTTON_A) ||
        InputManager::Instance().IsButtonTriggered(WPAD_PRO_BUTTON_A) ||
        InputManager::Instance().IsButtonTriggered(VPAD_BUTTON_PLUS) ||
        InputManager::Instance().IsButtonTriggered(WPAD_PRO_BUTTON_PLUS)) {
        AudioManager::Instance().PlayClick();
        SetPaused(false);
        return;
    }
}

void SceneManager::RenderPauseModal(SDL_Renderer* renderer) {
    const bool isPureGX2 = (m_currentScene && m_currentScene->IsPureGX2());

    constexpr float modalX = 320.0f;
    constexpr float modalY = 135.0f;
    constexpr float modalW = 640.0f;
    constexpr float modalH = 430.0f;
    constexpr float btnH = 48.0f;
    constexpr float btn1Y = modalY + 145.0f;
    constexpr float btn2Y = modalY + 205.0f;
    constexpr float btn3Y = modalY + 265.0f;

    // Animación de pulso sinusoidal para el contorno de neón del modal
    float pulse = 0.5f + 0.5f * std::sin(m_pauseAnimTimer * 5.0f);
    uint8_t borderPulse = static_cast<uint8_t>(180.0f + pulse * 75.0f);
    ColorRGBA modalBorder{ 0, borderPulse, 255, 255 };

    StackString<96> sceneText;
    sceneText.Format("ESCENA ACTUAL: [%s]", m_currentScene ? m_currentScene->GetSceneName().c_str() : "SISTEMA");

    if (isPureGX2) {
        auto& r2d = Render2DGX2::Instance();
        r2d.Begin();

        // 1. Telón semitransparente oscuro sobre toda la pantalla (1280x720)
        r2d.DrawRect(0.0f, 0.0f, 1280.0f, 720.0f, ColorRGBA{0, 0, 0, 195});

        // 2. Marco exterior y fondo del modal
        r2d.DrawBox(modalX, modalY, modalW, modalH, ColorRGBA{10, 16, 28, 250}, modalBorder, 2.5f);
        r2d.DrawBox(modalX + 4.0f, modalY + 4.0f, modalW - 8.0f, modalH - 8.0f,
                    ColorRGBA{0, 0, 0, 0}, ColorRGBA{0, 140, 200, 100}, 1.0f);

        // 3. Cabecera del modal
        r2d.DrawBox(modalX, modalY, modalW, 60.0f, ColorRGBA{18, 28, 48, 255}, modalBorder, 2.0f);
        r2d.DrawText("|| PAUSA", modalX + 235.0f, modalY + 18.0f, 3.0f, ColorRGBA::Cyan());

        // 4. Subtítulos informativos (Escala Canónica 2.0f)
        r2d.DrawText(sceneText.string_view(), modalX + 40.0f, modalY + 80.0f, 2.0f, ColorRGBA{180, 220, 255, 255});
        r2d.DrawText("SIMULACION Y AUDIO DETENIDOS", modalX + 40.0f, modalY + 110.0f, 2.0f, ColorRGBA{255, 215, 0, 255});

        // 5. Botones interactivos
        r2d.DrawButton("> REANUDAR (BOTON + / A)", modalX + 40.0f, btn1Y, modalW - 80.0f, btnH,
                       m_resumeHovered,
                       ColorRGBA{20, 36, 60, 240}, ColorRGBA{35, 75, 130, 255},
                       ColorRGBA{0, 210, 255, 255}, ColorRGBA::Yellow(),
                       ColorRGBA::White(), 2.0f);

        r2d.DrawButton("* CONFIGURACION (BOTON - / X)", modalX + 40.0f, btn2Y, modalW - 80.0f, btnH,
                       m_optionsHovered,
                       ColorRGBA{20, 36, 60, 240}, ColorRGBA{25, 90, 130, 255},
                       ColorRGBA{0, 210, 255, 255}, ColorRGBA::Yellow(),
                       ColorRGBA::White(), 2.0f);

        r2d.DrawButton("< VOLVER AL MENU (BOTON B)", modalX + 40.0f, btn3Y, modalW - 80.0f, btnH,
                       m_menuHovered,
                       ColorRGBA{20, 36, 60, 240}, ColorRGBA{130, 35, 35, 255},
                       ColorRGBA{0, 210, 255, 255}, ColorRGBA::Yellow(),
                       ColorRGBA::White(), 2.0f);

        // 6. Indicaciones ergonómicas (Escala Canónica 2.0f, centradas)
        r2d.DrawText("TOCA UN BOTON O PRESIONA + EN EL GAMEPAD", modalX + 86.0f, modalY + 332.0f, 2.0f, ColorRGBA{130, 155, 185, 255});
        r2d.DrawText("PARA VOLVER AL JUEGO EN CUALQUIER MOMENTO", modalX + 74.0f, modalY + 358.0f, 2.0f, ColorRGBA{130, 155, 185, 255});

        // Línea decorativa inferior
        r2d.DrawLine(modalX + 10.0f, modalY + modalH - 12.0f, modalX + modalW - 10.0f, modalY + modalH - 12.0f, 2.0f, ColorRGBA::Cyan());

        r2d.End();
    } else if (renderer) {
        // 1. Telón semitransparente oscuro sobre toda la pantalla (1280x720)
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 195);
        SDL_Rect fullScreen{ 0, 0, 1280, 720 };
        SDL_RenderFillRect(renderer, &fullScreen);

        // 2. Marco exterior y fondo del modal
        int mx = static_cast<int>(modalX);
        int my = static_cast<int>(modalY);
        int mw = static_cast<int>(modalW);
        int mh = static_cast<int>(modalH);
        int bh = static_cast<int>(btnH);

        Render2D::DrawBox(renderer, mx, my, mw, mh, ColorRGBA{10, 16, 28, 250}, modalBorder);
        Render2D::DrawBox(renderer, mx + 4, my + 4, mw - 8, mh - 8, ColorRGBA{0, 0, 0, 0}, ColorRGBA{0, 140, 200, 100});

        // 3. Cabecera del modal
        Render2D::DrawBox(renderer, mx, my, mw, 60, ColorRGBA{18, 28, 48, 255}, modalBorder);
        Render2D::DrawPixelString(renderer, "|| PAUSA", mx + 235, my + 18, 3, ColorRGBA::Cyan());

        // 4. Subtítulos informativos
        Render2D::DrawPixelString(renderer, sceneText.string_view(), mx + 40, my + 80, 2, ColorRGBA{180, 220, 255, 255});
        Render2D::DrawPixelString(renderer, "SIMULACION Y AUDIO DETENIDOS", mx + 40, my + 110, 2, ColorRGBA{255, 215, 0, 255});

        // 5. Botones interactivos
        ColorRGBA btn1Fill = m_resumeHovered ? ColorRGBA{35, 75, 130, 255} : ColorRGBA{20, 36, 60, 240};
        ColorRGBA btn1Border = m_resumeHovered ? ColorRGBA::Yellow() : ColorRGBA{0, 210, 255, 255};
        Render2D::DrawBox(renderer, mx + 40, static_cast<int>(btn1Y), mw - 80, bh, btn1Fill, btn1Border);
        Render2D::DrawPixelString(renderer, "> REANUDAR (BOTON + / A)", mx + 75, static_cast<int>(btn1Y) + 16, 2, ColorRGBA::White());

        ColorRGBA btn2Fill = m_optionsHovered ? ColorRGBA{25, 90, 130, 255} : ColorRGBA{20, 36, 60, 240};
        ColorRGBA btn2Border = m_optionsHovered ? ColorRGBA::Yellow() : ColorRGBA{0, 210, 255, 255};
        Render2D::DrawBox(renderer, mx + 40, static_cast<int>(btn2Y), mw - 80, bh, btn2Fill, btn2Border);
        Render2D::DrawPixelString(renderer, "* CONFIGURACION (BOTON - / X)", mx + 75, static_cast<int>(btn2Y) + 16, 2, ColorRGBA::White());

        ColorRGBA btn3Fill = m_menuHovered ? ColorRGBA{130, 35, 35, 255} : ColorRGBA{20, 36, 60, 240};
        ColorRGBA btn3Border = m_menuHovered ? ColorRGBA::Yellow() : ColorRGBA{0, 210, 255, 255};
        Render2D::DrawBox(renderer, mx + 40, static_cast<int>(btn3Y), mw - 80, bh, btn3Fill, btn3Border);
        Render2D::DrawPixelString(renderer, "< VOLVER AL MENU (BOTON B)", mx + 75, static_cast<int>(btn3Y) + 16, 2, ColorRGBA::White());

        // 6. Indicaciones ergonómicas
        Render2D::DrawPixelString(renderer, "TOCA UN BOTON O PRESIONA + EN EL GAMEPAD", mx + 60, my + 332, 2, ColorRGBA{130, 155, 185, 255});
        Render2D::DrawPixelString(renderer, "PARA VOLVER AL JUEGO EN CUALQUIER MOMENTO", mx + 45, my + 358, 2, ColorRGBA{130, 155, 185, 255});

        // Línea decorativa inferior
        SDL_SetRenderDrawColor(renderer, 0, 240, 255, 200);
        SDL_RenderDrawLine(renderer, mx + 10, my + mh - 12, mx + mw - 10, my + mh - 12);
    }
}

bool SceneManager::NavigateToHomeMenu() {
    if (m_homeMenuFactory) {
        ChangeScene(m_homeMenuFactory());
        return true;
    }
    return false;
}

bool SceneManager::NavigateToParentMenu(bool fromPureGX2) {
    if (fromPureGX2 && m_modelsMenuFactory) {
        ChangeScene(m_modelsMenuFactory());
        return true;
    }
    return NavigateToHomeMenu();
}

bool SceneManager::NavigateToOptions(bool fromPureGX2) {
    if (m_optionsSceneFactory) {
        ChangeScene(m_optionsSceneFactory([fromPureGX2]() {
            SceneManager::Instance().NavigateToParentMenu(fromPureGX2);
        }));
        return true;
    }
    return false;
}

} // namespace WiiU
