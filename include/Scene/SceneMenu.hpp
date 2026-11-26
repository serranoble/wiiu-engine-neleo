#pragma once

#include "Scene/IScene.hpp"
#include "Scene/Starfield3D.hpp"
#include "Graphics/UIComponents.hpp"
#include <vector>
#include <string>
#include <functional>

namespace Neleo {

class SceneMenu : public IScene {
public:
    SceneMenu();

    void OnEnter() override;
    void Update(float dt) override;
    void Render() override;

    [[nodiscard]] std::string GetSceneName() const override {
        return "Menu Principal";
    }
    [[nodiscard]] bool IsMenu() const override {
        return true;
    }

private:
    struct MainMenuItem {
        UIButton button;
        std::string subtitle;
        const char* actionName;
        std::function<void()> onSelect;
    };

    void InitMenuItems();

    Starfield3D m_starfield;
    std::vector<MainMenuItem> m_menuItems;
    size_t m_focusedIndex{0};
};

} // namespace Neleo
