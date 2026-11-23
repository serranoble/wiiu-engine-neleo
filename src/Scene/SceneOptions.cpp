#include "Scene/SceneOptions.hpp"
#include "Scene/SceneMenu.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Input/RumbleManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/ResourceMonitor.hpp"
#include "Core/Storage/StorageManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include <cstdio>
#include <algorithm>

namespace WiiU {

SceneOptions::SceneOptions(std::function<void()> onBackCallback)
    : m_starfield(120),
      m_onBackCallback(std::move(onBackCallback)),
      m_backButton([this]() {
          if (m_onBackCallback) {
              m_onBackCallback();
          } else {
              SceneManager::Instance().ChangeScene(std::make_unique<SceneMenu>());
          }
      }),
      m_tabGraphics(190, 115, 280, 44, "1. GRAFICOS", 2),
      m_tabAudio(500, 115, 280, 44, "2. AUDIO", 2),
      m_tabControls(810, 115, 280, 44, "3. CONTROL & HW", 2),
      m_btnSave(260, 580, 360, 48, "[GUARDAR AJUSTES (+)]", 2),
      m_btnDefaults(660, 580, 360, 48, "[DEFECTOS FABRICA (X)]", 2) {

    InitLayout();
    RefreshControls();
}

void SceneOptions::InitLayout() {
    for (int i = 0; i < 5; ++i) {
        int y = 190 + i * 72;
        m_rowButtons[i].SetRect(200, y, 880, 54);
        m_rowButtons[i].SetTextScale(2);
    }

    // Colores de botones de acción
    m_btnSave.SetColors(
        ColorRGBA{15, 45, 25, 235}, ColorRGBA{0, 200, 120, 245},
        ColorRGBA::Green(), ColorRGBA::Yellow(), ColorRGBA::White()
    );
    m_btnDefaults.SetColors(
        ColorRGBA{45, 20, 25, 235}, ColorRGBA{200, 50, 60, 245},
        ColorRGBA{240, 70, 70, 255}, ColorRGBA::Yellow(), ColorRGBA::White()
    );
}

void SceneOptions::OnEnter() {
    EngineConfig::Instance().ApplyToSubsystems();
    RefreshControls();
    AudioManager::Instance().PlayBGM();
}

void SceneOptions::RefreshControls() {
    const auto& cfg = EngineConfig::Instance();

    // 1. Estilo de Pestañas
    auto applyTabStyle = [](UIButton& btn, bool active, ColorRGBA activeColor) {
        if (active) {
            btn.SetColors(
                ColorRGBA{20, 45, 75, 240}, ColorRGBA{30, 80, 140, 255},
                activeColor, ColorRGBA::Yellow(), ColorRGBA::White()
            );
        } else {
            btn.SetColors(
                ColorRGBA{12, 18, 30, 220}, ColorRGBA{35, 50, 75, 235},
                ColorRGBA{80, 110, 140, 180}, ColorRGBA::Yellow(), ColorRGBA{160, 180, 205, 255}
            );
        }
    };

    applyTabStyle(m_tabGraphics, m_activeTab == OptionsTab::Graphics, ColorRGBA::Cyan());
    applyTabStyle(m_tabAudio, m_activeTab == OptionsTab::Audio, ColorRGBA::Green());
    applyTabStyle(m_tabControls, m_activeTab == OptionsTab::Controls, ColorRGBA::Orange());

    char buf[96];
    switch (m_activeTab) {
        case OptionsTab::Graphics: {
            // Fila 0: Resolución Scanbuffer
            std::snprintf(buf, sizeof(buf), "RESOLUCION SCANBUFFER: [720P NATIVO (1280x720)]");
            m_rowButtons[0].SetLabel(buf);

            // Fila 1: Sincronización VSync
            std::snprintf(buf, sizeof(buf), "SINCRONIZACION VSYNC: [%s]",
                          cfg.graphics.vsyncEnabled ? "60 FPS FIJOS (ACTIVADO)" : "SIN LIMITE (DESACTIVADO)");
            m_rowButtons[1].SetLabel(buf);

            // Fila 2: Filtrado de Texturas GX2
            std::snprintf(buf, sizeof(buf), "FILTRADO DE TEXTURAS: [%s]", cfg.graphics.GetFilterName());
            m_rowButtons[2].SetLabel(buf);

            // Fila 3: Telemetría OSD
            std::snprintf(buf, sizeof(buf), "TELEMETRIA OSD (BOTON -): [%s]",
                          cfg.graphics.showOSDTelemetry ? "ACTIVADA (VISIBLE)" : "DESACTIVADA (OCULTA)");
            m_rowButtons[3].SetLabel(buf);

            // Fila 4: Información de Silicio Gráfico
            m_rowButtons[4].SetLabel("SILICIO GRAFICO: [GPU AMD LATTE R700 @ 550 MHz]");
            break;
        }

        case OptionsTab::Audio: {
            // Fila 0: Volumen Maestro
            std::snprintf(buf, sizeof(buf), "VOLUMEN MAESTRO GENERAL: [%d%%]",
                          static_cast<int>(cfg.audio.masterVolume * 100.0f + 0.5f));
            m_rowButtons[0].SetLabel(buf);

            // Fila 1: Música de Fondo (BGM)
            std::snprintf(buf, sizeof(buf), "MUSICA DE FONDO (BGM): [%s]",
                          cfg.audio.bgmEnabled ? "ACTIVADA (REPRODUCIENDO)" : "DESACTIVADA (SILENCIO)");
            m_rowButtons[1].SetLabel(buf);

            // Fila 2: Volumen BGM
            std::snprintf(buf, sizeof(buf), "VOLUMEN MUSICA BGM: [%d%%]",
                          static_cast<int>(cfg.audio.bgmVolume * 100.0f + 0.5f));
            m_rowButtons[2].SetLabel(buf);

            // Fila 3: Efectos SFX
            std::snprintf(buf, sizeof(buf), "EFECTOS DE SONIDO (SFX): [%s]",
                          cfg.audio.sfxEnabled ? "ACTIVADOS" : "DESACTIVADOS");
            m_rowButtons[3].SetLabel(buf);

            // Fila 4: Probar SFX
            m_rowButtons[4].SetLabel("[PROBAR SFX DE INTERFAZ]");
            break;
        }

        case OptionsTab::Controls: {
            // Fila 0: Vibración DRC
            std::snprintf(buf, sizeof(buf), "VIBRACION HAPTICA: [%s]",
                          cfg.controls.rumbleEnabled ? "ACTIVADA" : "DESACTIVADA");
            m_rowButtons[0].SetLabel(buf);

            // Fila 1: Intensidad Vibración
            const char* intStr = "FUERTE (100%)";
            if (cfg.controls.rumbleIntensity <= 0.35f) {
                intStr = "SUAVE (30%)";
            } else if (cfg.controls.rumbleIntensity <= 0.75f) {
                intStr = "MEDIA (70%)";
            }
            std::snprintf(buf, sizeof(buf), "INTENSIDAD DE VIBRACION: [%s]", intStr);
            m_rowButtons[1].SetLabel(buf);

            // Fila 2: Deadzone Analógica
            const char* dzStr = "ESTANDAR (18%)";
            if (cfg.controls.deadzoneRadius <= 0.12f) {
                dzStr = "PRECISA (10%)";
            } else if (cfg.controls.deadzoneRadius >= 0.22f) {
                dzStr = "AMPLIA (25%)";
            }
            std::snprintf(buf, sizeof(buf), "ZONA MUERTA ANALOGICA: [%s]", dzStr);
            m_rowButtons[2].SetLabel(buf);

            // Fila 3: Probar Hápticos
            m_rowButtons[3].SetLabel("[PROBAR PULSO HAPTICO (200 MS)]");

            // Fila 4: Consulta Dinámica de Periférico Conectado
            const auto& inputMgr = InputManager::Instance();
            const auto& pad0 = inputMgr.GetGamepad(0);
            const char* devName = "NINGUNO DETECTADO";
            if (pad0.isConnected) {
                switch (pad0.type) {
                    case ControllerType::GamePadDRC:        devName = "WII U GAMEPAD DRC"; break;
                    case ControllerType::ProController:     devName = "WII U PRO CONTROLLER"; break;
                    case ControllerType::ClassicController: devName = "CLASSIC CONTROLLER"; break;
                    case ControllerType::WiiRemote:         devName = "WII REMOTE"; break;
                    case ControllerType::WiiRemoteNunchuk:  devName = "WII REMOTE + NUNCHUK"; break;
                    case ControllerType::KeyboardMouse:     devName = "TECLADO / RATON PC"; break;
                    case ControllerType::GenericSDL:        devName = "MANDO USB GENERICO"; break;
                    default:                                devName = "MANDO CONECTADO"; break;
                }
            }
            std::snprintf(buf, sizeof(buf), "MANDO PRINCIPAL (J1): [%s]", devName);
            m_rowButtons[4].SetLabel(buf);
            break;
        }
    }
}

void SceneOptions::TriggerActionForRow(int i) {
    auto& cfg = EngineConfig::Instance();

    switch (m_activeTab) {
        case OptionsTab::Graphics: {
            if (i == 0) {
                // Resolución scanbuffer nativa fija de la consola (1280x720)
            } else if (i == 1) {
                // Alternar VSync
                cfg.graphics.vsyncEnabled = !cfg.graphics.vsyncEnabled;
                cfg.ApplyToSubsystems();
            } else if (i == 2) {
                // Ciclar filtrado: Point -> Bilinear -> Aniso2x -> Aniso4x -> Aniso8x -> Aniso16x
                uint8_t nextFilter = (static_cast<uint8_t>(cfg.graphics.textureFilter) + 1) % 6;
                cfg.graphics.textureFilter = static_cast<TextureFilterMode>(nextFilter);
                cfg.ApplyToSubsystems();
            } else if (i == 3) {
                // Alternar telemetría OSD
                cfg.graphics.showOSDTelemetry = !cfg.graphics.showOSDTelemetry;
                cfg.ApplyToSubsystems();
            }
            break;
        }

        case OptionsTab::Audio: {
            if (i == 0) {
                // Pasos discretos deterministas: 0%, 25%, 50%, 75%, 100%
                static constexpr float VOL_STEPS[] = { 0.0f, 0.25f, 0.50f, 0.75f, 1.0f };
                int curIdx = 0;
                float minDiff = 999.0f;
                for (int s = 0; s < 5; ++s) {
                    float diff = std::abs(cfg.audio.masterVolume - VOL_STEPS[s]);
                    if (diff < minDiff) { minDiff = diff; curIdx = s; }
                }
                cfg.audio.masterVolume = VOL_STEPS[(curIdx + 1) % 5];
                cfg.ApplyToSubsystems();
            } else if (i == 1) {
                // Alternar BGM
                cfg.audio.bgmEnabled = !cfg.audio.bgmEnabled;
                cfg.ApplyToSubsystems();
            } else if (i == 2) {
                // Pasos discretos deterministas BGM: 0%, 25%, 50%, 75%, 100%
                static constexpr float VOL_STEPS[] = { 0.0f, 0.25f, 0.50f, 0.75f, 1.0f };
                int curIdx = 0;
                float minDiff = 999.0f;
                for (int s = 0; s < 5; ++s) {
                    float diff = std::abs(cfg.audio.bgmVolume - VOL_STEPS[s]);
                    if (diff < minDiff) { minDiff = diff; curIdx = s; }
                }
                cfg.audio.bgmVolume = VOL_STEPS[(curIdx + 1) % 5];
                cfg.ApplyToSubsystems();
            } else if (i == 3) {
                // Alternar SFX
                cfg.audio.sfxEnabled = !cfg.audio.sfxEnabled;
                cfg.ApplyToSubsystems();
            } else if (i == 4) {
                // Probar SFX (el clic ya fue emitido al seleccionar)
            }
            break;
        }

        case OptionsTab::Controls: {
            if (i == 0) {
                // Alternar vibración háptica
                cfg.controls.rumbleEnabled = !cfg.controls.rumbleEnabled;
                cfg.ApplyToSubsystems();
            } else if (i == 1) {
                // Ciclar intensidad: 30% -> 70% -> 100% -> 30%
                if (cfg.controls.rumbleIntensity <= 0.35f) {
                    cfg.controls.rumbleIntensity = 0.70f;
                } else if (cfg.controls.rumbleIntensity <= 0.75f) {
                    cfg.controls.rumbleIntensity = 1.00f;
                } else {
                    cfg.controls.rumbleIntensity = 0.30f;
                }
                cfg.ApplyToSubsystems();
            } else if (i == 2) {
                // Ciclar zona muerta: 10% -> 18% -> 25% -> 10%
                if (cfg.controls.deadzoneRadius <= 0.12f) {
                    cfg.controls.deadzoneRadius = 0.18f;
                } else if (cfg.controls.deadzoneRadius <= 0.20f) {
                    cfg.controls.deadzoneRadius = 0.25f;
                } else {
                    cfg.controls.deadzoneRadius = 0.10f;
                }
                cfg.ApplyToSubsystems();
            } else if (i == 3) {
                // Probar pulso háptico DRC (base 1.0f para no aplicar doble atenuación)
                RumbleManager::Instance().TriggerRumble(PlayerIndex::GamePad, 1.0f, 0.2f);
            }
            break;
        }
    }
}

void SceneOptions::SaveConfig() {
    AudioManager::Instance().PlayClick();
    if (m_saveInFlight) {
        return;
    }

    if (EngineConfig::Instance().SaveToFileAsync()) {
        m_saveInFlight = true;
        m_statusMessage = "GUARDANDO AJUSTES EN DISCO...";
        m_statusColor = ColorRGBA::Cyan();
        m_statusTimer = 10.0f;
    } else {
        m_statusMessage = "COLA DE GUARDADO OCUPADA, REINTENTA";
        m_statusColor = ColorRGBA::Yellow();
        m_statusTimer = 2.0f;
    }
}

void SceneOptions::RestoreDefaults() {
    AudioManager::Instance().PlayClick();
    EngineConfig::Instance().ResetToDefaults();
    EngineConfig::Instance().ApplyToSubsystems();
    RefreshControls();

    m_statusMessage = "AJUSTES RESTABLECIDOS A VALORES DE FABRICA";
    m_statusColor = ColorRGBA::Yellow();
    m_statusTimer = 2.5f;
}

void SceneOptions::Update(float dt) {
    m_starfield.Update(dt);

    if (m_saveInFlight && !StorageManager::IsAsyncSavePending()) {
        m_saveInFlight = false;
        if (StorageManager::GetLastAsyncSaveSuccess()) {
            m_statusMessage = "AJUSTES GUARDADOS CORRECTAMENTE EN DISCO";
            m_statusColor = ColorRGBA::Green();
        } else {
            m_statusMessage = "ERROR AL GUARDAR CONFIGURACION EN DISCO";
            m_statusColor = ColorRGBA::Red();
        }
        m_statusTimer = 2.5f;
    }

    if (m_statusTimer > 0.0f && !m_saveInFlight) {
        m_statusTimer -= dt;
    }

    if (m_stickRepeatTimer > 0.0f) {
        m_stickRepeatTimer -= dt;
    }

    const auto& inputMgr = InputManager::Instance();
    const auto& touch = inputMgr.GetGamepad(0).touch;

    // Retorno táctil con guarda de salida inmediata
    if (m_backButton.UpdateAndHandle(touch)) {
        return;
    }

    // Consulta de acciones sobre todos los mandos físicos conectados
    bool navUp = false;
    bool navDown = false;
    bool navLeftTab = false;
    bool navRightTab = false;
    bool actionA = false;
    bool actionB = false;
    bool actionPlus = false;
    bool actionX = false;

    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = inputMgr.GetGamepad(p);
        if (!pad.isConnected) continue;

        if (pad.WasJustPressed(VirtualButton::B)) actionB = true;
        if (pad.WasJustPressed(VirtualButton::DPadUp)) navUp = true;
        if (pad.WasJustPressed(VirtualButton::DPadDown)) navDown = true;
        if (pad.WasJustPressed(VirtualButton::L) || pad.WasJustPressed(VirtualButton::ZL)) navLeftTab = true;
        if (pad.WasJustPressed(VirtualButton::R) || pad.WasJustPressed(VirtualButton::ZR)) navRightTab = true;
        if (pad.WasJustPressed(VirtualButton::A)) actionA = true;
        if (pad.WasJustPressed(VirtualButton::Plus)) actionPlus = true;
        if (pad.WasJustPressed(VirtualButton::X)) actionX = true;

        // Navegación analógica por Stick Izquierdo Y con histéresis
        if (m_stickRepeatTimer <= 0.0f) {
            if (pad.leftStick.y > 0.55f) {
                navUp = true;
                m_stickRepeatTimer = 0.22f;
            } else if (pad.leftStick.y < -0.55f) {
                navDown = true;
                m_stickRepeatTimer = 0.22f;
            }
        }
    }

