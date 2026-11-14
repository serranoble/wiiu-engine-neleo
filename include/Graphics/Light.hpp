#pragma once

#include "Graphics/Math3D.hpp"
#include "Graphics/Color.hpp"
#include <algorithm>

namespace WiiU {

struct DirectionalLight {
    Point3D direction{-0.5f, -1.0f, -0.7f}; // Dirección de la luz espacial
    ColorRGBA color{255, 255, 240, 255};      // Color de la luz (Blanco/Cálido)
    float intensity{1.2f};                    // Intensidad
    float ambientIntensity{0.25f};            // Luz ambiental global
};

struct Material {
    ColorRGBA baseColor{255, 255, 255, 255};
    float specular{0.6f};
    float shininess{16.0f};
};

/**
 * @brief Computes Lambertian diffuse + ambient directional lighting for a surface normal.
 */
inline ColorRGBA CalculateLighting(const ColorRGBA& baseColor, const Point3D& normal, 
                                   const DirectionalLight& light) {
    const Point3D n = normal.Normalized();
    const Point3D l = (light.direction * -1.0f).Normalized();

    // Componente Difuso (Lambertian)
    const float diff = std::max(0.0f, Point3D::Dot(n, l));

    // Componente Ambiental
    const float ambient = light.ambientIntensity;
    const float totalLight = std::min(1.0f, ambient + diff * light.intensity);

    const Uint8 r = static_cast<Uint8>(std::min(255.0f, baseColor.r * totalLight));
    const Uint8 g = static_cast<Uint8>(std::min(255.0f, baseColor.g * totalLight));
    const Uint8 b = static_cast<Uint8>(std::min(255.0f, baseColor.b * totalLight));

    return ColorRGBA{r, g, b, baseColor.a};
}

inline ColorRGBA calculateLighting(const ColorRGBA& baseColor, const Point3D& normal, 
                                   const DirectionalLight& light) {
    return CalculateLighting(baseColor, normal, light);
}

} // namespace WiiU
