#pragma once

#include "Graphics/Math3D.hpp"
#include "Graphics/MaterialSystem.hpp"
#include "Core/ECS/Components/MaterialComponent.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Neleo {

class ForwardRendererGX2;

enum class RenderLayer : uint8_t {
    DepthPrePass = 0,
    Opaque       = 1,
    AlphaCutout  = 2,
    Skybox       = 3,
    Transparent  = 4,
    Overlay      = 5
};

/**
 * @brief [OPT-27 / OPT-104 / OPT-130 / OPT-143 / OPT-247 / OPT-260]
 * Comando de dibujo sin estado (Stateless Render Command) agnostico a hardware con clave de 64 bits.
 */
struct RenderCommand {
    uint64_t sortKey{0};
    RenderLayer layer{RenderLayer::Opaque};

    void* vbo{nullptr};
    void* ibo{nullptr};
    uint32_t indexCount{0};
    uint32_t vertexStride{48};
    bool use32BitIndices{false};

    Matrix4x4 modelMatrix{};

    RHITexturePtr diffuseTexture{nullptr};
    RHITexturePtr envCubeMap{nullptr};
    float reflectionIntensity{0.0f};
    bool normalMapEnabled{true};
    float bumpScale{2.0f};
    float ambientIntensity{0.38f};

    // Opcional: Skinning LBS
    const Matrix4x4* boneMatrices{nullptr};
    uint32_t boneCount{0};
    bool isSkinned{false};
    bool clayMode{false};

    BlendMode blendMode{BlendMode::Opaque};
    bool depthTest{true};
    bool depthWrite{true};
};

/**
 * @brief [OPT-27 / OPT-130 / OPT-260] Cola de Renderizado Stateless con Clave de 64 Bits (Zero-Alloc).
 */
class RenderQueue {
public:
    static constexpr size_t MAX_COMMANDS = 512;

    void Clear() noexcept { m_count = 0; }

    [[nodiscard]] static uint64_t BuildSortKey(RenderLayer layer,
                                               BlendMode blendMode,
                                               uint16_t shaderId,
                                               uint16_t materialId,
                                               float viewDepth) noexcept;

    bool Submit(const RenderCommand& cmd) noexcept;

    void Sort() noexcept;

    /**
     * @brief Despacha todos los comandos ordenados hacia ForwardRendererGX2.
     */
    void Flush(ForwardRendererGX2& renderer) const;

    /**
     * @brief Despacha unicamente comandos dentro del rango de capas [minLayer, maxLayer].
     *        Permite intercalar SkyboxRenderPass entre Opaque/AlphaCutout y Transparent.
     */
    void FlushRange(ForwardRendererGX2& renderer,
                    RenderLayer minLayer,
                    RenderLayer maxLayer,
                    bool earlyZEqualPass = false) const;

    /**
     * @brief Despacha unicamente los comandos opacos en modo Depth Pre-Pass (Early-Z).
     */
    void FlushDepthPrePass(ForwardRendererGX2& renderer) const;

    [[nodiscard]] size_t GetCommandCount() const noexcept { return m_count; }
    [[nodiscard]] const RenderCommand* GetCommands() const noexcept { return m_commands.data(); }

private:
    struct SortEntry {
        uint64_t key{0};
        uint16_t cmdIndex{0};
    };

    std::array<RenderCommand, MAX_COMMANDS> m_commands{};
    std::array<SortEntry, MAX_COMMANDS> m_sortEntries{};
    size_t m_count{0};
};

} // namespace Neleo
