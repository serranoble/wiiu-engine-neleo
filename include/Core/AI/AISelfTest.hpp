#pragma once

#include "Core/Diagnostics/SelfTestTypes.hpp"

namespace Neleo::AI {

/**
 * @brief Batería de pruebas deterministas de engine_ai (NavGraph + AStarPathfinder y SteeringBehaviors).
 */
class AISelfTest {
public:
    static void RunAll(Diagnostics::EngineSelfTestReport& outReport);
};

} // namespace Neleo::AI
