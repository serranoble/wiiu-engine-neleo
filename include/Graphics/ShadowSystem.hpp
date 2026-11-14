#pragma once

#include "Graphics/Math3D.hpp"
#include <cstdint>

namespace WiiU {

/**
 * @brief Descriptor de cámara agnóstico a ECS para el cálculo de cascadas CSM.
 */
struct ShadowCameraDesc {
    Point3D position{0.0f, 0.0f, 0.0f};
    Point3D target{0.0f, 0.0f, -1.0f};
    Point3D up{0.0f, 1.0f, 0.0f};
    float fovDeg{60.0f};
    float aspectRatio{16.0f / 9.0f};
    float nearClip{0.20f};
    float farClip{120.0f};
};

/**
 * @brief [Jason Gregory Cap. 11.3: "Real-Time Shadows - Cascaded Shadow Maps (CSM)"]
 * Datos por cascada de sombra direccional con estabilización de texels (anti-shimmering).
 */
struct ShadowCascade {
    float splitDepthNear{0.1f};
    float splitDepthFar{50.0f};
    Matrix4x4 lightViewMatrix;
    Matrix4x4 lightProjMatrix;
    Matrix4x4 lightViewProjMatrix;
    float orthoRadius{10.0f};
    float texelWorldSize{0.02f};
};

/**
 * @brief Configuración global reutilizable del sistema de sombras en tiempo real.
 */
struct ShadowConfig {
    uint32_t cascadeCount{3};        // 1 a 4 cascadas (típico 3 en consola)
    uint32_t shadowMapResolution{1024};
    float splitLambda{0.65f};        // Mezcla logarítmica/uniforme (Practical Split Scheme)
    float maxShadowDistance{120.0f};
    float depthBias{0.0025f};
    float pcfFilterRadius{1.5f};     // Radio de suavizado Percentage Closer Filtering
    float lightSourceRadius{0.45f};  // Radio de fuente de área para penumbra analítica / RT
};

/**
 * @brief [Jason Gregory Cap. 11.3] Subsistema de Sombras en Tiempo Real y Cascaded Shadow Maps (CSM).
 * Calcula particiones prácticas de frustum y matrices ortográficas estabilizadas al tamaño de texel
 * para erradicar el parpadeo en bordes durante el movimiento de cámara.
 */
class ShadowSystem {
public:
    static constexpr uint32_t MAX_CASCADES = 4;

    /**
     * @brief Calcula las profundidades de corte mediante el esquema práctico logarítmico-lineal.
     */
    static void ComputeCascadeSplits(float nearPlane, float farPlane, uint32_t count,
                                     float lambda, float* outSplits) noexcept;

    /**
     * @brief Calcula las matrices View-Projection ortográficas estabilizadas para cada cascada CSM.
     */
    static void ComputeCascades(const ShadowCameraDesc& camera,
                                const Point3D& lightDir, const ShadowConfig& config,
                                ShadowCascade outCascades[MAX_CASCADES]) noexcept;

    /**
     * @brief Calcula el factor de penumbra analítico (0.0 = sombra total, 1.0 = iluminación total)
     * para sombras suaves por distancia al oclusor (usado en PCSS / Ray Tracing).
     */
    [[nodiscard]] static float ComputeSoftShadowPenumbra(float occluderDist, float lightRadius,
                                                         float minFactor = 0.15f) noexcept;
};

} // namespace WiiU
