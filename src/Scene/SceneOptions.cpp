#include "Scene/SceneOptions.hpp"
#include "Scene/SceneMenu.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
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
      m_tabGraphics(180, 120, 220, 44, "1. GRAFICOS", 2),
      m_tabAudio(410, 120, 220, 44, "2. AUDIO", 2),
      m_tabControls(640, 120, 220, 44, "3. CONTROL", 2),
      m_tabDebug(870, 120, 220, 44, "4. DEPURACION", 2),
      m_btnSave(260, 575, 360, 48, "[GUARDAR CONFIGURACION]", 2),
      m_btnDefaults(660, 575, 360, 48, "[RESTABLECER DEFECTOS]", 2),
      m_rowSlider(200, 205, 880, 48, 0.0f, 1.0f, 0.8f, "VOLUMEN GENERAL") {

    InitLayout();
    RefreshControls();
}

void SceneOptions::InitLayout() {
    for (int i = 0; i < 5; ++i) {
        int y = 205 + i * 70;
        m_rowButtons[i].SetRect(200, y, 880, 52);
        m_rowButtons[i].SetTextScale(2);
    }

    // Colores para botones de acción
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
    EngineConfig::Get().ApplyToSubsystems();
    RefreshControls();
    AudioManager::Instance().PlayBGM();
}

