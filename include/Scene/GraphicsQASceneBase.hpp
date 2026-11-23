#pragma once

#include "Scene/IScene.hpp"
#include "Graphics/Math3D.hpp"
#include <cstdint>

namespace WiiU {

/**
 * @brief Base reutilizable para las Suites de Validación Gráfica (G1..G8).
 *
 * Provee:
 * - Cámara libre 3D controlada por GamePad/Pro/PC (L-Stick mueve, R-Stick rota, ZL/ZR altura, Y resetea).
 * - Salida unificada al menú principal con (B).
 * - HUD compacto Zero-Alloc (0 heap allocs a 60 FPS) con FPS, ms/frame, Draw Calls, Triángulos y parámetros de prueba.
 */
class GraphicsQASceneBase : public IScene {
public:
    GraphicsQASceneBase() noexcept = default;
    ~GraphicsQASceneBase() override = default;

    GraphicsQASceneBase(const GraphicsQASceneBase&) = delete;
    GraphicsQASceneBase& operator=(const GraphicsQASceneBase&) = delete;
    GraphicsQASceneBase(GraphicsQASceneBase&&) = delete;
    GraphicsQASceneBase& operator=(GraphicsQASceneBase&&) = delete;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render() override;

protected:
    // Hooks específicos por suite (G1..G8)
    virtual void OnQAEnter() {}
    virtual void OnQAExit() {}
    virtual void OnQAUpdate(float dt) = 0;
    virtual void OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) = 0;
    virtual void OnQARenderHUD() {}

    [[nodiscard]] virtual const char* GetSuiteCode() const noexcept = 0;
    [[nodiscard]] virtual const char* GetSuiteTitle() const noexcept = 0;
    [[nodiscard]] virtual const char* GetControlsHint() const noexcept = 0;

    void SetDefaultCamera(const Point3D& pos, float yawRad, float pitchRad) noexcept;
    void ResetCamera() noexcept;
    static void WaitForGpuIdleSafe() noexcept;

    [[nodiscard]] const Point3D& GetCameraPos() const noexcept { return m_camPos; }
    [[nodiscard]] float GetCameraYaw() const noexcept { return m_camYaw; }
    [[nodiscard]] float GetCameraPitch() const noexcept { return m_camPitch; }
    [[nodiscard]] float GetElapsedTime() const noexcept { return m_elapsedTime; }

private:
    void UpdateCamera(float dt) noexcept;
    void RenderBaseHUD() const noexcept;

    Point3D m_defaultCamPos{0.0f, 3.5f, 10.0f};
    float m_defaultCamYaw{0.0f};
    float m_defaultCamPitch{-0.22f};

    Point3D m_camPos{0.0f, 3.5f, 10.0f};
    float m_camYaw{0.0f};
    float m_camPitch{-0.22f};

    float m_elapsedTime{0.0f};
    float m_smoothedDt{0.016667f};
    bool m_hudVisible{true};
};

} // namespace WiiU
