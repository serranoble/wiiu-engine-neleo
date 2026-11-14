#pragma once

#include "Graphics/Math3D.hpp"
#include <cstdint>
#include <algorithm>

namespace WiiU {

/**
 * @brief [OPT-207] Componente de Controlador Cinemático de Personaje (Kinematic Character Component).
 * 
 * Modela el estado dinámico y físico de un actor/personaje controlable en 3D:
 * - Velocidad lineal tridimensional (m/s).
 * - Dirección de desplazamiento intencional relativa al espacio del mundo.
 * - Parámetros de cápsula física envolvente (radio y altura total).
 * - Detección continua de suelo (grounded), inclinación de pendientes (slope limit), gravedad y salto.
 * - Acumuladores de cinemática para síntesis de audio de pasos y futuros blend trees de animación esquelética.
 * 
 * Implementado bajo principios de Jason Gregory (Game Engine Architecture, 3rd Ed., Cap. 13 & 14):
 * - Control puramente cinemático (Swept Capsule & Slide) inmune a oscilaciones de integración por fuerzas rígidas.
 * - Estructura Data-Oriented POD contigua y alineada a líneas de caché L1D del procesador Espresso PPC 750CL.
 */
struct KinematicCharacterComponent {
    Point3D velocity{0.0f, 0.0f, 0.0f};          // Velocidad tridimensional actual (m/s)
    Point3D desiredMove{0.0f, 0.0f, 0.0f};       // Vector de movimiento solicitado (m/s) en espacio de mundo
    Point3D groundNormal{0.0f, 1.0f, 0.0f};      // Normal de la superficie de contacto actual
    float groundDistance{0.0f};                  // Distancia vertical a la superficie de apoyo

    // Dimensiones de la cápsula de colisión
    float capsuleRadius{0.35f};                  // Radio envolvente horizontal (m)
    float capsuleHeight{1.75f};                  // Altura total de la cápsula (m)

    // Parámetros de locomoción cinemática
    float walkSpeed{4.0f};                       // Velocidad de caminata estándar (m/s)
    float runSpeed{7.5f};                        // Velocidad de carrera / sprint (m/s)
    float jumpSpeed{5.8f};                       // Impulso inicial de salto vertical (m/s)
    float gravity{16.0f};                        // Aceleración gravitatoria descendente (m/s^2)
    float slopeLimitCos{0.707f};                 // Coseno del ángulo límite transitable (~45 grados)
    float stepOffset{0.30f};                     // Altura máxima de escalón franqueable (m)

    // Banderas de estado cinemático
    bool isGrounded{false};                      // True si está apoyado firmemente en suelo transitable
    bool isJumping{false};                       // True si está en fase ascendente activa de salto
    bool isRunning{false};                       // True si el modo de carrera/sprint está habilitado
    bool jumpRequested{false};                   // Solicitud pendiente de salto consumible por el sistema

    // Cinemática y telemetría
    float horizontalSpeed{0.0f};                 // Magnitud de velocidad horizontal en el plano XZ (m/s)
    float facingAngle{0.0f};                     // Ángulo de orientación horizontal en radianes (Yaw)

    // Acústica y cinemática de pasos
    float footstepDistanceAccum{0.0f};           // Metros recorridos desde el último paso sonoro
    float footstepInterval{1.6f};                // Intervalo en metros para disparar audio de pisada

    constexpr KinematicCharacterComponent() noexcept = default;

    constexpr explicit KinematicCharacterComponent(float radius, float height,
                                                  float walkSpd = 4.0f, float runSpd = 7.5f,
                                                  float jumpSpd = 5.8f, float grav = 16.0f) noexcept
        : capsuleRadius(radius), capsuleHeight(height),
          walkSpeed(walkSpd), runSpeed(runSpd),
          jumpSpeed(jumpSpd), gravity(grav) {}

    void RequestJump() noexcept {
        jumpRequested = true;
    }

    void SetMovementInput(float moveX, float moveZ, bool sprint) noexcept {
        isRunning = sprint;
        float targetSpeed = sprint ? runSpeed : walkSpeed;
        desiredMove = Point3D{moveX * targetSpeed, 0.0f, moveZ * targetSpeed};
    }

    void ResetVelocity() noexcept {
        velocity = Point3D{0.0f, 0.0f, 0.0f};
        desiredMove = Point3D{0.0f, 0.0f, 0.0f};
        horizontalSpeed = 0.0f;
    }
};

} // namespace WiiU
