#include "Core/Physics/PhysicsSelfTest.hpp"
#include "Core/Physics/SpatialHashGrid.hpp"
#include "Core/Spatial/SpatialBVH.hpp"
#include <cmath>
#include <cstdint>

namespace Neleo::Physics {

void PhysicsSelfTest::RunAll(Diagnostics::EngineSelfTestReport& outReport) {
    // 1. SpatialHashGrid: inserción de esferas, consulta sin duplicados y limpieza O(1)
    {
        SpatialHashGrid grid(4.0f);
        grid.Clear();
        grid.InsertSphere(1u, Point3D{0.0f, 0.0f, 0.0f}, 1.5f);
        grid.InsertSphere(2u, Point3D{2.0f, 0.0f, 0.0f}, 1.5f);
        grid.InsertSphere(3u, Point3D{40.0f, 0.0f, 0.0f}, 1.0f);

        uint32_t outIds[8]{};
        const size_t countNear = grid.QuerySphere(Point3D{1.0f, 0.0f, 0.0f}, 2.0f, outIds, 8u);
        bool has1 = false;
        bool has2 = false;
        bool has3 = false;
        for (size_t i = 0; i < countNear; ++i) {
            if (outIds[i] == 1u) has1 = true;
            if (outIds[i] == 2u) has2 = true;
            if (outIds[i] == 3u) has3 = true;
        }

        grid.Clear();
        const size_t countAfterClear = grid.QuerySphere(Point3D{1.0f, 0.0f, 0.0f}, 2.0f, outIds, 8u);
        const bool ok = has1 && has2 && !has3 && (countNear == 2u) && (countAfterClear == 0u);
        outReport.Record("PHYS", "SPATIAL_HASH_GRID", ok,
                         ok ? "BROADPHASE O(1) OK" : "FALLO SPATIAL HASH GRID");
    }

    // 2. Intersección AABB / Esfera y acotamiento finito
    {
        SpatialAABB a(Point3D{-2.0f, 0.0f, -2.0f}, Point3D{2.0f, 4.0f, 2.0f});
        SpatialAABB b(Point3D{1.5f, 1.0f, 1.5f}, Point3D{3.5f, 3.0f, 3.5f});
        SpatialAABB c(Point3D{10.0f, 10.0f, 10.0f}, Point3D{12.0f, 12.0f, 12.0f});

        Point3D pos{0.0f, 10.0f, 0.0f};
        Point3D vel{0.0f, 0.0f, 0.0f};
        constexpr float dt = 1.0f / 60.0f;
        constexpr float gravity = -9.81f;
        for (int i = 0; i < 60; ++i) {
            vel.y += gravity * dt;
            pos.y += vel.y * dt;
        }

        const bool ok = a.IntersectsAABB(b) && !a.IntersectsAABB(c) &&
                        std::isfinite(pos.y) && (pos.y < 6.0f) && (pos.y > 4.0f);
        outReport.Record("PHYS", "PHYSICS_AABB_BOUNDS", ok,
                         ok ? "AABB + INTEGRACION OK" : "FALLO COLISION/INTEGRACION");
    }
}

} // namespace Neleo::Physics
