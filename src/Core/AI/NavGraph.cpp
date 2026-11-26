#include "Core/AI/NavGraph.hpp"
#include <cmath>

namespace Neleo {

NavGraph::NavGraph() noexcept {
    Clear();
}

void NavGraph::Clear() noexcept {
    m_nodeCount = 0;
    for (auto& node : m_nodes) {
        node.id = INVALID_NODE_ID;
        node.edgeCount = 0;
        node.occupied = false;
    }
}

uint32_t NavGraph::AddNode(const Point3D& position, float radius) noexcept {
    if (m_nodeCount >= MAX_NODES) {
        return INVALID_NODE_ID;
    }

    const uint32_t newId = m_nodeCount++;
    NavNode& node = m_nodes[newId];
    node.id = newId;
    node.position = position;
    node.radius = radius;
    node.edgeCount = 0;
    node.occupied = false;
    return newId;
}

bool NavGraph::AddEdge(uint32_t fromId, uint32_t toId, float customCost) noexcept {
    if (fromId >= m_nodeCount || toId >= m_nodeCount || fromId == toId) {
        return false;
    }

    NavNode& node = m_nodes[fromId];
    if (node.edgeCount >= NavNode::MAX_EDGES_PER_NODE) {
        return false; // Capacidad máxima alcanzada en este nodo
    }

    // Evitar aristas duplicadas
    if (node.HasEdgeTo(toId)) {
        return true;
    }

    float cost = customCost;
    if (cost < 0.0f) {
        const float dx = node.position.x - m_nodes[toId].position.x;
        const float dy = node.position.y - m_nodes[toId].position.y;
        const float dz = node.position.z - m_nodes[toId].position.z;
        cost = std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    node.edges[node.edgeCount++] = NavEdge{ toId, cost };
    return true;
}

bool NavGraph::AddBiDirectionalEdge(uint32_t aId, uint32_t bId, float customCost) noexcept {
    const bool e1 = AddEdge(aId, bId, customCost);
    const bool e2 = AddEdge(bId, aId, customCost);
    return e1 && e2;
}

const NavNode* NavGraph::GetNode(uint32_t id) const noexcept {
    if (id >= m_nodeCount) {
        return nullptr;
    }
    return &m_nodes[id];
}

uint32_t NavGraph::FindNearestNode(const Point3D& pos) const noexcept {
    if (m_nodeCount == 0) {
        return INVALID_NODE_ID;
    }

    uint32_t bestId = INVALID_NODE_ID;
    float bestDistSq = std::numeric_limits<float>::max();

    for (uint32_t i = 0; i < m_nodeCount; ++i) {
        const float dx = m_nodes[i].position.x - pos.x;
        const float dy = m_nodes[i].position.y - pos.y;
        const float dz = m_nodes[i].position.z - pos.z;
        const float distSq = dx * dx + dy * dy + dz * dz;

        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestId = i;
        }
    }

    return bestId;
}

} // namespace Neleo
