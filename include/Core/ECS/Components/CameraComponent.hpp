#pragma once

#include "Graphics/Math3D.hpp"
#include <cstdint>

namespace WiiU {

enum class CameraProjectionType : uint8_t {
    Perspective = 0,
    Orthographic
};

enum class DisplayTarget : uint8_t {
    Both = 0,
    TV,
    DRC
};

struct ViewportRect {
    float x{0.0f};
    float y{0.0f};
    float width{1280.0f};
    float height{720.0f};
    float minDepth{0.0f};
    float maxDepth{1.0f};
};

/**
 * @brief [OPT-253, OPT-257] Componente de Cámara 3D orientado a datos.
 * 
 * Modela los parámetros de proyección, frustum y matrices de vista/proyección.
 * Centraliza el cálculo de cámaras y habilita vistas múltiples asimétricas (TV / DRC GamePad).
 */
struct CameraComponent {
    float fov{65.0f};         // Grados (Field of View)
    float nearClip{0.25f};
    float farClip{120.0f};
    float aspectRatio{1280.0f / 720.0f};
    Point3D target{0.0f, 0.8f, 0.0f};
    Point3D up{0.0f, 1.0f, 0.0f};
    Matrix4x4 viewMatrix{Matrix4x4::Identity()};
    Matrix4x4 projMatrix{Matrix4x4::Identity()};
    Matrix4x4 viewProjMatrix{Matrix4x4::Identity()};
    CameraProjectionType projectionType{CameraProjectionType::Perspective};
    DisplayTarget displayTarget{DisplayTarget::Both};
    ViewportRect viewport{0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f};
    bool isPrimary{true};
    bool isDirty{true};

    constexpr CameraComponent() noexcept = default;

    constexpr CameraComponent(float fovDeg, float aspect, float nClip, float fClip,
                             const Point3D& camTarget = {0.0f, 0.8f, 0.0f},
                             DisplayTarget targetDisp = DisplayTarget::Both) noexcept
        : fov(fovDeg), nearClip(nClip), farClip(fClip), aspectRatio(aspect),
          target(camTarget), up{0.0f, 1.0f, 0.0f},
          viewMatrix(Matrix4x4::Identity()), projMatrix(Matrix4x4::Identity()),
          viewProjMatrix(Matrix4x4::Identity()),
          projectionType(CameraProjectionType::Perspective),
          displayTarget(targetDisp),
          viewport{0.0f, 0.0f, (targetDisp == DisplayTarget::DRC ? 854.0f : 1280.0f),
                               (targetDisp == DisplayTarget::DRC ? 480.0f : 720.0f), 0.0f, 1.0f},
          isPrimary(targetDisp != DisplayTarget::DRC), isDirty(true) {}

    void MarkDirty() noexcept {
        isDirty = true;
    }
};

} // namespace WiiU
