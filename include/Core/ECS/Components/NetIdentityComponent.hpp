#pragma once

#include <cstdint>

namespace WiiU {

/**
 * @brief [Jason Gregory Cap. 20.4] Identidad de red ECS (POD puro en engine_core, cero includes de sockets).
 */
struct NetIdentityComponent {
    uint32_t netId{0};
    uint16_t lastAckedTick{0};
    bool isLocalAuthority{true};
    bool replicateTransform{true};

    constexpr NetIdentityComponent() noexcept = default;
    constexpr NetIdentityComponent(uint32_t id, bool localAuthority = true, bool replicate = true) noexcept
        : netId(id), isLocalAuthority(localAuthority), replicateTransform(replicate) {}
};

} // namespace WiiU
