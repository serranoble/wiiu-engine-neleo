#pragma once

#include "Core/Diagnostics/SelfTestTypes.hpp"

namespace Neleo::Diagnostics {

/**
 * @brief Batería de pruebas deterministas de engine_core (PRNG, CryptoHMAC, SpatialBVH, CVar/EngineConfig).
 */
class CoreSelfTest {
public:
    static void RunAll(EngineSelfTestReport& outReport);
};

} // namespace Neleo::Diagnostics
