#pragma once

#include "Scene/IScene.hpp"
#include "Scene/Starfield3D.hpp"
#include "Graphics/UIComponents.hpp"
#include "Core/Config/EngineConfig.hpp"
#include <functional>
#include <string>

namespace WiiU {

/**
 * @brief Categorías de configuración global expuestas en la interfaz de opciones.
 */
enum class OptionsTab : uint8_t {
    Graphics = 0,
    Audio = 1,
    Controls = 2
};

/**
 * @brief [Jason Gregory Cap. 5.3: "Engine Configuration"]
 * Menú Global de Configuración del Motor y Sincronización en Caliente.
 */
class SceneOptions : public IScene {
public:
    explicit SceneOptions(std::function<void()> onBackCallback = nullptr);

    void OnEnter() override;
    void Update(float dt) override;
    void Render() override;

    [[nodiscard]] std::string GetSceneName() const override {
        return "Configuracion del Motor";
    }
    [[nodiscard]] bool IsMenu() const override {
        return true;
    }

private:
    void InitLayout();
    void RefreshControls();
    void SaveConfig();
    void RestoreDefaults();
    void TriggerActionForRow(int rowIndex);

    Starfield3D m_starfield;
    std::function<void()> m_onBackCallback{nullptr};
    UIBackButton m_backButton;

    OptionsTab m_activeTab{OptionsTab::Graphics};
    int m_focusedRow{0};

    // Botones de pestañas superiores (3 pestañas globales)
    UIButton m_tabGraphics;
    UIButton m_tabAudio;
    UIButton m_tabControls;

    // Botones de acciones inferiores
    UIButton m_btnSave;
    UIButton m_btnDefaults;

    // Filas interactivas de opciones (5 filas)
    UIButton m_rowButtons[5];

    float m_stickRepeatTimer{0.0f};
    float m_statusTimer{0.0f};
    const char* m_statusMessage{nullptr};
    ColorRGBA m_statusColor{ColorRGBA::Green()};
    bool m_saveInFlight{false};
};

} // namespace WiiU
