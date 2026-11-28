#include "Scene/GraphicsQAModal.hpp"
#include "Scene/SceneQA_PipelineTextures.hpp"
#include "Scene/SceneQA_LightingPBR.hpp"
#include "Scene/SceneQA_Shadows.hpp"
#include "Scene/SceneQA_EnvironmentWater.hpp"
#include "Scene/SceneQA_PostProcessAlpha.hpp"
#include "Scene/SceneQA_StressCulling.hpp"
#include "Scene/SceneQA_ProceduralNPR.hpp"
#include "Scene/SceneQA_RaytracingBVH.hpp"
#include "Scene/SceneManager.hpp"
#include "Core/Diagnostics/EngineSelfTestSuite.hpp"
#include "Core/Localization/LocalizationManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include <cstdio>
#include <memory>

namespace Neleo {

namespace {
constexpr int kModalX = 110;
constexpr int kModalY = 56;
constexpr int kModalW = 1060;
constexpr int kModalH = 608;
constexpr int kListX  = 140;
constexpr int kListY  = 148;
constexpr int kRowW   = 1000;
constexpr int kRowH   = 46;
constexpr int kRowGap = 6;
} // namespace

GraphicsQAModal::GraphicsQAModal()
    : m_closeButton(975, 608, 165, 40, NELEO_LOC("qa.btn_close"), 2) {
    m_suites = {{
        {"G1", "qa.suite.g1", "CGP 4-6 / RTR4 2,4,6", true},
        {"G2", "qa.suite.g2", "CGP 7,10 / RTR4 5,9",  true},
        {"G3", "qa.suite.g3", "CGP 8 / RTR4 7",       true},
        {"G4", "qa.suite.g4", "CGP 9,15 / RTR4 10,13",true},
        {"G5", "qa.suite.g5", "CGP 14 / RTR4 5,8,12", true},
        {"G6", "qa.suite.g6", "CGP 4.9 / RTR4 18-19", true},
        {"G7", "qa.suite.g7", "CGP 12,14 / RTR4 15",  true},
        {"G8", "qa.suite.g8", "CGP 16 / RTR4 11,22",  true}
    }};

    m_closeButton.SetColors(
        ColorRGBA{55, 22, 28, 235},
        ColorRGBA{175, 45, 55, 245},
        ColorRGBA::Red(),
        ColorRGBA::Yellow(),
        ColorRGBA::White()
    );
}

void GraphicsQAModal::OnAttach() {
    m_selfTestReport = Diagnostics::EngineSelfTestSuite::RunAll();
}

void GraphicsQAModal::LaunchSelectedSuite() {
    if (m_selectedIndex >= SUITE_COUNT) return;

    if (!m_suites[m_selectedIndex].implemented) {
        AudioManager::Instance().PlayClick();
        m_noticeTimer = 2.4f;
        return;
    }

    AudioManager::Instance().PlayClick();
    if (m_selectedIndex == 0u) {
        // SceneManager::ChangeScene() invoca ModalStack::Clear() y destruye `this`; retornar de inmediato.
        SceneManager::Instance().ChangeScene(std::make_unique<SceneQA_PipelineTextures>());
        return;
    }
    if (m_selectedIndex == 1u) {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneQA_LightingPBR>());
        return;
    }
    if (m_selectedIndex == 2u) {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneQA_Shadows>());
        return;
    }
    if (m_selectedIndex == 3u) {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneQA_EnvironmentWater>());
        return;
    }
    if (m_selectedIndex == 4u) {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneQA_PostProcessAlpha>());
        return;
    }
    if (m_selectedIndex == 5u) {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneQA_StressCulling>());
        return;
    }
    if (m_selectedIndex == 6u) {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneQA_ProceduralNPR>());
        return;
    }
    if (m_selectedIndex == 7u) {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneQA_RaytracingBVH>());
        return;
    }
}

void GraphicsQAModal::Update(float dt) {
    if (m_noticeTimer > 0.0f) {
        m_noticeTimer -= dt;
        if (m_noticeTimer < 0.0f) m_noticeTimer = 0.0f;
    }

    const auto& touch = InputManager::Instance().GetTouchState();
    if (m_closeButton.Update(touch, "CERRAR QA MODAL")) {
        SceneManager::Instance().GetModalStack().Pop();
        return;
    }

    // Selección táctil directa sobre filas de suites
    if (touch.justPressed) {
        const int tx = static_cast<int>(touch.x);
        const int ty = static_cast<int>(touch.y);
        if (tx >= kListX && tx <= (kListX + kRowW)) {
            for (size_t i = 0; i < SUITE_COUNT; ++i) {
                const int ry = kListY + static_cast<int>(i) * (kRowH + kRowGap);
                if (ty >= ry && ty <= (ry + kRowH)) {
                    m_selectedIndex = i;
                    LaunchSelectedSuite();
                    return;
                }
            }
        }
    }

    bool navUp = false;
    bool navDown = false;
    bool confirm = false;
    bool cancel = false;
    bool rerunTests = false;

    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;
        if (pad.WasJustPressed(VirtualButton::DPadUp))   navUp = true;
        if (pad.WasJustPressed(VirtualButton::DPadDown)) navDown = true;
        if (pad.WasJustPressed(VirtualButton::A) || pad.WasJustPressed(VirtualButton::Plus)) confirm = true;
        if (pad.WasJustPressed(VirtualButton::B))        cancel = true;
        if (pad.WasJustPressed(VirtualButton::X))        rerunTests = true;
    }

    if (cancel) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().GetModalStack().Pop();
        return;
    }
    if (rerunTests) {
        AudioManager::Instance().PlayClick();
        m_selfTestReport = Diagnostics::EngineSelfTestSuite::RunAll();
    }
    if (navUp) {
        AudioManager::Instance().PlayClick();
        m_selectedIndex = (m_selectedIndex + SUITE_COUNT - 1u) % SUITE_COUNT;
    } else if (navDown) {
        AudioManager::Instance().PlayClick();
        m_selectedIndex = (m_selectedIndex + 1u) % SUITE_COUNT;
    } else if (confirm) {
        LaunchSelectedSuite();
        return;
    }
}