    // Salida física por botón B
    if (actionB) {
        AudioManager::Instance().PlayClick();
        if (m_onBackCallback) {
            m_onBackCallback();
        } else {
            SceneManager::Instance().ChangeScene(std::make_unique<SceneMenu>());
        }
        return;
    }

    // Navegación por Mando Físico (D-Pad / Sticks / Botones A, X, Plus, L, R)
    if (navUp) {
        AudioManager::Instance().PlayClick();
        m_focusedRow = (m_focusedRow + 4) % 5;
    } else if (navDown) {
        AudioManager::Instance().PlayClick();
        m_focusedRow = (m_focusedRow + 1) % 5;
    } else if (navLeftTab) {
        AudioManager::Instance().PlayClick();
        m_activeTab = static_cast<OptionsTab>((static_cast<uint8_t>(m_activeTab) + 2) % 3);
        RefreshControls();
    } else if (navRightTab) {
        AudioManager::Instance().PlayClick();
        m_activeTab = static_cast<OptionsTab>((static_cast<uint8_t>(m_activeTab) + 1) % 3);
        RefreshControls();
    } else if (actionA) {
        bool isInformational = (m_activeTab == OptionsTab::Graphics && (m_focusedRow == 0 || m_focusedRow == 4)) ||
                               (m_activeTab == OptionsTab::Controls && m_focusedRow == 4);
        if (!isInformational) {
            AudioManager::Instance().PlayClick();
            TriggerActionForRow(m_focusedRow);
            RefreshControls();
        }
    } else if (actionPlus) {
        SaveConfig();
    } else if (actionX) {
        RestoreDefaults();
    }

