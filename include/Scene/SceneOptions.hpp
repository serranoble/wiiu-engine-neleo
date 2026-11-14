#pragma once

#include "Scene/IScene.hpp"
#include "Scene/Starfield3D.hpp"
#include "Graphics/UIComponents.hpp"
#include "Core/Config/EngineConfig.hpp"
#include <SDL2/SDL.h>
#include <functional>
#include <string>

namespace WiiU {

/**
 * @brief Categorías de configuración expuestas en la interfaz de opciones.
 */
enum class OptionsTab : uint8_t {
    Graphics = 0,
    Audio = 1,
    Controls = 2,
    Debug = 3
};

/**
 * @brief [Hito 124 - Jason Gregory Cap. 5.3: "Engine Configuration"]
 * Menú Global de Configuración del Motor y Sincronización en Caliente.
 */
class SceneOptions : public IScene {
public:
    /**
     * @brief Constructor con callback opcional de retorno.
     * @param onBackCallback Acción a ejecutar al pulsar "< VOLVER" (si es nullptr, regresa a SceneMenu).
     */
    explicit SceneOptions(std::function<void()> onBackCallback = nullptr);

    void OnEnter() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::string GetSceneName() const override {
        return "Configuración del Motor";
    }
    [[nodiscard]] bool IsMenu() const override {
        return true;
    }

private:
    void InitLayout();
    void RefreshControls();
    void SaveConfig();
    void RestoreDefaults();

    Starfield3D m_starfield;
    std::function<void()> m_onBackCallback{nullptr};
    UIBackButton m_backButton;

    OptionsTab m_activeTab{OptionsTab::Graphics};

    // Botones de pestañas superiores
    UIButton m_tabGraphics;
    UIButton m_tabAudio;
    UIButton m_tabControls;
    UIButton m_tabDebug;

    // Botones de acciones inferiores
    UIButton m_btnSave;
    UIButton m_btnDefaults;

    // Filas interactivas de opciones (5 filas)
    UIButton m_rowButtons[5];
    UISlider m_rowSlider;
    bool m_useSliderForRow{false};

    float m_statusTimer{0.0f};
    std::string m_statusMessage;
    ColorRGBA m_statusColor{ColorRGBA::Green()};
};

} // namespace WiiU
