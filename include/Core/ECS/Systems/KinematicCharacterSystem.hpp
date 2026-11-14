#pragma once

#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/KinematicCharacterComponent.hpp"
#include "Graphics/Math3D.hpp"

namespace WiiU {

/**
 * @brief [OPT-207] Sistema Cinemático de Control de Personaje y Físicas Reactivas (KinematicCharacterSystem).
 * 
 * Implementa la simulación cinemática del jugador y actores móviles sobre AMD Latte / IBM Espresso:
 * 1. Integración de aceleración, gravedad e impulsos de salto sin inestabilidades de fuerzas de masa.
 * 2. Detección continua de suelo y límite de pendientes (slope limit ~45 grados).
 * 3. Barrido de colisión continuo (Swept Capsule / Slide Response) contra terreno, muralla y peñascos.
 * 4. Trazado analítico de oclusión acústica (Line of Sight Raycast) para el motor AudioEngine3D.
 * 5. Cero asignaciones dinámicas de memoria (0 bytes malloc/new) en el bucle interactivo de 60 FPS.
 */
class KinematicCharacterSystem {
public:
    // Geometría canónica de obstáculos en el entorno
    struct ObstacleAABB {
        Point3D min;
        Point3D max;
    };

    struct ObstacleSphere {
        Point3D center;
        float radius;
    };

    static constexpr ObstacleAABB WALL_AABB{
        Point3D{-6.0f, 0.45f, -3.2f},
        Point3D{-1.0f, 1.45f, -2.4f}
    };

    static constexpr ObstacleSphere ROCK1_SPHERE{
        Point3D{-3.5f, 0.65f, 3.8f},
        1.45f
    };

    static constexpr ObstacleSphere ROCK2_SPHERE{
        Point3D{6.2f, -0.45f, 6.4f},
        1.25f
    };

    /**
     * @brief Actualiza la cinemática de todas las entidades con (TransformComponent, KinematicCharacterComponent).
     */
    static void Update(ECSWorld& world, float dt);

    /**
     * @brief Evalúa la elevación analítica continua del mapa de alturas del terreno en (x, z).
     */
    [[nodiscard]] static float GetTerrainHeight(float x, float z) noexcept;

    /**
     * @brief Evalúa el vector normal unitario de la superficie del terreno en (x, z).
     */
    [[nodiscard]] static Point3D GetTerrainNormal(float x, float z) noexcept;

    /**
     * @brief Resuelve la colisión continua de barrido (Swept Collision) y deslizamiento (Slide).
     * Modifica in-situ la posición y el vector de velocidad horizontal ante impactos.
     */
    static void ResolveSweptCollision(Point3D& pos, Point3D& vel, float radius, float height, float dt) noexcept;

    /**
     * @brief Trazado analítico de línea de visión acústica entre un emisor y un oyente.
     * Evalúa oclusión contra el terreno intermedio, la muralla de ladrillos y los peñascos.
     * @param from Posición del emisor o receptor.
     * @param to Posición del receptor o emisor.
     * @param outOcclusion Factor de oclusión acústica resultante [0.0 = despejado, 1.0 = ocluido].
     * @return True si existe obstrucción física en la línea de visión.
     */
    static bool CheckLineOfSight(const Point3D& from, const Point3D& to, float& outOcclusion) noexcept;
};

} // namespace WiiU
