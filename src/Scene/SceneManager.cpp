#include "Scene/SceneManager.hpp"
#include "Scene/SceneAssetError.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/Localization/LocalizationManager.hpp"
#include "Core/JobSystem.hpp"
#include "Core/StackString.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/Color.hpp"
#include "Audio/AudioManager.hpp"
#include "Input/InputManager.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/swap.h>
#include <gx2/event.h>
#include <proc_ui/procui.h>
#endif

namespace Neleo {

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
    m_modalStack.Clear();

    // Si hay una carga asíncrona previa en vuelo en Core 2, esperar deterministamente su conclusión
    if (m_asyncLoadDispatched) {
        JobSystem::Instance().Wait(&m_asyncLoadCounter);
        m_asyncLoadDispatched = false;
    }

    if (!m_currentScene) {
        // Limpiar cualquier transicion previa residual antes de la carga inicial directa
        m_pendingScene.reset();
        m_transitionPhase = TransitionPhase::Idle;
        m_transitionProgress = 0.0f;
        m_targetProgress = 0.0f;
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

    // Comprobar solicitud de alternar pausa desde mandos físicos o atajos de teclado (solo en escenas de juego, no en menús)
    if (InputManager::Instance().IsPauseRequested()) {
        if (!m_currentScene || !m_currentScene->IsMenu()) {
            TogglePause();
            InputManager::Instance().ConsumePauseRequest();
            return;
        }
    }

    if (m_isPaused) {
        UpdatePauseModal(dt);
        return;
    }

    if (!m_modalStack.IsEmpty()) {
        const bool blockedBeforeUpdate = m_modalStack.IsBlockingSceneUpdate();
        m_modalStack.Update(dt);
        if (blockedBeforeUpdate || m_modalStack.IsBlockingSceneUpdate() ||
            m_transitionPhase != TransitionPhase::Idle) {
            return;
        }
    }

    if (m_currentScene) {
        m_currentScene->Update(dt);
    }
}

void SceneManager::Render() {
    if (m_transitionPhase != TransitionPhase::Idle) {
        RenderTransitionScreen();
        return;
    }

    if (m_currentScene) {
        m_currentScene->Render();
    }

    if (!m_modalStack.IsEmpty()) {
        m_modalStack.Render();
    }

    if (m_isPaused) {
        RenderPauseModal();
    }
}

void SceneManager::RenderTransitionScreen() {
    Render2D::Begin();

    // 1. Fondo Oscuro Futurista
    Render2D::DrawBox(0, 0, 1280, 720, ColorRGBA{6, 12, 24, 255}, ColorRGBA::Cyan());

    // Grid visual de fondo
    const ColorRGBA gridColor{15, 35, 60, 120};
    for (int x = 0; x < 1280; x += 64) {
        Render2D::DrawLine(x, 0, x, 720, gridColor, 1.0f);
    }
    for (int y = 0; y < 720; y += 64) {
        Render2D::DrawLine(0, y, 1280, y, gridColor, 1.0f);
    }

    // 2. Título Superior
    Render2D::DrawPixelString(NELEO_LOC("trans.title"), 220, 105, 3, ColorRGBA::Cyan());
    Render2D::DrawPixelString(NELEO_LOC("trans.subtitle"), 240, 145, 2, ColorRGBA{160, 210, 255, 255});

    // 3. Indicador de Fase Activa (Pill interactiva)
    const char* phaseText = NELEO_LOC_CSTR("trans.phase.unknown");
    ColorRGBA phaseColor = ColorRGBA::White();
    switch (m_transitionPhase) {
        case TransitionPhase::DrainingGPU:
            phaseText = NELEO_LOC_CSTR("trans.phase.1");
            phaseColor = ColorRGBA::Yellow();
            break;
        case TransitionPhase::UnloadingScene:
            phaseText = NELEO_LOC_CSTR("trans.phase.2");
            phaseColor = ColorRGBA{255, 140, 40, 255}; // Naranja Neón
            break;
        case TransitionPhase::LoadingScene:
            phaseText = NELEO_LOC_CSTR("trans.phase.3");
            phaseColor = ColorRGBA{0, 240, 255, 255}; // Cian Neón
            break;
        case TransitionPhase::Finalizing:
            phaseText = NELEO_LOC_CSTR("trans.phase.4");
            phaseColor = ColorRGBA{80, 250, 120, 255}; // Verde Esmeralda
            break;
        default:
            break;
    }

    Render2D::DrawBox(200, 185, 880, 42, ColorRGBA{15, 28, 48, 230}, phaseColor);
    Render2D::DrawPixelString(phaseText, 225, 198, 2, phaseColor);

    // 4. Tarjetas de Información de Escenas (Origen -> Destino)
    // Tarjeta Saliente
    Render2D::DrawBox(200, 242, 425, 68, ColorRGBA{10, 20, 35, 220}, ColorRGBA{180, 70, 70, 255});
    Render2D::DrawPixelString(NELEO_LOC("trans.unloading"), 215, 255, 2, ColorRGBA{255, 120, 120, 255});
    std::string_view outShort = m_outgoingSceneName.empty() ? NELEO_LOC("trans.none") : std::string_view(m_outgoingSceneName);
    if (outShort.size() > 24) outShort = outShort.substr(0, 24);
    Render2D::DrawPixelString(outShort, 215, 281, 2, ColorRGBA::White());

    // Tarjeta Entrante
    Render2D::DrawBox(655, 242, 425, 68, ColorRGBA{10, 20, 35, 220}, ColorRGBA{50, 180, 120, 255});
    Render2D::DrawPixelString(NELEO_LOC("trans.preparing"), 670, 255, 2, ColorRGBA{100, 255, 160, 255});
    std::string_view inShort = m_incomingSceneName.empty() ? NELEO_LOC("trans.next") : std::string_view(m_incomingSceneName);
    if (inShort.size() > 24) inShort = inShort.substr(0, 24);
    Render2D::DrawPixelString(inShort, 670, 281, 2, ColorRGBA::White());

    // 5. Barra de Progreso Neón Central
    constexpr int barX = 200;
    constexpr int barY = 328;
    constexpr int barW = 880;
    constexpr int barH = 50;
    Render2D::DrawBox(barX, barY, barW, barH, ColorRGBA{10, 20, 35, 240}, ColorRGBA::Cyan());

    int fillWidth = static_cast<int>((barW - 8) * std::clamp(m_transitionProgress, 0.0f, 1.0f));
    if (fillWidth > 0) {
        uint8_t glowAlpha = static_cast<uint8_t>(200 + 55.0f * std::sin(m_pulseAnim));
        Render2D::DrawBox(barX + 4, barY + 4, fillWidth, barH - 8,
                          ColorRGBA{0, 180, 240, glowAlpha}, ColorRGBA::White());
    }

    int percent = static_cast<int>(m_transitionProgress * 100.0f);
    StackString<32> percentStr;
    percentStr.Format("%d %%", percent);
    Render2D::DrawPixelString(percentStr.string_view(), barX + barW / 2 - 30, barY + 14, 3, ColorRGBA::White());

    // 6. Panel de Telemetría de Memoria y Auditoría
    Render2D::DrawBox(200, 395, 880, 275, ColorRGBA{8, 16, 28, 240}, ColorRGBA{70, 130, 200, 255});
    Render2D::DrawPixelString(NELEO_LOC("trans.audit.title"), 225, 412, 2, ColorRGBA::Yellow());

    auto& mem = MemoryManager::Instance();
    size_t activeAllocs = mem.GetActiveAllocationCount();
    size_t totalBytes   = mem.GetTotalAllocatedBytes();
    size_t peakBytes    = mem.GetPeakAllocatedBytes();

    StackString<160> line1;
    line1.Format(NELEO_LOC_CSTR("trans.audit.mem"),
                 totalBytes / 1024, static_cast<float>(totalBytes) / (1024.0f * 1024.0f), activeAllocs, peakBytes / 1024);
    Render2D::DrawPixelString(line1.string_view(), 225, 448, 2, ColorRGBA::White());

    StackString<160> line2;
    if (m_reclaimedBytes > 0) {
        line2.Format(NELEO_LOC_CSTR("trans.audit.reclaimed"), m_reclaimedBytes / 1024);
    } else {
        line2.Format("%s", NELEO_LOC_CSTR("trans.audit.purged"));
    }
    Render2D::DrawPixelString(line2.string_view(), 225, 484, 2, ColorRGBA{100, 255, 140, 255});

    Render2D::DrawPixelString(NELEO_LOC("trans.audit.dangling"), 225, 520, 2, ColorRGBA{0, 220, 255, 255});
    Render2D::DrawPixelString(NELEO_LOC("trans.audit.drain"), 225, 556, 2, ColorRGBA{255, 200, 100, 255});
    Render2D::DrawPixelString(NELEO_LOC("trans.audit.arenas"), 225, 592, 2, ColorRGBA{200, 160, 255, 255});

    StackString<160> line6;
    uint32_t asyncLoaded = AssetManager::GetAsyncBytesLoaded();
    uint32_t asyncTotal  = AssetManager::GetAsyncTotalBytes();
    if (asyncTotal > 0) {
        line6.Format(NELEO_LOC_CSTR("trans.audit.stream"),
                     static_cast<float>(asyncLoaded) / (1024.0f * 1024.0f),
                     static_cast<float>(asyncTotal) / (1024.0f * 1024.0f),
                     AssetManager::GetAsyncFilesLoaded());
    } else {
        line6.Format("%s", NELEO_LOC_CSTR("trans.audit.cache"));
    }
    Render2D::DrawPixelString(line6.string_view(), 225, 628, 2, ColorRGBA{120, 220, 255, 255});

    Render2D::End();
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
    m_modalStack.Clear();
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
    m_optionsHovered = false;

    // Pausar o reanudar el subsistema de audio determinista y contexto de entrada UIModal
    AudioManager::Instance().SetPaused(paused);
    if (paused) {
        InputManager::Instance().GetContextStack().PushContext(InputContext::UIModal, 120u, true);
    } else if (!m_modalStack.IsBlockingSceneInput()) {
        InputManager::Instance().GetContextStack().RemoveContext(InputContext::UIModal);
    }

    if (m_currentScene) {
        if (paused) {
            const std::string name = m_currentScene->GetSceneName();
            m_pausedSceneName.Format("%.*s", static_cast<int>(name.size()), name.data());
            m_currentScene->OnPause();
        } else {
            m_currentScene->OnResume();
        }
    } else if (paused) {
        m_pausedSceneName.Format("SISTEMA");
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

    // 3. Detección por mando físico universal (X / Minus -> Opciones, A / Plus -> Reanudar)
    bool actionX = false;
    bool actionA = false;
    bool actionPlus = false;
    bool actionMinus = false;
    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;
        if (pad.WasJustPressed(VirtualButton::X)) actionX = true;
        if (pad.WasJustPressed(VirtualButton::Minus)) actionMinus = true;
        if (pad.WasJustPressed(VirtualButton::A)) actionA = true;
        if (pad.WasJustPressed(VirtualButton::Plus)) actionPlus = true;
    }

    if (actionX || actionMinus) {
        AudioManager::Instance().PlayClick();
        SetPaused(false);
        const bool wasPureGX2 = (m_currentScene && m_currentScene->IsPureGX2());
        NavigateToOptions(wasPureGX2);
        return;
    }

    if (actionA || actionPlus) {
        AudioManager::Instance().PlayClick();
        SetPaused(false);
        return;
    }
}

void SceneManager::RenderPauseModal() {
    constexpr int modalX = 320;
    constexpr int modalY = 135;
    constexpr int modalW = 640;
    constexpr int modalH = 430;
    constexpr int btnX   = modalX + 40;
    constexpr int btnW   = modalW - 80;
    constexpr int btnH   = 48;
    constexpr int btn1Y  = modalY + 145;
    constexpr int btn2Y  = modalY + 205;
    constexpr int btn3Y  = modalY + 265;

    // Animación de pulso sinusoidal para el contorno de neón del modal
    float pulse = 0.5f + 0.5f * std::sin(m_pauseAnimTimer * 5.0f);
    uint8_t borderPulse = static_cast<uint8_t>(180.0f + pulse * 75.0f);
    ColorRGBA modalBorder{ 0, borderPulse, 255, 255 };

    StackString<96> sceneText;
    sceneText.Format(NELEO_LOC_CSTR("pause.scene"),
                     m_pausedSceneName.empty() ? NELEO_LOC_CSTR("pause.system") : m_pausedSceneName.c_str());

    auto drawModalBtn = [](int x, int y, int w, int h, std::string_view label,
                           bool hovered, ColorRGBA normalFill, ColorRGBA hoverFill,
                           ColorRGBA normalBorder, ColorRGBA hoverBorder) {
        Render2D::DrawBox(x, y, w, h, hovered ? hoverFill : normalFill, hovered ? hoverBorder : normalBorder);
        const int textW = static_cast<int>(label.size()) * 6 * 2;
        const int tx = x + std::max(10, (w - textW) / 2);
        const int ty = y + (h - 14) / 2;
        Render2D::DrawPixelString(label, tx, ty, 2, ColorRGBA::White());
    };

    Render2D::Begin();

    // 1. Telón semitransparente oscuro sobre toda la pantalla (1280x720)
    Render2D::DrawRect(0, 0, 1280, 720, ColorRGBA{0, 0, 0, 195});

    // 2. Marco exterior y fondo del modal
    Render2D::DrawBox(modalX, modalY, modalW, modalH, ColorRGBA{10, 16, 28, 250}, modalBorder);
    Render2D::DrawBox(modalX + 4, modalY + 4, modalW - 8, modalH - 8,
                      ColorRGBA{0, 0, 0, 0}, ColorRGBA{0, 140, 200, 100});

    // 3. Cabecera del modal
    Render2D::DrawBox(modalX, modalY, modalW, 60, ColorRGBA{18, 28, 48, 255}, modalBorder);
    Render2D::DrawPixelString(NELEO_LOC("pause.title"), modalX + 235, modalY + 18, 3, ColorRGBA::Cyan());

    // 4. Subtítulos informativos (Escala Canónica 2)
    Render2D::DrawPixelString(sceneText.string_view(), modalX + 40, modalY + 80, 2, ColorRGBA{180, 220, 255, 255});
    Render2D::DrawPixelString(NELEO_LOC("pause.subtitle"), modalX + 40, modalY + 110, 2, ColorRGBA{255, 215, 0, 255});

    // 5. Botones interactivos
    drawModalBtn(btnX, btn1Y, btnW, btnH, NELEO_LOC("pause.btn.resume"),
                 m_resumeHovered,
                 ColorRGBA{20, 36, 60, 240}, ColorRGBA{35, 75, 130, 255},
                 ColorRGBA{0, 210, 255, 255}, ColorRGBA::Yellow());

    drawModalBtn(btnX, btn2Y, btnW, btnH, NELEO_LOC("pause.btn.options"),
                 m_optionsHovered,
                 ColorRGBA{20, 36, 60, 240}, ColorRGBA{25, 90, 130, 255},
                 ColorRGBA{0, 210, 255, 255}, ColorRGBA::Yellow());

    drawModalBtn(btnX, btn3Y, btnW, btnH, NELEO_LOC("pause.btn.menu"),
                 m_menuHovered,
                 ColorRGBA{20, 36, 60, 240}, ColorRGBA{130, 35, 35, 255},
                 ColorRGBA{0, 210, 255, 255}, ColorRGBA::Yellow());

    // 6. Indicaciones ergonómicas
    Render2D::DrawPixelString(NELEO_LOC("pause.hint1"), modalX + 86, modalY + 332, 2, ColorRGBA{130, 155, 185, 255});
    Render2D::DrawPixelString(NELEO_LOC("pause.hint2"), modalX + 74, modalY + 358, 2, ColorRGBA{130, 155, 185, 255});

    // Línea decorativa inferior
    Render2D::DrawLine(modalX + 10, modalY + modalH - 12, modalX + modalW - 10, modalY + modalH - 12, ColorRGBA::Cyan(), 2.0f);

    Render2D::End();
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

} // namespace Neleo
