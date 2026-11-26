#pragma once

#include "Core/AI/NavGraph.hpp"
#include <array>
#include <cstdint>

namespace Neleo {

/**
 * @brief Ruta de navegación resultante del algoritmo de búsqueda de caminos.
 * Almacena una secuencia contigua y fija de waypoints espaciales con cero asignaciones en el heap.
 */
struct NavPath {
    static constexpr size_t MAX_WAYPOINTS = 32;

    std::array<Point3D, MAX_WAYPOINTS> waypoints{};
    std::array<uint32_t, MAX_WAYPOINTS> nodeIds{};
    uint32_t waypointCount{0};
    uint32_t currentIndex{0};
    bool isValid{false};

    void Clear() noexcept {
        waypointCount = 0;
        currentIndex = 0;
        isValid = false;
    }

    [[nodiscard]] bool IsFinished() const noexcept {
        return !isValid || currentIndex >= waypointCount;
    }

    [[nodiscard]] Point3D GetCurrentWaypoint() const noexcept {
        if (currentIndex < waypointCount) {
            return waypoints[currentIndex];
        }
        return (waypointCount > 0) ? waypoints[waypointCount - 1] : Point3D{0, 0, 0};
    }

    bool Advance() noexcept {
        if (currentIndex < waypointCount) {
            ++currentIndex;
            return currentIndex < waypointCount;
        }
        return false;
    }
};

/**
 * @brief Algoritmo A* Determinista optimizado para Hardware de Consola.
 * 
 * @author Wii U Game Engine Architecture Team
 * @date Noviembre 2026 (Hito 120 - Capítulo 19 Jason Gregory)
 * 
 * @par Fundamento Matemático y Literatura:
 * - **Jason Gregory** (*Game Engine Architecture*, 3ª Edición, Cap. 19.1.2 "Finding Paths", pp. 1012-1025).
 * - **Peter E. Hart, Nils J. Nilsson, Bertram Raphael** (*"A Formal Basis for the Heuristic Determination of Minimum Cost Paths"*, IEEE SSC 1968).
 * 
 * @par Cero Asignaciones Dinámicas (0 Heap Allocs a 60 FPS):
 * Las estructuras de control (Open Set, Closed Set, gScore, fScore, cameFrom) residen en la pila
 * o en miembros de memoria estática con límite acotado en $O(V)$, garantizando determinismo
 * absoluto de caché y cero degradación por recolección o fragmentación.
 */
class AStarPathfinder {
public:
    AStarPathfinder() noexcept = default;
    ~AStarPathfinder() noexcept = default;

    /**
     * @brief Encuentra el camino de costo mínimo entre dos identificadores de nodo en el NavGraph.
     * @param graph Grafo espacial de navegación.
     * @param startNodeId Nodo de origen.
     * @param goalNodeId Nodo objetivo.
     * @param[out] outPath Estructura donde se almacena la secuencia de waypoints calculada.
     * @return true si se halló una ruta continua; false si el objetivo es inalcanzable.
     */
    static bool FindPath(const NavGraph& graph, 
                         uint32_t startNodeId, 
                         uint32_t goalNodeId, 
                         NavPath& outPath) noexcept;

    /**
     * @brief Encuentra el camino proyectando primero las posiciones espaciales a los nodos más cercanos.
     * @param graph Grafo espacial de navegación.
     * @param startPos Posición tridimensional actual del agente.
     * @param targetPos Posición tridimensional del objetivo.
     * @param[out] outPath Secuencia resultante de waypoints.
     * @return true si se halló una ruta transitable.
     */
    static bool FindPath(const NavGraph& graph, 
                         const Point3D& startPos, 
                         const Point3D& targetPos, 
                         NavPath& outPath) noexcept;

private:
    /**
     * @brief Heurística euclidiana admisible 3D: $h(n) = \|\mathbf{p}_n - \mathbf{p}_{\text{goal}}\|$.
     */
    [[nodiscard]] static float HeuristicEuclidean(const Point3D& a, const Point3D& b) noexcept;
};

} // namespace Neleo
