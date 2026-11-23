#include "Core/AI/AISelfTest.hpp"
#include "Core/AI/NavGraph.hpp"
#include "Core/AI/AStarPathfinder.hpp"
#include "Core/AI/SteeringBehaviors.hpp"
#include <cmath>

namespace WiiU::AI {

void AISelfTest::RunAll(Diagnostics::EngineSelfTestReport& outReport) {
    // 1. NavGraph + AStarPathfinder: debe elegir la ruta óptima (0 -> 1 -> 3 costo 4) vs larga (0 -> 2 -> 3 costo 20)
    {
        NavGraph graph;
        const uint32_t n0 = graph.AddNode(Point3D{0.0f, 0.0f, 0.0f});
        const uint32_t n1 = graph.AddNode(Point3D{2.0f, 0.0f, 0.0f});
        const uint32_t n2 = graph.AddNode(Point3D{0.0f, 0.0f, 10.0f});
        const uint32_t n3 = graph.AddNode(Point3D{4.0f, 0.0f, 0.0f});

        graph.AddBiDirectionalEdge(n0, n1, 2.0f);
        graph.AddBiDirectionalEdge(n1, n3, 2.0f);
        graph.AddBiDirectionalEdge(n0, n2, 10.0f);
        graph.AddBiDirectionalEdge(n2, n3, 10.0f);

        NavPath path;
        const bool found = AStarPathfinder::FindPath(graph, n0, n3, path);
        const bool ok = found && path.isValid && (path.waypointCount == 3u) &&
                        (path.nodeIds[0] == n0) && (path.nodeIds[1] == n1) && (path.nodeIds[2] == n3);
        outReport.Record("AI", "NAVGRAPH_ASTAR", ok,
                         ok ? "ASTAR RUTA OPTIMA OK" : "FALLO ASTAR PATHFINDING");
    }

    // 2. SteeringBehaviors: Seek, Flee y Arrive finitos con dirección correcta y desaceleración
    {
        const Point3D pos{0.0f, 0.0f, 0.0f};
        const Point3D vel{0.0f, 0.0f, 0.0f};
        const Point3D target{10.0f, 0.0f, 0.0f};
        const Point3D closeTarget{1.0f, 0.0f, 0.0f};

        const Point3D seekF = SteeringBehaviors::Seek(pos, target, vel, 8.0f);
        const Point3D fleeF = SteeringBehaviors::Flee(pos, target, vel, 8.0f);
        const Point3D arriveNear = SteeringBehaviors::Arrive(pos, closeTarget, vel, 8.0f, 4.0f);

        const bool ok = std::isfinite(seekF.x) && (seekF.x > 7.5f) &&
                        std::isfinite(fleeF.x) && (fleeF.x < -7.5f) &&
                        std::isfinite(arriveNear.x) && (arriveNear.x > 0.5f) && (arriveNear.x < 4.0f);
        outReport.Record("AI", "STEERING_BEHAVIORS", ok,
                         ok ? "SEEK/FLEE/ARRIVE OK" : "FALLO STEERING BEHAVIORS");
    }
}

} // namespace WiiU::AI
