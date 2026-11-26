#pragma once

#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/ColliderComponent.hpp"
#include "Core/ECS/Components/TriggerComponent.hpp"
#include "Core/ECS/Components/KinematicCharacterComponent.hpp"

namespace Neleo {

/**
 * @brief [Hito 116] Sistema ECS de Detección y Resolución de Colisiones y Triggers (CollisionSystem).
 * 
 * @author Wii U Game Engine Architecture Team
 * @date Noviembre 2026 (Hito 116 & 119)
 * 
 * @par Fundamentos Matemáticos y Literatura de Referencia:
 * 1. **Christer Ericson** (*Real-Time Collision Detection*, Morgan Kaufmann 2004):
 *    - Cap. 4.3.1: "Testing Intersecting Spheres".
 *    - Cap. 5.1.3: "Closest Point on AABB to Point" & "Testing Sphere Against AABB".
 *    - Cap. 7.1: "Minimum Translation Vector (MTV) Penetration Resolution".
 * 2. **James Arvo** (*Graphics Gems*, Academic Press 1990):
 *    - "A Simple Method for Box-Sphere Intersection Testing" (Clamping analítico por coordenadas ortogonales).
 * 3. **Jason Gregory** (*Game Engine Architecture*, 3ª Edición):
 *    - Cap. 13.5: "Kinematic Character Controllers & Tangential Slide Response"
 *      ($\mathbf{v}' = \mathbf{v} - (\mathbf{v} \cdot \mathbf{n})\mathbf{n}$).
 *    - Cap. 13.7 & 16: "Trigger Volumes & Edge-Triggered State Machines".
 * 
 * @par Invariantes de Rendimiento y Silicio:
 * - **Cero Asignaciones de Memoria:** 0 bytes `malloc`/`new` por fotograma en bucle a 60 FPS.
 * - **Estabilidad Numérica:** Guarda defensiva contra divisiones por cero en vectores normales nulos.
 * - **Deslizamiento Suave:** Resuelve la expulsión y cancela la componente normal de velocidad evitando atascos en esquinas.
 */
class CollisionSystem {
public:
    /**
     * @brief Ejecuta el pase por frame de resolución física de penetración y evaluación de triggers sobre el mundo ECS.
     * 
     * @param world Referencia al mundo `ECSWorld` activo.
     * @param dt Delta time en segundos transcurridos desde el último frame (~0.0166s).
     */
    static void Update(ECSWorld& world, float dt) noexcept;

    /**
     * @brief Prueba analítica exacta de intersección Esfera contra Esfera.
     * 
     * @author Christer Ericson (Real-Time Collision Detection, Cap. 4.3.1)
     * @param centerA Centro tridimensional de la primera esfera en espacio de mundo.
     * @param radiusA Radio métrico de la primera esfera ($r_A > 0$).
     * @param centerB Centro tridimensional de la segunda esfera.
     * @param radiusB Radio métrico de la segunda esfera ($r_B > 0$).
     * @param[out] outNormal Vector normal unitario de contacto apuntando desde B hacia A.
     * @param[out] outPenetration Profundidad de penetración escalar $(r_A + r_B) - \|\mathbf{c}_A - \mathbf{c}_B\|$.
     * @return true si las esferas se solapan o intersectan; false si están separadas.
     */
    static bool IntersectSphereSphere(const Point3D& centerA, float radiusA,
                                      const Point3D& centerB, float radiusB,
                                      Point3D& outNormal, float& outPenetration) noexcept;

    /**
     * @brief Prueba analítica de intersección Esfera contra Caja AABB.
     * 
     * @author James Arvo (Graphics Gems 1990) / Christer Ericson (Cap. 5.1.3)
     * @param sphereCenter Centro tridimensional de la esfera en espacio de mundo.
     * @param sphereRadius Radio métrico de la esfera ($r > 0$).
     * @param boxMin Esquina mínima $(\min_x, \min_y, \min_z)$ de la caja AABB.
     * @param boxMax Esquina máxima $(\max_x, \max_y, \max_z)$ de la caja AABB.
     * @param[out] outNormal Vector normal unitario de expulsión (MTV) apuntando hacia el exterior del AABB.
     * @param[out] outPenetration Magnitud de penetración resuelta en metros.
     * @return true si la esfera penetra o toca el volumen del AABB; false si es completamente externa.
     * 
     * @note Maneja analíticamente tanto el caso exterior (la esfera colisiona con una cara, arista o vértice)
     * como el caso interior crítico (el centro de la esfera está dentro del AABB, expulsando por la cara más próxima).
     */
    static bool IntersectSphereAABB(const Point3D& sphereCenter, float sphereRadius,
                                    const Point3D& boxMin, const Point3D& boxMax,
                                    Point3D& outNormal, float& outPenetration) noexcept;
};

} // namespace Neleo
