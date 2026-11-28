#include "Scene/SceneOptions.hpp"
#include "Scene/SceneMenu.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Input/RumbleManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/ResourceMonitor.hpp"
#include "Core/Localization/LocalizationManager.hpp"
#include "Core/Storage/StorageManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace Neleo {

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
      m_tabGraphics(190, 115, 280, 44, NELEO_LOC("options.tab.graphics"), 2),
      m_tabAudio(500, 115, 280, 44, NELEO_LOC("options.tab.audio"), 2),
      m_tabControls(810, 115, 280, 44, NELEO_LOC("options.tab.controls"), 2),
      m_btnSave(260, 580, 360, 48, NELEO_LOC("options.btn.save"), 2),
      m_btnDefaults(660, 580, 360, 48, NELEO_LOC("options.btn.defaults"), 2) {

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

    // Refrescar etiquetas de pestañas y botones de acción según el idioma activo
    m_tabGraphics.SetLabel(NELEO_LOC("options.tab.graphics"));
    m_tabAudio.SetLabel(NELEO_LOC("options.tab.audio"));
    m_tabControls.SetLabel(NELEO_LOC("options.tab.controls"));
    m_btnSave.SetLabel(NELEO_LOC("options.btn.save"));
    m_btnDefaults.SetLabel(NELEO_LOC("options.btn.defaults"));
    m_backButton.RefreshLabel();

    applyTabStyle(m_tabGraphics, m_activeTab == OptionsTab::Graphics, ColorRGBA::Cyan());
    applyTabStyle(m_tabAudio, m_activeTab == OptionsTab::Audio, ColorRGBA::Green());
    applyTabStyle(m_tabControls, m_activeTab == OptionsTab::Controls, ColorRGBA::Orange());

    char buf[128];
    switch (m_activeTab) {
        case OptionsTab::Graphics: {
            // Fila 0: Resolución Scanbuffer
            m_rowButtons[0].SetLabel(NELEO_LOC("options.gfx.resolution"));

            // Fila 1: Sincronización VSync
            std::snprintf(buf, sizeof(buf), NELEO_LOC_CSTR("options.gfx.vsync"),
                          cfg.graphics.vsyncEnabled ? NELEO_LOC_CSTR("options.gfx.vsync_on")
                                                    : NELEO_LOC_CSTR("options.gfx.vsync_off"));
            m_rowButtons[1].SetLabel(buf);

            // Fila 2: Filtrado de Texturas GX2
            std::snprintf(buf, sizeof(buf), NELEO_LOC_CSTR("options.gfx.filter"), cfg.graphics.GetFilterName());
            m_rowButtons[2].SetLabel(buf);

            // Fila 3: Telemetría OSD
            std::snprintf(buf, sizeof(buf), NELEO_LOC_CSTR("options.gfx.osd"),
                          cfg.graphics.showOSDTelemetry ? NELEO_LOC_CSTR("options.gfx.osd_on")
                                                        : NELEO_LOC_CSTR("options.gfx.osd_off"));
            m_rowButtons[3].SetLabel(buf);

            // Fila 4: Información de Silicio Gráfico
            m_rowButtons[4].SetLabel(NELEO_LOC("options.gfx.silicon"));
            break;
        }

        case OptionsTab::Audio: {
            // Fila 0: Volumen Maestro
            std::snprintf(buf, sizeof(buf), NELEO_LOC_CSTR("options.audio.master"),
                          static_cast<int>(cfg.audio.masterVolume * 100.0f + 0.5f));
            m_rowButtons[0].SetLabel(buf);

            // Fila 1: Música de Fondo (BGM)
            std::snprintf(buf, sizeof(buf), NELEO_LOC_CSTR("options.audio.bgm"),
                          cfg.audio.bgmEnabled ? NELEO_LOC_CSTR("options.audio.bgm_on")
                                               : NELEO_LOC_CSTR("options.audio.bgm_off"));
            m_rowButtons[1].SetLabel(buf);

            // Fila 2: Volumen BGM
            std::snprintf(buf, sizeof(buf), NELEO_LOC_CSTR("options.audio.bgm_vol"),
                          static_cast<int>(cfg.audio.bgmVolume * 100.0f + 0.5f));
            m_rowButtons[2].SetLabel(buf);

            // Fila 3: Efectos SFX
            std::snprintf(buf, sizeof(buf), NELEO_LOC_CSTR("options.audio.sfx"),
                          cfg.audio.sfxEnabled ? NELEO_LOC_CSTR("options.audio.sfx_on")
                                               : NELEO_LOC_CSTR("options.audio.sfx_off"));
            m_rowButtons[3].SetLabel(buf);

            // Fila 4: Probar SFX
            m_rowButtons[4].SetLabel(NELEO_LOC("options.audio.test_sfx"));
            break;
        }

        case OptionsTab::Controls: {
            // Fila 0: Vibración DRC
            std::snprintf(buf, sizeof(buf), NELEO_LOC_CSTR("options.ctrl.rumble"),
                          cfg.controls.rumbleEnabled ? NELEO_LOC_CSTR("options.ctrl.rumble_on")
                                                     : NELEO_LOC_CSTR("options.ctrl.rumble_off"));
            m_rowButtons[0].SetLabel(buf);

            // Fila 1: Intensidad Vibración
            const char* intStr = NELEO_LOC_CSTR("options.ctrl.intensity_high");
            if (cfg.controls.rumbleIntensity <= 0.35f) {
                intStr = NELEO_LOC_CSTR("options.ctrl.intensity_low");
            } else if (cfg.controls.rumbleIntensity <= 0.75f) {
                intStr = NELEO_LOC_CSTR("options.ctrl.intensity_med");
            }
            std::snprintf(buf, sizeof(buf), NELEO_LOC_CSTR("options.ctrl.intensity"), intStr);
            m_rowButtons[1].SetLabel(buf);

            // Fila 2: Deadzone Analógica
            const char* dzStr = NELEO_LOC_CSTR("options.ctrl.dz_med");
            if (cfg.controls.deadzoneRadius <= 0.12f) {
                dzStr = NELEO_LOC_CSTR("options.ctrl.dz_low");
            } else if (cfg.controls.deadzoneRadius >= 0.22f) {
                dzStr = NELEO_LOC_CSTR("options.ctrl.dz_high");
            }
            std::snprintf(buf, sizeof(buf), NELEO_LOC_CSTR("options.ctrl.deadzone"), dzStr);
            m_rowButtons[2].SetLabel(buf);

            // Fila 3: Probar Hápticos
            m_rowButtons[3].SetLabel(NELEO_LOC("options.ctrl.test_rumble"));

            // Fila 4: Idioma del Motor (extensible vía assets/lang/languages.cfg)
            const std::string_view langDisplay = LocalizationManager::Instance().GetCurrentLanguageDisplayName();
            char langBuf[48]{};
            const size_t copyLen = std::min(langDisplay.size(), sizeof(langBuf) - 1u);
            if (copyLen > 0u) {
                std::memcpy(langBuf, langDisplay.data(), copyLen);
            }
            langBuf[copyLen] = '\0';
            std::snprintf(buf, sizeof(buf), NELEO_LOC_CSTR("options.ctrl.language"), langBuf);
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
            } else if (i == 4) {
                // Ciclar idioma activo en caliente y sincronizar con EngineConfig
                auto& locMgr = LocalizationManager::Instance();
                if (locMgr.CycleLanguage()) {
                    const std::string_view code = locMgr.GetCurrentLanguageCode();
                    const size_t copyLen = std::min(code.size(), sizeof(cfg.general.language) - 1u);
                    if (copyLen > 0u) {
                        std::memcpy(cfg.general.language, code.data(), copyLen);
                    }
                    cfg.general.language[copyLen] = '\0';
                    cfg.ApplyToSubsystems();
                }
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
        m_statusMessage = "options.status.saving";
        m_statusColor = ColorRGBA::Cyan();
        m_statusTimer = 10.0f;
    } else {
        m_statusMessage = "options.status.busy";
        m_statusColor = ColorRGBA::Yellow();
        m_statusTimer = 2.0f;
    }
}

void SceneOptions::RestoreDefaults() {
    AudioManager::Instance().PlayClick();
    EngineConfig::Instance().ResetToDefaults();
    EngineConfig::Instance().ApplyToSubsystems();
    RefreshControls();

    m_statusMessage = "options.status.defaults";
    m_statusColor = ColorRGBA::Yellow();
    m_statusTimer = 2.5f;
}

void SceneOptions::Update(float dt) {
    m_starfield.Update(dt);

    if (m_saveInFlight && !StorageManager::IsAsyncSavePending()) {
        m_saveInFlight = false;
        if (StorageManager::GetLastAsyncSaveSuccess()) {
            m_statusMessage = "options.status.saved";
            m_statusColor = ColorRGBA::Green();
        } else {
            m_statusMessage = "options.status.save_error";
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
        bool isInformational = (m_activeTab == OptionsTab::Graphics && (m_focusedRow == 0 || m_focusedRow == 4));
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
            bool isInformational = (m_activeTab == OptionsTab::Graphics && (i == 0 || i == 4));
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
    Render2D::DrawPixelString(NELEO_LOC("options.title"), 220, 38, 2, ColorRGBA::White());
    Render2D::DrawPixelString(NELEO_LOC("options.hint"), 190, 72, 1, ColorRGBA{160, 200, 240, 255});

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

    // Mensaje de estado flotante (resuelto por clave en Render para 0 stale pointers al cambiar idioma)
    if (m_statusTimer > 0.0f && m_statusMessage) {
        Render2D::DrawBox(240, 642, 800, 36, ColorRGBA{10, 15, 25, 240}, m_statusColor);
        Render2D::DrawPixelString(LocalizationManager::Instance().Get(m_statusMessage), 260, 652, 1, m_statusColor);
    }

    Render2D::End();
}

} // namespace Neleo
