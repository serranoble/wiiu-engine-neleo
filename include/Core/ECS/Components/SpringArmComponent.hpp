#pragma once

#include "Graphics/Math3D.hpp"
#include <algorithm>
#include <cmath>

namespace WiiU {

/**
 * @brief [OPT-321] Componente ECS de Brazo Elástico y Barrido de Colisión de Cámara (Spring-Arm).
 * 
 * Basado en Mark Haigh-Hutchinson (Real-Time Cameras, Morgan Kaufmann, Cap. 7)
 * y Jason Gregory (Game Engine Architecture, 3rd Ed., Cap. 16.4).
 * Proporciona:
 * 1. Longitud objetivo deseada vs longitud real colisionada.
 * 2. Radio de la sonda esférica de la cámara (Probe Radius).
 * 3. Amortiguamiento elástico asimétrico: retracción rápida para prevenir clipping,
 *    extensión suave de retorno cuando el obstáculo desaparece.
 * 4. Cota mínima de suelo (Ground Plane Prevention) para evitar que la cámara perfore el suelo o pedestal.
 */
struct SpringArmComponent {
    float targetArmLength{10.5f};
    float currentArmLength{10.5f};
    float probeRadius{0.35f};
    float minArmLength{1.8f};
    float maxArmLength{40.0f};
    float minGroundY{0.35f};         // Cota mínima absoluta sobre el suelo
    float collapseDamping{24.0f};    // Amortiguamiento rápido al retractarse por colisión
    float extendDamping{6.0f};       // Amortiguamiento suave al extenderse de retorno
    bool collisionEnabled{true};
    bool isObstructed{false};

    constexpr SpringArmComponent() noexcept = default;

    constexpr explicit SpringArmComponent(float targetLen, float probeRad = 0.35f,
                                          float minLen = 1.8f, float maxLen = 40.0f,
                                          float groundY = 0.35f) noexcept
        : targetArmLength(targetLen), currentArmLength(targetLen), probeRadius(probeRad),
          minArmLength(minLen), maxArmLength(maxLen), minGroundY(groundY),
          collapseDamping(24.0f), extendDamping(6.0f),
          collisionEnabled(true), isObstructed(false) {}
};

} // namespace WiiU
