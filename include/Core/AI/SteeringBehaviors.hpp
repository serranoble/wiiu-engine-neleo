#pragma once

#include "Graphics/Math3D.hpp"

namespace Neleo {

/**
 * @brief Sub-sistema Cinemático y Comportamientos de Dirección (Steering Behaviors).
 * 
 * @author Wii U Game Engine Architecture Team
 * @date Noviembre 2026 (Hito 120 - Capítulo 19 Jason Gregory)
 * 
 * @par Fundamento Matemático y Literatura:
 * - **Craig W. Reynolds** (*"Steering Behaviors For Autonomous Characters"*, Game Developers Conference 1999).
 * - **Jason Gregory** (*Game Engine Architecture*, 3ª Edición, Cap. 19.1.3 "Steering Behaviors", pp. 1025-1035).
 * 
 * @par Cero Asignaciones Dinámicas (0 Heap Allocs a 60 FPS):
 * Métodos estáticos puros tipo POD que reciben vectores y escalares directamente por valor/referencia const,
 * ejecutando matemáticas vectoriales SIMD o escalares de alta velocidad.
 */
class SteeringBehaviors {
public:
    /**
     * @brief Comportamiento Seek (Perseguir): Acelera hacia el objetivo a velocidad máxima.
     * @param currentPos Posición actual del actor.
     * @param targetPos Posición del objetivo hacia el que se avanza.
     * @param currentVel Vector de velocidad actual.
     * @param maxSpeed Velocidad crucero máxima.
     * @return Fuerza de dirección calculada $\mathbf{f}_{\text{seek}} = \mathbf{v}_{\text{deseada}} - \mathbf{v}_{\text{actual}}$.
     */
    [[nodiscard]] static Point3D Seek(const Point3D& currentPos, 
                                      const Point3D& targetPos, 
                                      const Point3D& currentVel, 
                                      float maxSpeed) noexcept;

    /**
     * @brief Comportamiento Flee (Huir): Acelera en sentido opuesto a una amenaza.
     */
    [[nodiscard]] static Point3D Flee(const Point3D& currentPos, 
                                      const Point3D& threatPos, 
                                      const Point3D& currentVel, 
                                      float maxSpeed) noexcept;

    /**
     * @brief Comportamiento Arrive (Llegar): Reduce progresivamente la velocidad dentro del radio de deceleración.
     * @param currentPos Posición actual.
     * @param targetPos Coordenada final de llegada.
     * @param currentVel Velocidad actual.
     * @param maxSpeed Velocidad máxima antes de decelerar.
     * @param slowingRadius Radio métrico a partir del cual comienza el frenado gradual.
     * @return Vector de fuerza de dirección hacia el objetivo.
     */
    [[nodiscard]] static Point3D Arrive(const Point3D& currentPos, 
                                        const Point3D& targetPos, 
                                        const Point3D& currentVel, 
                                        float maxSpeed, 
                                        float slowingRadius) noexcept;

    /**
     * @brief Calcula la orientación Yaw (en radianes) orientada hacia el vector de velocidad.
     * @param velocity Vector de movimiento (plano XZ).
     * @param fallbackYaw Ángulo de respaldo en caso de velocidad nula.
     * @return Ángulo de rotación Yaw en radianes.
     */
    [[nodiscard]] static float VelocityToYaw(const Point3D& velocity, float fallbackYaw = 0.0f) noexcept;

    /**
     * @brief Evalúa si un punto objetivo se encuentra dentro del cono sensorial de visión 3D.
     * @param observerPos Posición de los ojos del observador.
     * @param observerYawRad Orientación horizontal del observador (radianes).
     * @param targetPos Posición del objeto a percibir.
     * @param maxRange Distancia máxima de visión.
     * @param fovDeg Ángulo de apertura del cono de visión (grados, ej. 90.0f).
     * @return true si el objetivo cae dentro del sector circular; false en caso contrario.
     */
    [[nodiscard]] static bool IsInSightCone(const Point3D& observerPos, 
                                            float observerYawRad, 
                                            const Point3D& targetPos, 
                                            float maxRange, 
                                            float fovDeg) noexcept;
};

} // namespace Neleo
