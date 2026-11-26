#pragma once

#include "Core/Diagnostics/SelfTestTypes.hpp"

namespace Neleo::Audio {

/**
 * @brief Batería de pruebas deterministas de engine_audio (AudioEngine3D telemetría, listener y mezcla segura).
 */
class AudioSelfTest {
public:
    static void RunAll(Diagnostics::EngineSelfTestReport& outReport);
};

} // namespace Neleo::Audio
