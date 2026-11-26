#pragma once

#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/KinematicCharacterComponent.hpp"
#include "Graphics/Math3D.hpp"

namespace Neleo {

/**
 * @brief [OPT-374] Sistema Reutilizable de Locomocion Cinematica Relativa a Camara.
 *
 * Desacopla la proyeccion de ejes del stick izquierdo sobre la base ortonormal XZ de la camara
 * (forward/right), eliminando matematicas duplicadas y errores de polaridad en escenas 3D.
 */
class CameraRelativeMovementSystem {
public:
    /**
     * @brief Calcula un vector unitario/escalado de direccion en plano XZ relativo al vector camForward.
     * @param stickX Eje horizontal del stick [-1 = izquierda, +1 = derecha].
     * @param stickY Eje vertical del stick [-1 = atras, +1 = adelante].
     * @param camForward Vector forward de la camara en espacio de mundo.
     */
    [[nodiscard]] static Point3D ComputeWorldMoveDirection(float stickX, float stickY,
                                                           const Point3D& camForward) noexcept;

    /**
     * @brief Aplica movimiento relativo a la camara directamente a un KinematicCharacterComponent.
     */
    static void ApplyToCharacter(KinematicCharacterComponent& kcc,
                                 float stickX, float stickY,
                                 const Point3D& camForward,
                                 bool sprint = false,
                                 bool jumpTriggered = false) noexcept;

    /**
     * @brief Version directa por angulo orbital Yaw (radianes) de la camara.
     */
    static void ApplyToCharacterYaw(KinematicCharacterComponent& kcc,
                                    float stickX, float stickY,
                                    float cameraYawRad,
                                    bool sprint = false,
                                    bool jumpTriggered = false) noexcept;
};

} // namespace Neleo
