#include "Core/ECS/Systems/OrbitCameraSystem.hpp"
#include "Core/Physics/CameraRaycastHelper.hpp"
#include "Input/InputManager.hpp"
#include <algorithm>
#include <cmath>

namespace Neleo {

void OrbitCameraSystem::SetSplineControlPoints(const Point3D* points, uint32_t count) noexcept {
    if (!points || count == 0) {
        m_splineCount = 0;
        m_splineRailActive = false;
        return;
    }
    m_splineCount = std::min(count, MAX_SPLINE_POINTS);
    for (uint32_t i = 0; i < m_splineCount; ++i) {
        m_splinePoints[i] = points[i];
    }
    m_splineT = 0.0f;
}

Point3D OrbitCameraSystem::EvaluateSplineAt(float tNormalized) const noexcept {
    if (m_splineCount == 0) {
        return m_camPos;
    }
    if (m_splineCount == 1) {
        return m_splinePoints[0];
    }

    const float wrapped = tNormalized - std::floor(tNormalized);
    const float scaled = wrapped * static_cast<float>(m_splineCount);
    const uint32_t seg = static_cast<uint32_t>(scaled) % m_splineCount;
    const float u = scaled - std::floor(scaled);

    const uint32_t i0 = (seg + m_splineCount - 1) % m_splineCount;
    const uint32_t i1 = seg;
    const uint32_t i2 = (seg + 1) % m_splineCount;
    const uint32_t i3 = (seg + 2) % m_splineCount;

    const Point3D& p0 = m_splinePoints[i0];
    const Point3D& p1 = m_splinePoints[i1];
    const Point3D& p2 = m_splinePoints[i2];
    const Point3D& p3 = m_splinePoints[i3];

    const float u2 = u * u;
    const float u3 = u2 * u;

    // Interpolacion C^1 estandar de Catmull-Rom (tau = 0.5)
    auto evalAxis = [u, u2, u3](float v0, float v1, float v2, float v3) noexcept -> float {
        return 0.5f * ((2.0f * v1) +
                       (-v0 + v2) * u +
                       (2.0f * v0 - 5.0f * v1 + 4.0f * v2 - v3) * u2 +
                       (-v0 + 3.0f * v1 - 3.0f * v2 + v3) * u3);
    };

    return Point3D{
        evalAxis(p0.x, p1.x, p2.x, p3.x),
        evalAxis(p0.y, p1.y, p2.y, p3.y),
        evalAxis(p0.z, p1.z, p2.z, p3.z)
    };
}

void OrbitCameraSystem::Update(ECSWorld& world, float deltaTime) {
    const float dt = std::max(0.0f, deltaTime);
    const auto& input = InputManager::Instance();

    // 1. Trauma Camera Shake decay (T^2 response)
    if (m_trauma > 0.0f) {
        m_shakeTime += dt;
        m_trauma = std::max(0.0f, m_trauma - 1.35f * dt);
    }
    const float shake = m_trauma * m_trauma;
    const float shakeYawDeg = shake * 3.8f * std::sin(m_shakeTime * 37.0f);
    const float shakePitchDeg = shake * 2.6f * std::cos(m_shakeTime * 43.0f);
    const Point3D shakePosOffset{
        shake * 0.16f * std::sin(m_shakeTime * 53.0f),
        shake * 0.12f * std::cos(m_shakeTime * 61.0f),
        shake * 0.14f * std::sin(m_shakeTime * 47.0f)
    };

    // 2. Velocity Look-Ahead suavizado en plano XZ
    const Point3D desiredLookAhead{
        std::clamp(m_targetVelocity.x * 0.18f, -1.35f, 1.35f),
        0.0f,
        std::clamp(m_targetVelocity.z * 0.18f, -1.35f, 1.35f)
    };
    const float lookAlpha = std::clamp(1.0f - std::exp(-5.5f * dt), 0.0f, 1.0f);
    m_lookAheadOffset.x += (desiredLookAhead.x - m_lookAheadOffset.x) * lookAlpha;
    m_lookAheadOffset.z += (desiredLookAhead.z - m_lookAheadOffset.z) * lookAlpha;

    const Point3D focusPoint = m_target + m_lookAheadOffset;

    // 3. Modo Riel Spline C^1 de Catmull-Rom
    if (m_splineRailActive && m_splineCount >= 2) {
        m_splineT += m_splineSpeed * dt;
        if (m_splineT >= 1.0f) {
            m_splineT -= std::floor(m_splineT);
        }
        m_camPos = EvaluateSplineAt(m_splineT) + shakePosOffset;
        m_isCompressing = false;
        m_effectiveDistance = (m_camPos - focusPoint).Length();
        m_viewMatrix = Matrix4x4::LookAt(m_camPos, focusPoint + shakePosOffset, Point3D{0.0f, 1.0f, 0.0f});
        return;
    }

    // 4. Órbita manual con Stick Derecho
    const auto stickR = input.GetRightStick();
    if (std::abs(stickR.x) > 0.15f) {
        m_yaw += stickR.x * 110.0f * dt;
    }
    if (std::abs(stickR.y) > 0.15f) {
        m_pitch += stickR.y * 75.0f * dt;
        if (m_pitch < -15.0f) m_pitch = -15.0f;
        if (m_pitch > 75.0f) m_pitch = 75.0f;
    }

    // 5. Zoom con Stick Izquierdo Y (si no está reservado para locomoción de jugador)
    if (m_leftStickZoom) {
        const auto stickL = input.GetLeftStick();
        if (std::abs(stickL.y) > 0.20f) {
            AdjustZoom(-stickL.y * 5.0f * dt);
        }
    }

    // 6. Cálculo de Dirección Esférica (en yaw=0 la cámara está en -Z mirando hacia +Z)
    const float effYaw = m_yaw + shakeYawDeg;
    const float effPitch = std::clamp(m_pitch + shakePitchDeg, -18.0f, 78.0f);
    const float radYaw = effYaw * 0.0174532925f;
    const float radPitch = effPitch * 0.0174532925f;

    const float cosPitch = std::cos(radPitch);
    const float sinPitch = std::sin(radPitch);
    const float sinYaw = std::sin(radYaw);
    const float cosYaw = std::cos(radYaw);

    const Point3D rayDir{
        cosPitch * sinYaw,
        sinPitch,
        -cosPitch * cosYaw
    };

    // 7. Spring-Arm Raycast contra Layer_StaticWorld si collisionEnabled está activo
    if (m_collisionEnabled) {
        const float maxAllowed = CameraRaycastHelper::SweepWorld(
            world,
            Entity::Null,
            focusPoint,
            rayDir.Normalized(),
            m_distance,
            m_minArmLength,
            m_probeRadius,
            m_minGroundY,
            false
        );
        m_isCompressing = (maxAllowed < m_distance - 0.08f);
        const float damping = m_isCompressing ? 24.0f : 7.0f;
        const float armAlpha = (dt > 0.0f) ? std::clamp(1.0f - std::exp(-damping * dt), 0.0f, 1.0f) : 1.0f;
        m_effectiveDistance += (maxAllowed - m_effectiveDistance) * armAlpha;
    } else {
        m_isCompressing = false;
        m_effectiveDistance = m_distance;
    }

    m_camPos = focusPoint + rayDir * m_effectiveDistance + shakePosOffset;

    // 8. LookAt View Matrix
    m_viewMatrix = Matrix4x4::LookAt(m_camPos, focusPoint + shakePosOffset, Point3D{0.0f, 1.0f, 0.0f});
}

} // namespace Neleo
