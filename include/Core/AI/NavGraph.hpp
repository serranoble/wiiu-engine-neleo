#pragma once

#include "Graphics/Math3D.hpp"
#include <array>
#include <cstdint>
#include <cstddef>
#include <limits>

namespace WiiU {

/**
 * @brief Sub-sistema de Grafos de Navegación Espacial (NavGraph) para Consolas.
 * 
 * @author Wii U Game Engine Architecture Team
 * @date Noviembre 2026 (Hito 120 - Capítulo 19 Jason Gregory)
 * 
 * @par Fundamento Teórico y Literatura:
 * - **Jason Gregory** (*Game Engine Architecture*, 3ª Edición, Cap. 19.1.1 "Discretizing the World").
 * - **Amit Patel** (*Red Blob Games: Introduction to A* and Graph Search*).
 * 
 * @par Principios de Consola y Multiplataforma:
 * - **Cero Asignaciones en el Heap (0 Heap Allocs a 60 FPS):** Capacidad fija estática de nodos y aristas.
 * - **Desacoplamiento Absoluto:** Código C++20 puro, 100% portable a Nintendo Switch, 3DS o PC.
 */

struct NavEdge {
    uint32_t targetNodeId{0};
    float cost{0.0f};
};

struct NavNode {
    static constexpr size_t MAX_EDGES_PER_NODE = 8;

    uint32_t id{0};
    Point3D position{0.0f, 0.0f, 0.0f};
    float radius{1.2f};
    uint32_t edgeCount{0};
    std::array<NavEdge, MAX_EDGES_PER_NODE> edges{};
    bool occupied{false};

    [[nodiscard]] bool HasEdgeTo(uint32_t targetId) const noexcept {
        for (uint32_t i = 0; i < edgeCount; ++i) {
            if (edges[i].targetNodeId == targetId) return true;
        }
        return false;
    }
};

class NavGraph {
public:
    static constexpr size_t MAX_NODES = 64;
    static constexpr uint32_t INVALID_NODE_ID = 0xFFFFFFFFU;

    NavGraph() noexcept;
    ~NavGraph() noexcept = default;

    NavGraph(const NavGraph&) = delete;
    NavGraph& operator=(const NavGraph&) = delete;
    NavGraph(NavGraph&&) noexcept = default;
    NavGraph& operator=(NavGraph&&) noexcept = default;

    /**
     * @brief Limpia todos los nodos y aristas del grafo.
     */
    void Clear() noexcept;

    /**
     * @brief Añade un nuevo nodo de navegación en la posición tridimensional especificada.
     * @param position Coordenadas mundiales del nodo (X, Y, Z).
     * @param radius Radio métrico de llegada/cobertura del nodo.
     * @return Identificador entero del nodo o INVALID_NODE_ID si se superó la capacidad máxima.
     */
    uint32_t AddNode(const Point3D& position, float radius = 1.2f) noexcept;

    /**
     * @brief Añade una arista dirigida entre dos nodos existentes.
     * @param fromId Nodo de origen.
     * @param toId Nodo de destino.
     * @param customCost Costo métrico (si es < 0, se calcula la distancia euclidiana 3D).
     * @return true si la arista fue insertada con éxito.
     */
    bool AddEdge(uint32_t fromId, uint32_t toId, float customCost = -1.0f) noexcept;

    /**
     * @brief Añade una arista bidireccional transitable en ambos sentidos.
     */
    bool AddBiDirectionalEdge(uint32_t aId, uint32_t bId, float customCost = -1.0f) noexcept;

    /**
     * @brief Obtiene el puntero de solo lectura a un nodo por su ID.
     */
    [[nodiscard]] const NavNode* GetNode(uint32_t id) const noexcept;

    /**
     * @brief Encuentra el nodo más cercano a una posición espacial arbitraria.
     * @param pos Coordenada de consulta.
     * @return ID del nodo más cercano o INVALID_NODE_ID si el grafo está vacío.
     */
    [[nodiscard]] uint32_t FindNearestNode(const Point3D& pos) const noexcept;

    [[nodiscard]] size_t GetNodeCount() const noexcept { return m_nodeCount; }
    [[nodiscard]] const std::array<NavNode, MAX_NODES>& GetNodes() const noexcept { return m_nodes; }

private:
    std::array<NavNode, MAX_NODES> m_nodes{};
    uint32_t m_nodeCount{0};
};

} // namespace WiiU
