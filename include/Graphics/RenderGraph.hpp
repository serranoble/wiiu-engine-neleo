#pragma once

#include "Core/Diagnostics/EngineProfiler.hpp"
#include <array>
#include <cstdint>

namespace Neleo {

/**
 * @brief [OPT-465] Nodo declarativo de pase en el RenderGraph (Frostbite GDC 2017 / RTR4 Cap. 20).
 * Usa puntero a funcion plano + void* userData (Zero-Alloc, 0 std::function, 0 heap).
 */
struct RenderPassNode {
    using ExecuteFn = void (*)(void* userData);

    const char* name{nullptr};
    ProfileZone profileZone{ProfileZone::Render3D};
    ExecuteFn executeFn{nullptr};
    void* userData{nullptr};
    bool enabled{true};
};

/**
 * @brief [OPT-465] Render Graph / Frame Graph declarativo ligero y Zero-Alloc.
 * Orquesta secuencias multi-pass (ShadowPass, RTT Planar, HDR Scene, PostFX)
 * con perfilado automatico por zona en EngineProfiler.
 */
template <uint32_t MaxPasses = 16u>
class BasicRenderGraph {
    static_assert(MaxPasses > 0u, "BasicRenderGraph requiere MaxPasses > 0");
public:
    static constexpr uint32_t MAX_PASSES = MaxPasses;

    void Reset() noexcept {
        m_passCount = 0u;
        m_executedCount = 0u;
    }

    [[nodiscard]] bool AddPass(const char* name,
                               ProfileZone zone,
                               RenderPassNode::ExecuteFn fn,
                               void* userData = nullptr,
                               bool enabled = true) noexcept {
        if (m_passCount >= MAX_PASSES || !fn) {
            return false;
        }
        RenderPassNode& node = m_passes[m_passCount++];
        node.name = name;
        node.profileZone = zone;
        node.executeFn = fn;
        node.userData = userData;
        node.enabled = enabled;
        return true;
    }

    void Execute() noexcept {
        m_executedCount = 0u;
        for (uint32_t i = 0u; i < m_passCount; ++i) {
            const RenderPassNode& node = m_passes[i];
            if (!node.enabled || !node.executeFn) {
                continue;
            }
            ScopedProfileZone scope(node.profileZone);
            node.executeFn(node.userData);
            ++m_executedCount;
        }
    }

    [[nodiscard]] uint32_t GetPassCount() const noexcept { return m_passCount; }
    [[nodiscard]] uint32_t GetExecutedCount() const noexcept { return m_executedCount; }
    [[nodiscard]] const RenderPassNode& GetPass(uint32_t idx) const noexcept {
        return m_passes[idx < m_passCount ? idx : 0u];
    }

private:
    std::array<RenderPassNode, MAX_PASSES> m_passes{};
    uint32_t m_passCount{0u};
    uint32_t m_executedCount{0u};
};

using RenderGraph = BasicRenderGraph<16u>;
static_assert(sizeof(RenderGraph) <= 1024u, "RenderGraph debe ser ultra-ligero en stack (<= 1 KB)");

} // namespace Neleo
