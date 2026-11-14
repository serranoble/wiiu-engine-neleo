#pragma once

#include "Graphics/Color.hpp"
#include <cstdint>

namespace WiiU {

enum class LightType : uint8_t {
    Point = 0,
    Directional,
    Spot
};

/**
 * @brief [OPT-244] Componente de Fuente de Iluminación 3D orientado a datos.
 * 
 * Almacena parámetros fotométricos para subida a registros de uniformes CafeGLSL (KC2[0..4]).
 */
struct LightComponent {
    ColorRGBA color{ColorRGBA::White()};
    float intensity{1.0f};
    float range{20.0f};
    float spotAngle{0.0f};
    LightType type{LightType::Point};
    bool castShadows{false};
    bool isEnabled{true};

    constexpr LightComponent() noexcept = default;

    constexpr LightComponent(const ColorRGBA& col, float inten = 1.0f, float rng = 20.0f,
                             LightType lType = LightType::Point, bool shadows = false) noexcept
        : color(col), intensity(inten), range(rng), type(lType), castShadows(shadows), isEnabled(true) {}
};

} // namespace WiiU
