#pragma once

#include "Graphics/Math3D.hpp"
#include <cstdint>

namespace WiiU {

/**
 * @brief [OPT-256] Componente de Volumen Delimitador para Frustum & Small-Primitive Culling.
 * 
 * Almacena una esfera delimitadora en espacio local y banderas de visibilidad.
 * Optimizado para vectorización Fast Math en la CPU IBM Espresso PPC 750CL.
 */
struct BoundingVolumeComponent {
    Point3D center{0.0f, 0.0f, 0.0f};      // Centro en espacio local de la malla
    float radius{1.0f};                     // Radio en espacio local
    bool isCulled{false};                   // True si está fuera del frustum o sub-píxel
    bool enableFrustumCulling{true};        // Culling contra los 6 planos del frustum
    bool enableSmallPrimitiveCulling{true}; // Culling de submallas sub-píxel
    float minScreenRadius{1.5f};            // Umbral de píxeles en pantalla

    constexpr BoundingVolumeComponent() noexcept = default;

    constexpr explicit BoundingVolumeComponent(const Point3D& c, float r,
                                              bool frustumCull = true,
                                              bool smallCull = true,
                                              float minPixels = 1.5f) noexcept
        : center(c), radius(r), isCulled(false),
          enableFrustumCulling(frustumCull),
          enableSmallPrimitiveCulling(smallCull),
          minScreenRadius(minPixels) {}
};

} // namespace WiiU
