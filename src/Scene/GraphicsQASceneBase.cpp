#include "Scene/GraphicsQASceneBase.hpp"
#include "Scene/SceneMenu.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Core/ResourceMonitor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#if defined(__WIIU__) || defined(__wiiu__)
#include "Core/Window.hpp"
#include <gx2/event.h>
#include <proc_ui/procui.h>
#endif

namespace WiiU {

namespace {
constexpr float kMoveSpeed = 12.0f;
constexpr float kLookSpeed = 1.85f;
constexpr float kMaxPitch  = 1.42f; // ~81 grados
constexpr float kFovYRad   = 1.04719755f; // 60 grados
constexpr float kAspect169 = 1280.0f / 720.0f;
} // namespace

void GraphicsQASceneBase::SetDefaultCamera(const Point3D& pos, float yawRad, float pitchRad) noexcept {
    m_defaultCamPos = pos;
    m_defaultCamYaw = yawRad;
    m_defaultCamPitch = std::clamp(pitchRad, -kMaxPitch, kMaxPitch);
    ResetCamera();
}

void GraphicsQASceneBase::ResetCamera() noexcept {
    m_camPos = m_defaultCamPos;
    m_camYaw = m_defaultCamYaw;
    m_camPitch = m_defaultCamPitch;
}

void GraphicsQASceneBase::WaitForGpuIdleSafe() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown() &&
        Window::HasForeground() && !Window::IsExitingProcUI()) {
        GX2DrawDone();
    }
#endif
}

void GraphicsQASceneBase::OnEnter() {
    ForwardRendererGX2::Instance().Initialize();
    m_elapsedTime = 0.0f;
    m_smoothedDt = 0.016667f;
    m_hudVisible = true;
    ResetCamera();
    OnQAEnter();
}

void GraphicsQASceneBase::OnExit() {
    OnQAExit();
    ForwardRendererGX2::Instance().ResetMeshSamplerDefaults();
}

void GraphicsQASceneBase::UpdateCamera(float dt) noexcept {
    float moveForward = 0.0f;
    float moveRight = 0.0f;
    float moveUp = 0.0f;
    float lookYaw = 0.0f;
    float lookPitch = 0.0f;
    bool resetPressed = false;
    bool toggleHud = false;

    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;

        moveRight   += pad.leftStick.x;
        moveForward += pad.leftStick.y;
        lookYaw     += pad.rightStick.x;
        lookPitch   += pad.rightStick.y;

        if (pad.IsHeld(VirtualButton::ZR) || pad.IsHeld(VirtualButton::R)) moveUp += 1.0f;
        if (pad.IsHeld(VirtualButton::ZL) || pad.IsHeld(VirtualButton::L)) moveUp -= 1.0f;
        if (pad.WasJustPressed(VirtualButton::Y)) resetPressed = true;
        if (pad.WasJustPressed(VirtualButton::Plus)) toggleHud = true;
    }

    if (resetPressed) {
        AudioManager::Instance().PlayClick();
        ResetCamera();
        return;
    }
    if (toggleHud) {
        AudioManager::Instance().PlayClick();
        m_hudVisible = !m_hudVisible;
    }

    m_camYaw += std::clamp(lookYaw, -1.0f, 1.0f) * kLookSpeed * dt;
    m_camPitch = std::clamp(m_camPitch + std::clamp(lookPitch, -1.0f, 1.0f) * kLookSpeed * dt,
                            -kMaxPitch, kMaxPitch);

    const float sinY = std::sin(m_camYaw);
    const float cosY = std::cos(m_camYaw);
    const Point3D forward{sinY, 0.0f, -cosY};
    const Point3D right{cosY, 0.0f, sinY};

    const float fwdInput = std::clamp(moveForward, -1.0f, 1.0f);
    const float rgtInput = std::clamp(moveRight, -1.0f, 1.0f);
    const float upInput  = std::clamp(moveUp, -1.0f, 1.0f);

    m_camPos.x += (forward.x * fwdInput + right.x * rgtInput) * kMoveSpeed * dt;
    m_camPos.z += (forward.z * fwdInput + right.z * rgtInput) * kMoveSpeed * dt;
    m_camPos.y += upInput * (kMoveSpeed * 0.75f) * dt;
    if (m_camPos.y < 0.35f) m_camPos.y = 0.35f;
}

