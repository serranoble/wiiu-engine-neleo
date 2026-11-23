#pragma once

#include "Core/Diagnostics/SelfTestTypes.hpp"

namespace WiiU::Physics {

/**
 * @brief Batería de pruebas deterministas de engine_physics (SpatialHashGrid y volúmenes AABB).
 */
class PhysicsSelfTest {
public:
    static void RunAll(Diagnostics::EngineSelfTestReport& outReport);
};

} // namespace WiiU::Physics
