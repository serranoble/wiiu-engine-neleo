#pragma once

#include "Scene/IScene.hpp"
#include "Core/StackString.hpp"
#include "Graphics/Color.hpp"
#include <string>

namespace Neleo {

class SceneLoading : public IScene {
public:
    SceneLoading();
    ~SceneLoading() override = default;

    void OnEnter() override;
    void Update(float dt) override;
    void Render() override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return {"music.wav", "click.wav"};
    }
    [[nodiscard]] std::string GetSceneName() const override {
        return "Pantalla de Carga";
    }
    [[nodiscard]] bool IsMenu() const override {
        return true;
    }

private:
    float m_progress{0.0f};      // 0.0f a 1.0f
    float m_targetProgress{0.0f};
    StackString<128> m_statusText{};
    float m_pulseAnim{0.0f};
    float m_finishTimer{0.0f};
};

} // namespace Neleo
