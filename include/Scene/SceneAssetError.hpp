#pragma once

#include "Scene/IScene.hpp"
#include "Graphics/UIComponents.hpp"
#include <string>
#include <vector>

namespace WiiU {

/**
 * @brief Escena modal defensiva que bloquea el inicio de una escena cuando faltan recursos,
 * informando al usuario en pantalla de los archivos faltantes y permitiéndole volver al menú.
 */
class SceneAssetError : public IScene {
public:
    SceneAssetError(std::string targetSceneName, std::vector<std::string> missingAssets);
    ~SceneAssetError() override = default;

    void OnEnter() override;
    void Update(float dt) override;
    void Render() override;

    [[nodiscard]] std::string GetSceneName() const override {
        return "Error: Recursos No Encontrados";
    }

private:
    std::string m_targetSceneName;
    std::vector<std::string> m_missingAssets;
    UIButton m_acceptButton;
    float m_animTime{0.0f};
};

} // namespace WiiU