void GraphicsQASceneBase::Update(float dt) {
    const float clampedDt = std::clamp(dt, 0.0001f, 0.1f);
    m_elapsedTime += clampedDt;
    m_smoothedDt = m_smoothedDt * 0.90f + clampedDt * 0.10f;

    // Salida unificada al Menú Principal con (B)
    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (pad.isConnected && pad.WasJustPressed(VirtualButton::B)) {
            AudioManager::Instance().PlayClick();
            SceneManager::Instance().ChangeScene(std::make_unique<SceneMenu>());
            return;
        }
    }

    UpdateCamera(clampedDt);
    OnQAUpdate(clampedDt);
}

void GraphicsQASceneBase::Render() {
    const float cosP = std::cos(m_camPitch);
    const float sinP = std::sin(m_camPitch);
    const float sinY = std::sin(m_camYaw);
    const float cosY = std::cos(m_camYaw);

    const Point3D lookDir{sinY * cosP, sinP, -cosY * cosP};
    const Point3D target = m_camPos + lookDir;
    const Matrix4x4 view = Matrix4x4::LookAt(m_camPos, target, Point3D{0.0f, 1.0f, 0.0f});
    const Matrix4x4 proj = Matrix4x4::Perspective(kFovYRad, kAspect169, 0.15f, 300.0f);

    ForwardRendererGX2::Instance().BeginFrame(m_camPos, view, proj);
    OnQARender3D(view, proj);
    ForwardRendererGX2::Instance().EndFrame();

    Render2D::Begin();
    if (m_hudVisible) {
        RenderBaseHUD();
        OnQARenderHUD();
    } else {
        Render2D::DrawBox(18, 16, 360, 32, ColorRGBA{8, 16, 28, 185}, ColorRGBA::Cyan());
        Render2D::DrawPixelString("(+) MOSTRAR HUD QA   (B) MENU", 30, 25, 2, ColorRGBA::Yellow());
    }
    Render2D::End();
}

void GraphicsQASceneBase::RenderBaseHUD() const noexcept {
    // Barra superior de telemetría (Zero-Alloc: buffers en stack + snprintf)
    Render2D::DrawBox(18, 14, 1244, 68, ColorRGBA{8, 16, 28, 210}, ColorRGBA::Cyan());

    char headerBuf[128];
    std::snprintf(headerBuf, sizeof(headerBuf), "[%s] %s", GetSuiteCode(), GetSuiteTitle());
    Render2D::DrawPixelString(headerBuf, 32, 24, 2, ColorRGBA::Cyan());

    const auto& metrics = ResourceMonitor::Instance().GetMetrics();
    const float fps = (m_smoothedDt > 0.0001f) ? (1.0f / m_smoothedDt) : 60.0f;
    const float frameMs = m_smoothedDt * 1000.0f;
    const uint32_t draws = ForwardRendererGX2::Instance().GetDrawCallsCount();
    const uint32_t tris  = ForwardRendererGX2::Instance().GetTrianglesCount();

    char statsBuf[160];
    std::snprintf(statsBuf, sizeof(statsBuf),
                  "FPS:%4.1f (%4.1fMS) | DRAWS:%3u | TRIS:%5u | MEM1:%4.1f%% | MEM2:%4.1f%%",
                  fps, frameMs, draws, tris, metrics.mem1Percent, metrics.mem2Percent);
    Render2D::DrawPixelString(statsBuf, 32, 52, 2, ColorRGBA{140, 255, 175, 255});

    // Barra inferior de controles
    Render2D::DrawBox(18, 654, 1244, 52, ColorRGBA{8, 16, 28, 205}, ColorRGBA{80, 150, 210, 255});
    Render2D::DrawPixelString(GetControlsHint(), 32, 663, 2, ColorRGBA::Yellow());
    Render2D::DrawPixelString("L-STICK:MOVER  R-STICK:ROTAR  ZL/ZR:ALTURA  Y:RESET CAM  +:HUD  B:MENU",
                              32, 685, 2, ColorRGBA{185, 215, 245, 255});
}

} // namespace WiiU
