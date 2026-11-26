#pragma once

#include "Core/ResourceHandle.hpp"
#include "Core/ECS/Components/MaterialComponent.hpp"
#include "Graphics/Math3D.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Neleo {

using RHITexturePtr = void*;

/**
 * @brief [OPT-13 / OPT-216 / OPT-365] Bloque Uniforme GPU puro de Material alineado a 256 bytes (Latte GX2).
 */
struct alignas(256) MaterialUBOData {
    float albedoTint[4]{1.0f, 1.0f, 1.0f, 1.0f};
    // x = reflectionIntensity, y = normalMapEnabled (0/1), z = bumpScale, w = ambientIntensity
    float materialParams[4]{0.0f, 1.0f, 2.0f, 0.38f};
    // x = roughness, y = metallic, z = uvTiling, w = customParam
    float pbrParams[4]{0.5f, 0.0f, 1.0f, 0.0f};
    float emissive[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float reservedPad[48]{}; // Padding explicito a 256 bytes exactos
};
static_assert(alignof(MaterialUBOData) == 256, "MaterialUBOData debe alinearse a 256B para Latte GX2");
static_assert(sizeof(MaterialUBOData) == 256, "MaterialUBOData debe medir exactamente 256B");

/**
 * @brief [OPT-13 / OPT-216 / OPT-365] Bloque de Propiedades de Material agnostico a hardware (UBO + bindings RHI).
 */
struct MaterialPropertyBlock {
    MaterialUBOData ubo{};

    RHITexturePtr diffuseTexture{nullptr};
    RHITexturePtr envCubeMap{nullptr};
    BlendMode blendMode{BlendMode::Opaque};
    uint16_t shaderId{0};
    bool depthTest{true};
    bool depthWrite{true};
    bool backfaceCull{true};

    void ApplyFromComponent(const MaterialComponent& mat) noexcept {
        ubo.albedoTint[0] = static_cast<float>(mat.baseColor.r) / 255.0f;
        ubo.albedoTint[1] = static_cast<float>(mat.baseColor.g) / 255.0f;
        ubo.albedoTint[2] = static_cast<float>(mat.baseColor.b) / 255.0f;
        ubo.albedoTint[3] = static_cast<float>(mat.baseColor.a) / 255.0f;
        ubo.materialParams[0] = mat.metallic;
        ubo.materialParams[2] = (mat.customParam != 0.0f) ? mat.customParam : 2.0f;
        ubo.pbrParams[0] = mat.roughness;
        ubo.pbrParams[1] = mat.metallic;
        ubo.pbrParams[2] = mat.uvTiling;
        ubo.pbrParams[3] = mat.customParam;
        diffuseTexture = mat.diffuseTexture;
        envCubeMap = mat.envCubeMap;
        blendMode = mat.blendMode;
        depthTest = mat.depthTest;
        depthWrite = mat.depthWrite;
    }
};

/**
 * @brief [OPT-144 / OPT-152] Bloque Coalescido Multi-Luz alineado a 256 bytes para UBO / Registros KC2.
 */
struct alignas(256) LightBlockUBO {
    static constexpr uint32_t MAX_POINT_LIGHTS = 8;

    float dirLightDir[4]{0.60f, 0.55f, 0.58f, 1.0f};       // xyz = direction (default canonico ForwardRendererGX2)
    float dirLightColor[4]{1.0f, 0.96f, 0.88f, 32.0f};     // rgb = color, w = shininess
    float pointLightPosRadius[MAX_POINT_LIGHTS][4]{};      // xyz = world pos, w = radius
    float pointLightColorIntensity[MAX_POINT_LIGHTS][4]{}; // rgb = color, w = intensity
    uint32_t activePointLights{0};
    uint32_t pad[3]{0, 0, 0};
    // Luz focal / linterna (Spot Light — CGP Cap. 7)
    float spotLightPosRange[4]{0.0f, 5.0f, 6.0f, 25.0f};      // xyz = world pos, w = range
    float spotLightDirInner[4]{0.0f, -0.35f, -0.94f, 0.94f};  // xyz = unit dir, w = cos(innerAngle)
    float spotLightColorOuter[4]{1.0f, 0.96f, 0.88f, 0.85f};  // rgb = color, w = cos(outerAngle)
    float spotLightIntensity{0.0f};                            // 0.0f = apagada por defecto
    uint32_t reservedPad[39]{}; // Relleno exacto a 512 bytes (multiplo de 256B)

