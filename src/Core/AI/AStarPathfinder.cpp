#include "Core/AI/AStarPathfinder.hpp"
#include <cmath>

namespace WiiU {

float AStarPathfinder::HeuristicEuclidean(const Point3D& a, const Point3D& b) noexcept {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool AStarPathfinder::FindPath(const NavGraph& graph, 
                               uint32_t startNodeId, 
                               uint32_t goalNodeId, 
                               NavPath& outPath) noexcept {
    outPath.Clear();

    const size_t totalNodes = graph.GetNodeCount();
    if (startNodeId >= totalNodes || goalNodeId >= totalNodes || totalNodes == 0) {
        return false;
    }

    const NavNode* startNode = graph.GetNode(startNodeId);
    const NavNode* goalNode = graph.GetNode(goalNodeId);
    if (!startNode || !goalNode) {
        return false;
    }

    // Caso trivial: Origen coincide con Destino
    if (startNodeId == goalNodeId) {
        outPath.waypointCount = 1;
        outPath.waypoints[0] = goalNode->position;
        outPath.nodeIds[0] = goalNodeId;
        outPath.currentIndex = 0;
        outPath.isValid = true;
        return true;
    }

    // Estructuras de búsqueda de A* con 0 asignaciones dinámicas en el Heap
    std::array<bool, NavGraph::MAX_NODES> inOpenSet{};
    std::array<bool, NavGraph::MAX_NODES> inClosedSet{};
    std::array<float, NavGraph::MAX_NODES> gScore{};
    std::array<float, NavGraph::MAX_NODES> fScore{};
    std::array<uint32_t, NavGraph::MAX_NODES> cameFrom{};

    constexpr float INF = 1e30f;
    gScore.fill(INF);
    fScore.fill(INF);
    cameFrom.fill(NavGraph::INVALID_NODE_ID);

    // Inicializar nodo raíz
    gScore[startNodeId] = 0.0f;
    fScore[startNodeId] = HeuristicEuclidean(startNode->position, goalNode->position);
    inOpenSet[startNodeId] = true;
    uint32_t openCount = 1;

    bool pathFound = false;

    while (openCount > 0) {
        // Encontrar el nodo en Open Set con menor fScore (f(n) = g(n) + h(n))
        uint32_t current = NavGraph::INVALID_NODE_ID;
        float lowestF = INF;

        for (uint32_t i = 0; i < totalNodes; ++i) {
            if (inOpenSet[i] && fScore[i] < lowestF) {
                lowestF = fScore[i];
                current = i;
            }
        }

        if (current == NavGraph::INVALID_NODE_ID) {
            break; // No hay ruta transitable
        }

        // Meta alcanzada
        if (current == goalNodeId) {
            pathFound = true;
            break;
        }

        inOpenSet[current] = false;
        --openCount;
        inClosedSet[current] = true;

        const NavNode* currentNode = graph.GetNode(current);
        if (!currentNode) continue;

        // Evaluar todas las aristas adyacentes del nodo actual
        for (uint32_t e = 0; e < currentNode->edgeCount; ++e) {
            const auto& edge = currentNode->edges[e];
            const uint32_t neighbor = edge.targetNodeId;

            if (neighbor >= totalNodes || inClosedSet[neighbor]) {
                continue;
            }

            const float tentativeG = gScore[current] + edge.cost;

            if (!inOpenSet[neighbor]) {
                inOpenSet[neighbor] = true;
                ++openCount;
            } else if (tentativeG >= gScore[neighbor]) {
                continue; // No es una mejor ruta
            }

            const NavNode* neighborNode = graph.GetNode(neighbor);
            cameFrom[neighbor] = current;
            gScore[neighbor] = tentativeG;
            fScore[neighbor] = tentativeG + (neighborNode ? HeuristicEuclidean(neighborNode->position, goalNode->position) : 0.0f);
        }
    }

    if (!pathFound) {
        return false;
    }

    // Reconstrucción del camino en sentido inverso (Backtracking desde meta)
    std::array<uint32_t, NavPath::MAX_WAYPOINTS> reversePath{};
    uint32_t stepCount = 0;
    uint32_t curr = goalNodeId;

    while (curr != NavGraph::INVALID_NODE_ID && stepCount < NavPath::MAX_WAYPOINTS) {
        reversePath[stepCount++] = curr;
        if (curr == startNodeId) {
            break;
        }
        curr = cameFrom[curr];
    }

    if (stepCount == 0 || reversePath[stepCount - 1] != startNodeId) {
        return false;
    }

    // Invertir la ruta para que comience en el primer paso hacia el objetivo
    outPath.waypointCount = stepCount;
    for (uint32_t i = 0; i < stepCount; ++i) {
        const uint32_t nid = reversePath[stepCount - 1 - i];
        outPath.nodeIds[i] = nid;
        const NavNode* nd = graph.GetNode(nid);
        outPath.waypoints[i] = nd ? nd->position : Point3D{0, 0, 0};
    }

    outPath.currentIndex = (stepCount > 1) ? 1 : 0; // Comenzar hacia el primer waypoint siguiente
    outPath.isValid = true;
    return true;
}

bool AStarPathfinder::FindPath(const NavGraph& graph, 
                               const Point3D& startPos, 
                               const Point3D& targetPos, 
                               NavPath& outPath) noexcept {
    const uint32_t startId = graph.FindNearestNode(startPos);
    const uint32_t goalId = graph.FindNearestNode(targetPos);

    if (startId == NavGraph::INVALID_NODE_ID || goalId == NavGraph::INVALID_NODE_ID) {
        outPath.Clear();
        return false;
    }

    return FindPath(graph, startId, goalId, outPath);
}

} // namespace WiiU
