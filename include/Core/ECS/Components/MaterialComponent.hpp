#pragma once

#include "Graphics/Color.hpp"
#include "Core/ResourceHandle.hpp"
#include <cstdint>

struct GX2Texture;

namespace WiiU {

enum class BlendMode : uint8_t {
    Opaque = 0,
    AlphaBlend,
    Additive,
    AlphaCutout
};

/**
 * @brief [OPT-252] Componente Desacoplado de Material y Pipeline State orientado a datos.
 * 
 * Almacena parámetros de superficie y sombreado físico (PBR/Blinn-Phong) para shaders CafeGLSL
 * y RHI multiplataforma. Empaquetado POD contiguo.
 */
struct MaterialComponent {
    ColorRGBA baseColor{ColorRGBA::White()};
    float roughness{0.5f};
    float metallic{0.0f};
    float uvTiling{1.0f};
    float materialMode{1.0f}; // 0 = SkyDome, 1 = Textured, 2 = Water, 3 = Emissive/Sun
    float customParam{0.0f};  // Parámetro auxiliar de silicio (ej. waterLevel = -0.35f)
    uint32_t textureId{0};
    void* diffuseTexture{nullptr}; // Puntero RHI opaco opcional (GX2Texture* en Wii U)
    void* envCubeMap{nullptr};     // CubeMap ambiental RHI opaco opcional
    ResourceHandle<GX2Texture> textureHandle{};
    BlendMode blendMode{BlendMode::Opaque};
    bool depthTest{true};
    bool depthWrite{true};
    bool isVisible{true};

    constexpr MaterialComponent() noexcept = default;

    constexpr explicit MaterialComponent(float mode, float tiling = 1.0f, float rough = 0.5f,
                                        float custom = 0.0f, BlendMode blend = BlendMode::Opaque,
                                        bool dTest = true, bool dWrite = true) noexcept
        : roughness(rough), metallic(0.0f), uvTiling(tiling), materialMode(mode),
          customParam(custom), textureId(0), blendMode(blend),
          depthTest(dTest), depthWrite(dWrite), isVisible(true) {}
};

} // namespace WiiU