void SceneOptions::RefreshControls() {
    const auto& cfg = EngineConfig::Get();

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
    applyTabStyle(m_tabDebug, m_activeTab == OptionsTab::Debug, ColorRGBA::Magenta());

    // 2. Contenido según pestaña activa
    m_useSliderForRow = false;

    char buf[128];
    switch (m_activeTab) {
        case OptionsTab::Graphics: {
            // Fila 0: Modo Shading
            std::snprintf(buf, sizeof(buf), "MODO DE SOMBREADO: [%s]", cfg.graphics.GetRenderModeName());
            m_rowButtons[0].SetLabel(buf);

            // Fila 1: Filtro de Textura GX2
            std::snprintf(buf, sizeof(buf), "FILTRADO DE TEXTURAS GX2: [%s]", cfg.graphics.GetFilterName());
            m_rowButtons[1].SetLabel(buf);

            // Fila 2: VSync / FPS
            std::snprintf(buf, sizeof(buf), "SINCRONIZACION VSYNC: [%s]", cfg.graphics.vsyncEnabled ? "60 FPS FIJOS (ACTIVADO)" : "SIN LIMITE (DESACTIVADO)");
            m_rowButtons[2].SetLabel(buf);

            // Fila 3: Sombras
            const char* shadowNames[3] = { "DESACTIVADAS", "BAJA CALIDAD", "ALTA CALIDAD (PCF)" };
            std::snprintf(buf, sizeof(buf), "SOMBRAS DINAMICAS: [%s]", shadowNames[static_cast<int>(cfg.graphics.shadowQuality)]);
            m_rowButtons[3].SetLabel(buf);

            // Fila 4: Pedestal en dioramas
            std::snprintf(buf, sizeof(buf), "SUELO / PEDESTAL EN DIORAMAS: [%s]", cfg.graphics.showPedestal ? "ACTIVADO" : "OCULTO");
            m_rowButtons[4].SetLabel(buf);
            break;
        }

        case OptionsTab::Audio: {
            // Fila 0: Slider de Volumen Maestro
            m_useSliderForRow = true;
            m_rowSlider.SetValue(cfg.audio.masterVolume);

            // Fila 1: Música BGM
            std::snprintf(buf, sizeof(buf), "MUSICA DE FONDO (BGM): [%s]", cfg.audio.bgmEnabled ? "ACTIVADA (ON)" : "DESACTIVADA (OFF)");
            m_rowButtons[1].SetLabel(buf);

            // Fila 2: Volumen BGM
            std::snprintf(buf, sizeof(buf), "VOLUMEN MUSICA BGM: [%d%%]", static_cast<int>(cfg.audio.bgmVolume * 100.0f));
            m_rowButtons[2].SetLabel(buf);

            // Fila 3: Efectos SFX
            std::snprintf(buf, sizeof(buf), "EFECTOS DE SONIDO (SFX): [%s]", cfg.audio.sfxEnabled ? "ACTIVADOS (ON)" : "DESACTIVADOS (OFF)");
            m_rowButtons[3].SetLabel(buf);

            // Fila 4: Probar Sonido
            m_rowButtons[4].SetLabel("PROBAR EFECTO DE SONIDO: [CLICK SFX]");
            break;
        }

        case OptionsTab::Controls: {
            // Fila 0: Sensibilidad Orbital
            std::snprintf(buf, sizeof(buf), "SENSIBILIDAD CAMARA ORBITAL: [%.1fX]", cfg.camera.orbitSensitivity);
            m_rowButtons[0].SetLabel(buf);

            // Fila 1: Invertir Eje Y
            std::snprintf(buf, sizeof(buf), "INVERTIR EJE VERTICAL (EJE Y): [%s]", cfg.camera.invertY ? "SI (INVERTIDO)" : "NO (ESTANDAR)");
            m_rowButtons[1].SetLabel(buf);

            // Fila 2: FOV
            std::snprintf(buf, sizeof(buf), "CAMPO DE VISION VERTICAL (FOV): [%.0f GRADOS]", cfg.camera.fovDegrees);
            m_rowButtons[2].SetLabel(buf);

            // Fila 3: Control Analógico
            m_rowButtons[3].SetLabel("DISTRIBUCION ANALOGICA: [STICK L = ZOOM | STICK R = ORBITA]");

            // Fila 4: Control Táctil
            m_rowButtons[4].SetLabel("CONTROL TACTIL GAMEPAD DRC: [ARRASTRE EN PANTALLA]");
            break;
        }

        case OptionsTab::Debug: {
            // Fila 0: Colisionadores 3D
            std::snprintf(buf, sizeof(buf), "VISUALIZAR COLISIONADORES 3D (AABB / ESFERA): [%s]", cfg.debug.showColliders ? "SI" : "NO");
            m_rowButtons[0].SetLabel(buf);

            // Fila 1: Triggers Espaciales
            std::snprintf(buf, sizeof(buf), "VISUALIZAR TRIGGERS ESPACIALES: [%s]", cfg.debug.showTriggers ? "SI" : "NO");
            m_rowButtons[1].SetLabel(buf);

            // Fila 2: Grafo y Rutas A*
            std::snprintf(buf, sizeof(buf), "VISUALIZAR NAVGRAPH & RUTAS A*: [%s]", cfg.debug.showNavGraph ? "SI" : "NO");
            m_rowButtons[2].SetLabel(buf);

            // Fila 3: Conos de Visión IA
            std::snprintf(buf, sizeof(buf), "VISUALIZAR PERCEPCION SENSORIAL IA: [%s]", cfg.debug.showVisionCones ? "SI" : "NO");
            m_rowButtons[3].SetLabel(buf);

            // Fila 4: Telemetría OSD
            std::snprintf(buf, sizeof(buf), "BARRA DE TELEMETRIA OSD (FPS / RECURSOS): [%s]", cfg.debug.showOSDTelemetry ? "SI" : "NO");
            m_rowButtons[4].SetLabel(buf);
            break;
        }
    }

    // Colores de las filas
    for (int i = 0; i < 5; ++i) {
        m_rowButtons[i].SetColors(
            ColorRGBA{15, 25, 42, 230}, ColorRGBA{30, 60, 100, 245},
            ColorRGBA{0, 180, 230, 220}, ColorRGBA::Yellow(), ColorRGBA::White()
        );
    }
}

void SceneOptions::SaveConfig() {
    if (EngineConfig::Get().SaveToFile()) {
        m_statusMessage = "CONFIGURACION GUARDADA EN DISCO (engine_config.ini)";
        m_statusColor = ColorRGBA::Green();
    } else {
        m_statusMessage = "AVISO: NO SE PUDO ESCRIBIR EN DISCO (USANDO VALORES EN MEMORIA)";
        m_statusColor = ColorRGBA::Yellow();
    }
    m_statusTimer = 3.5f;
    AudioManager::Instance().PlayClick();
}

void SceneOptions::RestoreDefaults() {
    EngineConfig::Get().ResetToDefaults();
    EngineConfig::Get().ApplyToSubsystems();
    RefreshControls();
    m_statusMessage = "CONFIGURACION RESTABLECIDA A VALORES DE FABRICA";
    m_statusColor = ColorRGBA::Cyan();
    m_statusTimer = 3.5f;
    AudioManager::Instance().PlayClick();
}

