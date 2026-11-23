#pragma once

#include "Scene/ModalStack.hpp"
#include "Core/Diagnostics/SelfTestTypes.hpp"
#include "Graphics/UIComponents.hpp"
#include <array>
#include <cstddef>

namespace WiiU {

/**
 * @brief Modal selector unificado del Laboratorio Gráfico QA (Suites G1 a G8).
 *
 * Se abre desde un único botón en SceneMenu sobre ModalStack, manteniendo limpio el menú principal.
 * Permite navegar por las 8 Suites de validación (CGP + RTR4) con D-Pad o pantalla táctil.
 */
class GraphicsQAModal final : public ISceneOverlay {
public:
    static constexpr size_t SUITE_COUNT = 8u;

    GraphicsQAModal();
    ~GraphicsQAModal() override = default;

    void OnAttach() override;
    void Update(float dt) override;
    void Render() override;

    [[nodiscard]] const char* GetOverlayName() const noexcept override {
        return "GraphicsQAModal";
    }

private:
    struct SuiteEntry {
        const char* code;
        const char* title;
        const char* refBooks;
        bool implemented;
    };

    void LaunchSelectedSuite();

    std::array<SuiteEntry, SUITE_COUNT> m_suites{};
    size_t m_selectedIndex{0u};
    UIButton m_closeButton;
    float m_noticeTimer{0.0f};
    Diagnostics::EngineSelfTestReport m_selfTestReport{};
};

} // namespace WiiU
