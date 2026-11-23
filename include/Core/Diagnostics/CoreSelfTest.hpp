#pragma once

#include "Core/Diagnostics/SelfTestTypes.hpp"

namespace WiiU::Diagnostics {

/**
 * @brief Batería de pruebas deterministas de engine_core (PRNG, CryptoHMAC, SpatialBVH, CVar/EngineConfig).
 */
class CoreSelfTest {
public:
    static void RunAll(EngineSelfTestReport& outReport);
};

} // namespace WiiU::Diagnostics
