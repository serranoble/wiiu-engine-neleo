#pragma once

#include "Graphics/Math3D.hpp"
#include <cmath>

namespace WiiU {

/**
 * @brief [OPT-302] Componente Reutilizable de Plataforma Giratoria y Cámara de Estudio.
 * 
 * Basado en Jason Gregory (Game Engine Architecture, 3rd Ed., Cap. 16)
 * y Mark Haigh-Hutchinson (Real-Time Cameras).
 * Proporciona rotación orbital suave, amortiguamiento exponencial acoplado
 * y auto-rotación desacoplada para visores de modelos, galerías y trofeos 3D.
 */
struct StudioTurntableComponent {
    // Ángulos actuales (Radianes)
    float yaw{0.65f};
    float pitch{0.28f};
    float distance{10.5f};
    Point3D target{0.0f, 2.55f, 0.0f};

    // Objetivos para suavizado exponencial
    float targetYaw{0.65f};
    float targetPitch{0.28f};
    float targetDistance{10.5f};
    Point3D targetTarget{0.0f, 2.55f, 0.0f};

    // Parámetros de auto-rotación (Turntable)
    float autoRotateSpeed{0.28f}; // Radianes / segundo
    bool autoRotate{false};

    // Rangos límites
    float minPitch{-0.35f};
    float maxPitch{1.35f};
    float minDistance{2.5f};
    float maxDistance{40.0f};
    float smoothFactor{12.0f};

    // Estado táctil/arrastre
    bool isDragging{false};

    constexpr StudioTurntableComponent() noexcept = default;

    constexpr StudioTurntableComponent(float initYaw, float initPitch, float initDist,
                                       const Point3D& initTarget, bool autoRot = false) noexcept
        : yaw(initYaw), pitch(initPitch), distance(initDist), target(initTarget),
          targetYaw(initYaw), targetPitch(initPitch), targetDistance(initDist), targetTarget(initTarget),
          autoRotateSpeed(0.28f), autoRotate(autoRot),
          minPitch(-0.35f), maxPitch(1.35f), minDistance(2.5f), maxDistance(40.0f),
          smoothFactor(12.0f), isDragging(false) {}

    [[nodiscard]] Point3D ComputeEyePosition() const noexcept {
        float cosPitch = std::cos(pitch);
        float sinPitch = std::sin(pitch);
        float sinYaw = std::sin(yaw);
        float cosYaw = std::cos(yaw);
        return Point3D{
            target.x + distance * sinYaw * cosPitch,
            target.y + distance * sinPitch,
            target.z - distance * cosYaw * cosPitch
        };
    }
};

} // namespace WiiU
