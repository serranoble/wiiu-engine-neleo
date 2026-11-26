#pragma once

#include "Graphics/Math3D.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace Neleo {

class ECSWorld;

/**
 * @brief [Hito 133] OrbitCameraSystem: Sistema avanzado de cámara cinematográfica en 3ª persona (Cap. 17).
 *
 * Gestiona con compatibilidad total hacia atrás:
 * - Rotación orbital horizontal (Yaw) y vertical (Pitch) suave con Stick Derecho.
 * - Zoom continuo (Distancia a target) con botones ZL/ZR o Stick Izquierdo.
 * - Spring-Arm Raycast analítico Ray-AABB y Ray-Sphere contra `Layer_StaticWorld`.
 * - Anticipación de foco por velocidad (`Velocity Look-Ahead`).
 * - Trauma Camera Shake cuadrático ($T^2$) determinista.
 * - Cámara sobre Riel Spline Cúbico de Catmull-Rom ($C^1$) hasta 8 puntos de control.
 */
class OrbitCameraSystem {
public:
    static constexpr uint32_t MAX_SPLINE_POINTS = 8;

    OrbitCameraSystem() noexcept = default;
    ~OrbitCameraSystem() = default;

    void Initialize() {}
    void Update(ECSWorld& world, float deltaTime);

    void SetTarget(const Point3D& target) noexcept { m_target = target; }
    [[nodiscard]] Point3D GetTarget() const noexcept { return m_target; }

    void SetDistance(float dist) noexcept {
        m_distance = dist;
        if (!m_collisionEnabled) {
            m_effectiveDistance = dist;
        }
    }
    [[nodiscard]] float GetDistance() const noexcept { return m_distance; }

    void SetAngles(float yawDeg, float pitchDeg) noexcept {
        m_yaw = yawDeg;
        m_pitch = pitchDeg;
    }
    [[nodiscard]] float GetYaw() const noexcept { return m_yaw; }
    [[nodiscard]] float GetPitch() const noexcept { return m_pitch; }
    void SetLeftStickZoomEnabled(bool enabled) noexcept { m_leftStickZoom = enabled; }

    // -------------------------------------------------------------------------
    // Hito 133 (Cap. 17): Spring-Arm Collision, Look-Ahead, Trauma & Spline Rail
    // -------------------------------------------------------------------------
    void SetCollisionEnabled(bool enabled) noexcept {
        m_collisionEnabled = enabled;
        if (!enabled) {
            m_effectiveDistance = m_distance;
            m_isCompressing = false;
        }
    }
    [[nodiscard]] bool IsCollisionEnabled() const noexcept { return m_collisionEnabled; }
    [[nodiscard]] float GetEffectiveDistance() const noexcept { return m_effectiveDistance; }
    [[nodiscard]] bool IsSpringArmCompressing() const noexcept { return m_isCompressing; }

    void SetTargetVelocity(const Point3D& velocity) noexcept { m_targetVelocity = velocity; }
    [[nodiscard]] Point3D GetLookAheadOffset() const noexcept { return m_lookAheadOffset; }

    void AddTrauma(float amount) noexcept {
        m_trauma = std::clamp(m_trauma + amount, 0.0f, 1.0f);
    }
    [[nodiscard]] float GetTrauma() const noexcept { return m_trauma; }

    void SetSplineControlPoints(const Point3D* points, uint32_t count) noexcept;
    void SetSplineRailActive(bool active) noexcept {
        m_splineRailActive = (active && m_splineCount >= 2);
    }
    [[nodiscard]] bool IsSplineRailActive() const noexcept { return m_splineRailActive; }
    [[nodiscard]] Point3D EvaluateSplineAt(float tNormalized) const noexcept;

    [[nodiscard]] Point3D GetPosition() const noexcept { return m_camPos; }
    [[nodiscard]] Matrix4x4 GetViewMatrix() const noexcept { return m_viewMatrix; }

    void Reset() noexcept {
        m_yaw = 0.0f;
        m_pitch = 14.0f;
        m_distance = 8.8f;
        m_effectiveDistance = 8.8f;
        m_isCompressing = false;
        m_trauma = 0.0f;
        m_lookAheadOffset = Point3D{0.0f, 0.0f, 0.0f};
        m_targetVelocity = Point3D{0.0f, 0.0f, 0.0f};
        m_splineRailActive = false;
        m_splineT = 0.0f;
    }

    void AdjustZoom(float delta) noexcept {
        m_distance += delta;
        if (m_distance < 2.0f) m_distance = 2.0f;
        if (m_distance > 30.0f) m_distance = 30.0f;
    }

private:
    Point3D m_target{0.0f, 1.8f, 0.0f};
    float m_yaw{0.0f};
    float m_pitch{14.0f};
    float m_distance{8.8f};
    bool m_leftStickZoom{true};

    // Spring-Arm collision state
    bool m_collisionEnabled{false};
    bool m_isCompressing{false};
    float m_effectiveDistance{8.8f};
    float m_minArmLength{1.5f};
    float m_probeRadius{0.28f};
    float m_minGroundY{0.20f};

    // Velocity look-ahead
    Point3D m_targetVelocity{0.0f, 0.0f, 0.0f};
    Point3D m_lookAheadOffset{0.0f, 0.0f, 0.0f};

    // Trauma shake
    float m_trauma{0.0f};
    float m_shakeTime{0.0f};

    // Closed Catmull-Rom Spline Rail
    std::array<Point3D, MAX_SPLINE_POINTS> m_splinePoints{};
    uint32_t m_splineCount{0};
    bool m_splineRailActive{false};
    float m_splineT{0.0f};
    float m_splineSpeed{0.075f};

    Point3D m_camPos{0.0f, 2.5f, 8.8f};
    Matrix4x4 m_viewMatrix{};
};

} // namespace Neleo
