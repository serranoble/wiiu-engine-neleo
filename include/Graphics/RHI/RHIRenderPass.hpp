#pragma once

#include <cstdint>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/mem.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#endif

namespace Neleo {

enum class RHILoadAction : uint8_t {
    Load = 0,
    Clear,
    DontCare
};

enum class RHIStoreAction : uint8_t {
    Store = 0,
    DontCare
};

enum class RHIAccessFlags : uint16_t {
    None                 = 0,
    ColorAttachmentWrite = 1 << 0,
    DepthStencilWrite    = 1 << 1,
    ShaderResourceRead   = 1 << 2,
    TransferWrite        = 1 << 3,
    All                  = 0xFFFF
};

inline constexpr RHIAccessFlags operator|(RHIAccessFlags a, RHIAccessFlags b) noexcept {
    return static_cast<RHIAccessFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline constexpr RHIAccessFlags operator&(RHIAccessFlags a, RHIAccessFlags b) noexcept {
    return static_cast<RHIAccessFlags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

inline constexpr RHIAccessFlags operator~(RHIAccessFlags a) noexcept {
    return static_cast<RHIAccessFlags>(~static_cast<uint16_t>(a));
}

inline constexpr bool HasAny(RHIAccessFlags mask, RHIAccessFlags query) noexcept {
    return (static_cast<uint16_t>(mask) & static_cast<uint16_t>(query)) != 0;
}

/**
 * @brief [OPT-265] Descriptor declarativo de pase de renderizado e inserción de barreras.
 */
struct RHIRenderPassDesc {
    const char* name{"RenderPass"};
    RHILoadAction colorLoadAction{RHILoadAction::Load};
    RHIStoreAction colorStoreAction{RHIStoreAction::Store};
    RHILoadAction depthLoadAction{RHILoadAction::Load};
    RHIStoreAction depthStoreAction{RHIStoreAction::Store};

    float clearColor[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float clearDepth{1.0f};
    uint8_t clearStencil{0};

    RHIAccessFlags initialAccess{RHIAccessFlags::None};
    RHIAccessFlags finalAccess{RHIAccessFlags::ColorAttachmentWrite | RHIAccessFlags::DepthStencilWrite};

#if defined(__WIIU__) || defined(__wiiu__)
    GX2ContextState* contextState{nullptr};
    GX2ColorBuffer* colorBuffer{nullptr};
    GX2DepthBuffer* depthBuffer{nullptr};
#else
    void* contextState{nullptr};
    void* colorBuffer{nullptr};
    void* depthBuffer{nullptr};
#endif

    bool autoInvalidateCaches{true};
    bool autoFlushOnEnd{true};
    bool enableHiZFastClear{true};
};

/**
 * @brief [OPT-265] Orquestador de pases de renderizado con resolución automática de riesgos de hardware (Barriers).
 */
class RHIRenderPass {
public:
    static void Begin(const RHIRenderPassDesc& desc);
    static void End();

    [[nodiscard]] static bool IsInsidePass() noexcept { return s_insidePass; }
    [[nodiscard]] static const RHIRenderPassDesc& GetActivePassDesc() noexcept { return s_activeDesc; }
    [[nodiscard]] static uint32_t GetPassCount() noexcept { return s_passCount; }
    static void ResetStats() noexcept { s_passCount = 0; }

private:
    static inline bool s_insidePass{false};
    static inline RHIRenderPassDesc s_activeDesc{};
    static inline uint32_t s_passCount{0};
};

/**
 * @brief Guarda RAII para pases de renderizado declarativos.
 */
class ScopedRenderPass {
public:
    explicit ScopedRenderPass(const RHIRenderPassDesc& desc) {
        RHIRenderPass::Begin(desc);
    }
    ~ScopedRenderPass() {
        RHIRenderPass::End();
    }

    ScopedRenderPass(const ScopedRenderPass&) = delete;
    ScopedRenderPass& operator=(const ScopedRenderPass&) = delete;
    ScopedRenderPass(ScopedRenderPass&&) = delete;
    ScopedRenderPass& operator=(ScopedRenderPass&&) = delete;
};

} // namespace Neleo
