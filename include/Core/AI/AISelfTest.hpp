#pragma once

#include "Core/Diagnostics/SelfTestTypes.hpp"

namespace WiiU::AI {

/**
 * @brief Batería de pruebas deterministas de engine_ai (NavGraph + AStarPathfinder y SteeringBehaviors).
 */
class AISelfTest {
public:
    static void RunAll(Diagnostics::EngineSelfTestReport& outReport);
};

} // namespace WiiU::AI