void SceneOptions::Update(float dt) {
    m_starfield.Update(dt);

    if (m_statusTimer > 0.0f) {
        m_statusTimer -= dt;
    }

    auto& input = InputManager::Instance();
    const auto& touch = input.GetTouchState();

    // 1. Botón Volver
    if (m_backButton.UpdateAndHandle(touch) || input.IsBackRequested()) {
        AudioManager::Instance().PlayClick();
        if (m_onBackCallback) {
            m_onBackCallback();
        } else {
            SceneManager::Instance().ChangeScene(std::make_unique<SceneMenu>());
        }
        return;
    }

    // 2. Pestañas
    if (m_tabGraphics.Update(touch, "TAB GRAFICOS")) {
        m_activeTab = OptionsTab::Graphics;
        AudioManager::Instance().PlayClick();
        RefreshControls();
        return;
    }
    if (m_tabAudio.Update(touch, "TAB AUDIO")) {
        m_activeTab = OptionsTab::Audio;
        AudioManager::Instance().PlayClick();
        RefreshControls();
        return;
    }
    if (m_tabControls.Update(touch, "TAB CONTROL")) {
        m_activeTab = OptionsTab::Controls;
        AudioManager::Instance().PlayClick();
        RefreshControls();
        return;
    }
    if (m_tabDebug.Update(touch, "TAB DEPURACION")) {
        m_activeTab = OptionsTab::Debug;
        AudioManager::Instance().PlayClick();
        RefreshControls();
        return;
    }

    // 3. Botones de Acción (Guardar / Defectos)
    if (m_btnSave.Update(touch, "GUARDAR")) {
        SaveConfig();
        return;
    }
    if (m_btnDefaults.Update(touch, "DEFECTOS")) {
        RestoreDefaults();
        return;
    }

    // 4. Filas de Opciones según Pestaña
    auto& cfg = EngineConfig::Get();

    switch (m_activeTab) {
        case OptionsTab::Graphics: {
            if (m_rowButtons[0].Update(touch, "TOGGLE MODO")) {
                uint8_t m = (static_cast<uint8_t>(cfg.graphics.renderMode) + 1) % 3;
                cfg.graphics.renderMode = static_cast<ShadingRenderMode>(m);
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            if (m_rowButtons[1].Update(touch, "TOGGLE FILTRO")) {
                uint8_t f = (static_cast<uint8_t>(cfg.graphics.textureFilter) + 1) % 7;
                cfg.graphics.textureFilter = static_cast<TextureFilterMode>(f);
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            if (m_rowButtons[2].Update(touch, "TOGGLE VSYNC")) {
                cfg.graphics.vsyncEnabled = !cfg.graphics.vsyncEnabled;
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            if (m_rowButtons[3].Update(touch, "TOGGLE SOMBRAS")) {
                uint8_t s = (static_cast<uint8_t>(cfg.graphics.shadowQuality) + 1) % 3;
                cfg.graphics.shadowQuality = static_cast<ShadowQuality>(s);
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            if (m_rowButtons[4].Update(touch, "TOGGLE PEDESTAL")) {
                cfg.graphics.showPedestal = !cfg.graphics.showPedestal;
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            break;
        }

        case OptionsTab::Audio: {
            if (m_useSliderForRow && m_rowSlider.Update(touch, "SLIDER VOLUMEN")) {
                cfg.audio.masterVolume = m_rowSlider.GetValue();
                cfg.ApplyToSubsystems();
            }
            if (m_rowButtons[1].Update(touch, "TOGGLE BGM")) {
                cfg.audio.bgmEnabled = !cfg.audio.bgmEnabled;
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            if (m_rowButtons[2].Update(touch, "CYCLE VOL BGM")) {
                if (cfg.audio.bgmVolume < 0.35f) cfg.audio.bgmVolume = 0.50f;
                else if (cfg.audio.bgmVolume < 0.60f) cfg.audio.bgmVolume = 0.75f;
                else if (cfg.audio.bgmVolume < 0.85f) cfg.audio.bgmVolume = 1.00f;
                else cfg.audio.bgmVolume = 0.25f;
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            if (m_rowButtons[3].Update(touch, "TOGGLE SFX")) {
                cfg.audio.sfxEnabled = !cfg.audio.sfxEnabled;
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            if (m_rowButtons[4].Update(touch, "PROBAR SFX")) {
                AudioManager::Instance().PlayClick();
            }
            break;
        }

        case OptionsTab::Controls: {
            if (m_rowButtons[0].Update(touch, "CYCLE SENSIBILIDAD")) {
                if (cfg.camera.orbitSensitivity < 0.75f) cfg.camera.orbitSensitivity = 1.0f;
                else if (cfg.camera.orbitSensitivity < 1.25f) cfg.camera.orbitSensitivity = 1.5f;
                else if (cfg.camera.orbitSensitivity < 1.75f) cfg.camera.orbitSensitivity = 2.0f;
                else cfg.camera.orbitSensitivity = 0.5f;
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            if (m_rowButtons[1].Update(touch, "TOGGLE INVERT Y")) {
                cfg.camera.invertY = !cfg.camera.invertY;
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            if (m_rowButtons[2].Update(touch, "CYCLE FOV")) {
                if (cfg.camera.fovDegrees < 60.0f) cfg.camera.fovDegrees = 65.0f;
                else if (cfg.camera.fovDegrees < 70.0f) cfg.camera.fovDegrees = 75.0f;
                else cfg.camera.fovDegrees = 55.0f;
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            break;
        }

        case OptionsTab::Debug: {
            if (m_rowButtons[0].Update(touch, "TOGGLE COLLIDERS")) {
                cfg.debug.showColliders = !cfg.debug.showColliders;
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            if (m_rowButtons[1].Update(touch, "TOGGLE TRIGGERS")) {
                cfg.debug.showTriggers = !cfg.debug.showTriggers;
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            if (m_rowButtons[2].Update(touch, "TOGGLE NAVGRAPH")) {
                cfg.debug.showNavGraph = !cfg.debug.showNavGraph;
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            if (m_rowButtons[3].Update(touch, "TOGGLE VISION")) {
                cfg.debug.showVisionCones = !cfg.debug.showVisionCones;
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            if (m_rowButtons[4].Update(touch, "TOGGLE OSD")) {
                cfg.debug.showOSDTelemetry = !cfg.debug.showOSDTelemetry;
                cfg.ApplyToSubsystems();
                AudioManager::Instance().PlayClick();
                RefreshControls();
            }
            break;
        }
    }
}

void SceneOptions::Render(SDL_Renderer* renderer) {
    // 1. Fondo Estelar
    m_starfield.Render(renderer, 1280, 720);

    // 2. Botón Volver
    m_backButton.Render(renderer);

    // 3. Título y Subtítulo
    Render2D::drawPixelString(renderer, "CONFIGURACION GLOBAL DEL MOTOR", 345, 30, 4, ColorRGBA::Cyan());
    Render2D::drawPixelString(renderer, "JASON GREGORY CAP. 5.3: OPCIONES PERSISTENTES Y SINCRONIZADAS", 270, 75, 2, ColorRGBA::Yellow());

    // 4. Pestañas Superiores
    m_tabGraphics.Render(renderer);
    m_tabAudio.Render(renderer);
    m_tabControls.Render(renderer);
    m_tabDebug.Render(renderer);

    // 5. Marco Contenedor
    Render2D::DrawBox(renderer, 160, 180, 960, 380, ColorRGBA{8, 14, 24, 220}, ColorRGBA{0, 200, 240, 140});

    // 6. Filas de Opciones
    if (m_useSliderForRow) {
        m_rowSlider.Render(renderer);
        for (int i = 1; i < 5; ++i) {
            m_rowButtons[i].Render(renderer);
        }
    } else {
        for (int i = 0; i < 5; ++i) {
            m_rowButtons[i].Render(renderer);
        }
    }

    // 7. Botones de Acción (Guardar / Defectos)
    m_btnSave.Render(renderer);
    m_btnDefaults.Render(renderer);

    // 8. Mensaje de Estado / Feedback
    if (m_statusTimer > 0.0f) {
        Render2D::drawPixelString(renderer, m_statusMessage.c_str(), 320, 640, 2, m_statusColor);
    } else {
        Render2D::drawPixelString(renderer, "PULSA UN BOTON PARA MODIFICAR LA OPCION | PERSISTENCIA EN DISCO", 280, 640, 2, ColorRGBA{160, 180, 200, 220});
    }

    // 9. Barra Guía
    Render2D::drawPixelString(renderer, "TOUCH / A: MODIFICAR | B: VOLVER | +: MENU PAUSA | -: OSD METRICAS", 275, 680, 2, ColorRGBA{140, 150, 165, 255});
}

} // namespace WiiU
