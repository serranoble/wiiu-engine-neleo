#pragma once

#include "Graphics/Color.hpp"
#include <cstdint>

namespace WiiU {

class StaticMesh;

/**
 * @brief [OPT-351 / OPT-355 / OPT-367] Componente ECS agnostico a hardware para Skybox / Domo Celeste.
 *
 * Usa puntero RHI opaco (void*) para mantener 0 acoplamiento de GPU especifica en Core/ECS
 * y portabilidad completa multi-consola (Wii U, Switch, PC).
 */
struct SkyboxComponent {
    StaticMesh* mesh{nullptr};
    void* skyTexture{nullptr}; // Puntero RHI opaco (GX2Texture* en Wii U)

    ColorRGBA zenithColor{35, 95, 185, 255};
    ColorRGBA horizonColor{165, 205, 235, 255};
    float exposure{1.0f};
    float rotationYawRad{0.0f};
    bool isCubeMap{false};
    bool enabled{true};

    constexpr SkyboxComponent() noexcept = default;
};

} // namespace WiiU
