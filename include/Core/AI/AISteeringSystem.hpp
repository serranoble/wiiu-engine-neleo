#pragma once

#include "Core/ECS/ECSWorld.hpp"
#include "Core/AI/NavGraph.hpp"
#include "Graphics/Math3D.hpp"

namespace WiiU {

/**
 * @brief Sistema ECS de Navegación, Comportamientos Cinemáticos y FSM de IA.
 * 
 * @author Wii U Game Engine Architecture Team
 * @date Noviembre 2026 (Hito 120 - Capítulo 19 Jason Gregory)
 * 
 * Desacoplado bajo arquitectura Data-Oriented Design (DOD):
 * - Itera linealmente sobre tuplas contiguas de (TransformComponent, AIControllerComponent).
 * - Cero alocaciones dinámicas a 60 FPS.
 */
class AISteeringSystem {
public:
    AISteeringSystem() noexcept = default;
    ~AISteeringSystem() noexcept = default;

    /**
     * @brief Actualiza la cognición, percepción sensorial y locomoción de todos los agentes IA en el mundo.
     * @param world Registro central ECS.
     * @param navGraph Grafo espacial de navegación de la escena.
     * @param targetPosition Posición espacial del actor objetivo (ej. jugador humano).
     * @param dt Delta time en segundos (frecuencia típica 1/60s).
     */
    static void Update(ECSWorld& world, 
                       const NavGraph& navGraph, 
                       const Point3D& targetPosition, 
                       float dt) noexcept;
};

} // namespace WiiU
