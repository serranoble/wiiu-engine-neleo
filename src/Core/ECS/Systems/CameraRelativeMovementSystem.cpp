#include "Core/ECS/Systems/CameraRelativeMovementSystem.hpp"
#include <algorithm>
#include <cmath>

namespace Neleo {

Point3D CameraRelativeMovementSystem::ComputeWorldMoveDirection(float stickX, float stickY,
                                                                const Point3D& camForward) noexcept {
    // Proyectar forward de camara en el plano horizontal XZ
    Point3D fwd{camForward.x, 0.0f, camForward.z};
    const float fwdLenSq = fwd.x * fwd.x + fwd.z * fwd.z;
    if (fwdLenSq > 1e-6f) {
        const float invLen = 1.0f / std::sqrt(fwdLenSq);
        fwd.x *= invLen;
        fwd.z *= invLen;
    } else {
        fwd = Point3D{0.0f, 0.0f, -1.0f};
    }

    // En sistema diestro (Right-Handed, +Y arriba): right = normalize(cross(fwd, up)) = (-fwd.z, 0, fwd.x)
    const Point3D right{-fwd.z, 0.0f, fwd.x};

    Point3D move{
        right.x * stickX + fwd.x * stickY,
        0.0f,
        right.z * stickX + fwd.z * stickY
    };

    const float magSq = move.x * move.x + move.z * move.z;
    if (magSq > 1.0f) {
        const float invMag = 1.0f / std::sqrt(magSq);
        move.x *= invMag;
        move.z *= invMag;
    }
    return move;
}

void CameraRelativeMovementSystem::ApplyToCharacter(KinematicCharacterComponent& kcc,
                                                    float stickX, float stickY,
                                                    const Point3D& camForward,
                                                    bool sprint,
                                                    bool jumpTriggered) noexcept {
    const Point3D dir = ComputeWorldMoveDirection(stickX, stickY, camForward);
    kcc.SetMovementInput(dir.x, dir.z, sprint);
    if (jumpTriggered) {
        kcc.RequestJump();
    }
}

void CameraRelativeMovementSystem::ApplyToCharacterYaw(KinematicCharacterComponent& kcc,
                                                       float stickX, float stickY,
                                                       float cameraYawRad,
                                                       bool sprint,
                                                       bool jumpTriggered) noexcept {
    const Point3D camForward{-std::sin(cameraYawRad), 0.0f, -std::cos(cameraYawRad)};
    ApplyToCharacter(kcc, stickX, stickY, camForward, sprint, jumpTriggered);
}

} // namespace Neleo