void GraphicsQAModal::Render() {
    Render2D::Begin();

    // Velo semitransparente sobre la escena de fondo
    Render2D::DrawBox(0, 0, 1280, 720, ColorRGBA{4, 8, 16, 180}, ColorRGBA{0, 0, 0, 0});

    // Marco principal del modal
    Render2D::DrawBox(kModalX, kModalY, kModalW, kModalH, ColorRGBA{12, 22, 38, 245}, ColorRGBA::Cyan());
    Render2D::DrawPixelString(NELEO_LOC("qa.title"), 265, 74, 3, ColorRGBA::Cyan());
    Render2D::DrawPixelString(NELEO_LOC("qa.subtitle"), 280, 112, 2, ColorRGBA::Yellow());

    for (size_t i = 0; i < SUITE_COUNT; ++i) {
        const auto& suite = m_suites[i];
        const int ry = kListY + static_cast<int>(i) * (kRowH + kRowGap);
        const bool selected = (i == m_selectedIndex);

        const ColorRGBA fill = selected
            ? (suite.implemented ? ColorRGBA{18, 68, 92, 240} : ColorRGBA{52, 42, 26, 235})
            : (suite.implemented ? ColorRGBA{14, 36, 48, 220} : ColorRGBA{22, 28, 38, 200});
        const ColorRGBA border = selected
            ? ColorRGBA::Yellow()
            : (suite.implemented ? ColorRGBA::Cyan() : ColorRGBA{75, 90, 115, 220});

        Render2D::DrawBox(kListX, ry, kRowW, kRowH, fill, border);

        char rowBuf[128];
        std::snprintf(rowBuf, sizeof(rowBuf), "[%s] %s",
                      suite.code, LocalizationManager::Instance().GetCStr(suite.title));
        const ColorRGBA textCol = suite.implemented ? ColorRGBA::White() : ColorRGBA{155, 165, 185, 255};
        Render2D::DrawPixelString(rowBuf, kListX + 14, ry + 8, 2, textCol);

        const char* badge = suite.implemented ? NELEO_LOC_CSTR("qa.badge.active") : NELEO_LOC_CSTR("qa.badge.planned");
        const ColorRGBA badgeCol = suite.implemented ? ColorRGBA{110, 255, 150, 255} : ColorRGBA{215, 175, 95, 255};
        Render2D::DrawPixelString(badge, kListX + 835, ry + 8, 2, badgeCol);

        Render2D::DrawPixelString(suite.refBooks, kListX + 74, ry + 27, 1, ColorRGBA{165, 205, 240, 255});
    }

    if (m_noticeTimer > 0.0f) {
        Render2D::DrawPixelString(NELEO_LOC("qa.notice.planned"),
                                  140, 620, 2, ColorRGBA::Yellow());
    } else {
        uint32_t activeSuites = 0u;
        for (const auto& s : m_suites) {
            if (s.implemented) ++activeSuites;
        }
        char selfTestBuf[128];
        if (m_selfTestReport.AllPassed()) {
            std::snprintf(selfTestBuf, sizeof(selfTestBuf),
                          NELEO_LOC_CSTR("qa.selftest.pass"),
                          m_selfTestReport.passedCount, m_selfTestReport.totalCount,
                          activeSuites, static_cast<unsigned>(SUITE_COUNT));
            Render2D::DrawPixelString(selfTestBuf, 140, 620, 2, ColorRGBA{115, 255, 160, 255});
        } else {
            const auto* fail = m_selfTestReport.GetFirstFailure();
            std::snprintf(selfTestBuf, sizeof(selfTestBuf),
                          NELEO_LOC_CSTR("qa.selftest.fail"),
                          m_selfTestReport.passedCount, m_selfTestReport.totalCount,
                          fail ? fail->name : "UNKNOWN",
                          fail ? fail->detail : "");
            Render2D::DrawPixelString(selfTestBuf, 140, 620, 2, ColorRGBA{255, 95, 95, 255});
        }
    }

    m_closeButton.Render();
    Render2D::End();
}

} // namespace Neleo
