#pragma once

#include "Core/Diagnostics/SelfTestTypes.hpp"

namespace Neleo::Input {

/**
 * @brief Batería de pruebas deterministas de engine_input (Zona Muerta Radial e InputContextStack).
 */
class InputSelfTest {
public:
    static void RunAll(Diagnostics::EngineSelfTestReport& outReport);
};

} // namespace Neleo::Input