    // Interacción Táctil Simultánea
    if (m_tabGraphics.Update(touch)) {
        AudioManager::Instance().PlayClick();
        m_activeTab = OptionsTab::Graphics;
        RefreshControls();
    } else if (m_tabAudio.Update(touch)) {
        AudioManager::Instance().PlayClick();
        m_activeTab = OptionsTab::Audio;
        RefreshControls();
    } else if (m_tabControls.Update(touch)) {
        AudioManager::Instance().PlayClick();
        m_activeTab = OptionsTab::Controls;
        RefreshControls();
    }

    if (m_btnSave.Update(touch)) {
        SaveConfig();
    } else if (m_btnDefaults.Update(touch)) {
        RestoreDefaults();
    }

    for (int i = 0; i < 5; ++i) {
        if (m_rowButtons[i].Update(touch)) {
            bool isInformational = (m_activeTab == OptionsTab::Graphics && (i == 0 || i == 4)) ||
                                   (m_activeTab == OptionsTab::Controls && i == 4);
            if (!isInformational) {
                AudioManager::Instance().PlayClick();
                m_focusedRow = i;
                TriggerActionForRow(i);
                RefreshControls();
            }
            break;
        }
    }
}

void SceneOptions::Render() {
    Render2D::Begin();

    m_starfield.Render(1280, 720);

    // Encabezado
    Render2D::DrawPixelString("CONFIGURACION DEL MOTOR - AJUSTES GLOBALES", 220, 38, 2, ColorRGBA::White());
    Render2D::DrawPixelString("D-PAD ARRIBA/ABAJO: NAVEGAR | (A) / TACTIL: CAMBIAR | (L)/(R): PESTANAS | (+): GUARDAR", 190, 72, 1, ColorRGBA{160, 200, 240, 255});

    // Pestañas
    m_tabGraphics.Render();
    m_tabAudio.Render();
    m_tabControls.Render();

    // Filas interactivas
    for (int i = 0; i < 5; ++i) {
        m_rowButtons[i].Render();
    }

    // Resaltado visual del cursor de navegación por mando físico (D-Pad Focus Ring)
    const auto& focusedRect = m_rowButtons[m_focusedRow].GetRect();
    Render2D::DrawBox(focusedRect.x - 3, focusedRect.y - 3, focusedRect.w + 6, focusedRect.h + 6,
                      ColorRGBA{0, 0, 0, 0}, ColorRGBA::Yellow());

    // Botones inferiores
    m_btnSave.Render();
    m_btnDefaults.Render();
    m_backButton.Render();

    // Mensaje de estado flotante
    if (m_statusTimer > 0.0f && m_statusMessage) {
        Render2D::DrawBox(240, 642, 800, 36, ColorRGBA{10, 15, 25, 240}, m_statusColor);
        Render2D::DrawPixelString(m_statusMessage, 260, 652, 1, m_statusColor);
    }

    Render2D::End();
}

} // namespace WiiU