    void ClearPointLights() noexcept { activePointLights = 0; }

    void ClearSpotLight() noexcept { spotLightIntensity = 0.0f; }

    void SetSpotLight(const Point3D& pos,
                      const Point3D& dir,
                      const Point3D& color,
                      float intensity,
                      float range = 28.0f,
                      float cosInnerCutoff = 0.94f,
                      float cosOuterCutoff = 0.84f) noexcept {
        const Point3D n = dir.Normalized();
        spotLightPosRange[0] = pos.x;
        spotLightPosRange[1] = pos.y;
        spotLightPosRange[2] = pos.z;
        spotLightPosRange[3] = range;
        spotLightDirInner[0] = n.x;
        spotLightDirInner[1] = n.y;
        spotLightDirInner[2] = n.z;
        spotLightDirInner[3] = cosInnerCutoff;
        spotLightColorOuter[0] = color.x;
        spotLightColorOuter[1] = color.y;
        spotLightColorOuter[2] = color.z;
        spotLightColorOuter[3] = cosOuterCutoff;
        spotLightIntensity = intensity;
    }

    bool AddPointLight(const Point3D& pos, float radius, const Point3D& color, float intensity) noexcept {
        if (activePointLights >= MAX_POINT_LIGHTS) return false;
        const uint32_t i = activePointLights++;
        pointLightPosRadius[i][0] = pos.x;
        pointLightPosRadius[i][1] = pos.y;
        pointLightPosRadius[i][2] = pos.z;
        pointLightPosRadius[i][3] = radius;
        pointLightColorIntensity[i][0] = color.x;
        pointLightColorIntensity[i][1] = color.y;
        pointLightColorIntensity[i][2] = color.z;
        pointLightColorIntensity[i][3] = intensity;
        return true;
    }

    void SetDirectionalLight(const Point3D& dir, const Point3D& color, float shininess = 32.0f) noexcept {
        const Point3D n = dir.Normalized();
        dirLightDir[0] = n.x;
        dirLightDir[1] = n.y;
        dirLightDir[2] = n.z;
        dirLightDir[3] = 1.0f;
        dirLightColor[0] = color.x;
        dirLightColor[1] = color.y;
        dirLightColor[2] = color.z;
        dirLightColor[3] = shininess;
    }
};
static_assert(alignof(LightBlockUBO) == 256, "LightBlockUBO debe alinearse a 256B para Latte GX2");
static_assert((sizeof(LightBlockUBO) % 256) == 0, "LightBlockUBO debe ser multiplo de 256B");

using MaterialHandle = ResourceHandle<MaterialPropertyBlock>;

/**
 * @brief [OPT-13 / OPT-216 / OPT-365] Registro Generacional Lock-Free (0 mutex en hot path) de Materiales y Luces.
 */
class MaterialRegistry {
public:
    static constexpr size_t MAX_MATERIALS = 128;

    static MaterialRegistry& Instance() noexcept;

    MaterialRegistry(const MaterialRegistry&) = delete;
    MaterialRegistry& operator=(const MaterialRegistry&) = delete;
    MaterialRegistry(MaterialRegistry&&) = delete;
    MaterialRegistry& operator=(MaterialRegistry&&) = delete;

    [[nodiscard]] MaterialHandle CreateMaterial(const MaterialPropertyBlock& desc) noexcept;
    [[nodiscard]] MaterialPropertyBlock* GetMaterial(MaterialHandle handle) noexcept;
    [[nodiscard]] const MaterialPropertyBlock* GetMaterial(MaterialHandle handle) const noexcept;
    void DestroyMaterial(MaterialHandle handle) noexcept;
    void Clear() noexcept;

    [[nodiscard]] LightBlockUBO& GetActiveLightBlock() noexcept { return m_lightBlock; }
    [[nodiscard]] const LightBlockUBO& GetActiveLightBlock() const noexcept { return m_lightBlock; }

    [[nodiscard]] size_t GetActiveCount() const noexcept { return m_activeCount; }

private:
    MaterialRegistry() noexcept = default;
    ~MaterialRegistry() noexcept = default;

    struct Slot {
        MaterialPropertyBlock data{};
        uint16_t generation{1};
        bool active{false};
    };

    std::array<Slot, MAX_MATERIALS> m_slots{};
    size_t m_activeCount{0};
    LightBlockUBO m_lightBlock{};
};

} // namespace Neleo
