#pragma once

#include "Core/ECS/Components/MaterialComponent.hpp"
#include <cstdint>
#include <type_traits>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/enum.h>
#include <gx2/state.h>
#endif

namespace WiiU {

#if defined(__WIIU__) || defined(__wiiu__)
inline constexpr uint8_t RHI_COMPARE_LEQUAL = static_cast<uint8_t>(GX2_COMPARE_FUNC_LEQUAL); // 3
inline constexpr uint8_t RHI_COMPARE_ALWAYS = static_cast<uint8_t>(GX2_COMPARE_FUNC_ALWAYS); // 7
inline constexpr uint8_t RHI_FRONT_FACE_CCW = static_cast<uint8_t>(GX2_FRONT_FACE_CCW);     // 1
#else
inline constexpr uint8_t RHI_COMPARE_LEQUAL = 3;
inline constexpr uint8_t RHI_COMPARE_ALWAYS = 7;
inline constexpr uint8_t RHI_FRONT_FACE_CCW = 1;
#endif

// =============================================================================
// [OPT-262] Verificación en Tiempo de Compilación de Silicio Latte (Static Assertions)
// =============================================================================
#if defined(__WIIU__) || defined(__wiiu__)
static_assert(RHI_COMPARE_LEQUAL == static_cast<uint8_t>(GX2_COMPARE_FUNC_LEQUAL),
              "RHI_COMPARE_LEQUAL must match GX2_COMPARE_FUNC_LEQUAL in Cafe OS SDK");
static_assert(RHI_COMPARE_ALWAYS == static_cast<uint8_t>(GX2_COMPARE_FUNC_ALWAYS),
              "RHI_COMPARE_ALWAYS must match GX2_COMPARE_FUNC_ALWAYS in Cafe OS SDK");
static_assert(RHI_FRONT_FACE_CCW == static_cast<uint8_t>(GX2_FRONT_FACE_CCW),
              "RHI_FRONT_FACE_CCW must match GX2_FRONT_FACE_CCW in Cafe OS SDK");
#endif

// =============================================================================
// [OPT-264] Máscara de Bits Fuertemente Tipada para Seguimiento de Dirty Flags
// =============================================================================
enum class RHIStateBits : uint16_t {
    None        = 0,
    DepthTest   = 1 << 0,
    DepthWrite  = 1 << 1,
    DepthFunc   = 1 << 2,
    BlendMode   = 1 << 3,
    CullEnable  = 1 << 4,
    FrontFace   = 1 << 5,
    AllDepth    = DepthTest | DepthWrite | DepthFunc,
    AllBlend    = BlendMode,
    AllRaster   = CullEnable | FrontFace,
    All         = 0xFFFF
};

inline constexpr RHIStateBits operator|(RHIStateBits a, RHIStateBits b) noexcept {
    return static_cast<RHIStateBits>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline constexpr RHIStateBits operator&(RHIStateBits a, RHIStateBits b) noexcept {
    return static_cast<RHIStateBits>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

inline constexpr RHIStateBits operator~(RHIStateBits a) noexcept {
    return static_cast<RHIStateBits>(~static_cast<uint16_t>(a));
}

inline constexpr bool HasAny(RHIStateBits mask, RHIStateBits query) noexcept {
    return (static_cast<uint16_t>(mask) & static_cast<uint16_t>(query)) != 0;
}

// =============================================================================
// [OPT-262] Diagnóstico y Validación de Estados de Silicio (Silicon State Guard)
// =============================================================================
enum class RHIValidationResult : uint8_t {
    Valid = 0,
    InvalidDepthFunc,
    InvalidFrontFace,
    HazardDepthWriteAlways
};

/**
 * @brief [OPT-255 / OPT-262 / OPT-264] Estado inmutable de pipeline RHI (PSO).
 * 
 * Encapsula la configuración de profundidad, estarcido, mezcla y rasterizado.
 * - [OPT-255] Genera un hash compacto de 64 bits para erradicar llamadas redundantes.
 * - [OPT-262] Capa de validación en tiempo de compilación y ejecución anti-regresiones.
 * - [OPT-264] Dirty flags diferenciales por máscara de bits para despacho branchless en PPC 750CL.
 */
struct RHIPipelineState {
    bool depthTest{true};
    bool depthWrite{true};
    uint8_t depthFunc{RHI_COMPARE_LEQUAL}; // 3 = GX2_COMPARE_FUNC_LEQUAL
    BlendMode blendMode{BlendMode::Opaque};
    bool cullEnable{false};
    uint8_t frontFace{RHI_FRONT_FACE_CCW}; // 1 = GX2_FRONT_FACE_CCW

    constexpr RHIPipelineState() noexcept = default;

    constexpr RHIPipelineState(bool dTest, bool dWrite, uint8_t dFunc,
                               BlendMode bMode, bool cull = false, uint8_t fFace = RHI_FRONT_FACE_CCW) noexcept
        : depthTest(dTest), depthWrite(dWrite), depthFunc(dFunc),
          blendMode(bMode), cullEnable(cull), frontFace(fFace) {}

    [[nodiscard]] constexpr uint64_t ComputeHash() const noexcept {
        uint64_t h = 0;
        h |= static_cast<uint64_t>(depthTest ? 1 : 0) << 0;
        h |= static_cast<uint64_t>(depthWrite ? 1 : 0) << 1;
        h |= static_cast<uint64_t>(depthFunc & 0x0F) << 2;
        h |= static_cast<uint64_t>(static_cast<uint8_t>(blendMode) & 0x0F) << 6;
        h |= static_cast<uint64_t>(cullEnable ? 1 : 0) << 10;
        h |= static_cast<uint64_t>(frontFace & 0x03) << 11;
        return h;
    }

    [[nodiscard]] constexpr uint64_t GetHash() const noexcept {
        return ComputeHash();
    }

    // [OPT-264] Cálculo diferencial branchless de dirty bits entre dos estados
    [[nodiscard]] constexpr RHIStateBits ComputeDiff(const RHIPipelineState& other) const noexcept {
        uint16_t diff = 0;
        if (depthTest != other.depthTest)   diff |= static_cast<uint16_t>(RHIStateBits::DepthTest);
        if (depthWrite != other.depthWrite) diff |= static_cast<uint16_t>(RHIStateBits::DepthWrite);
        if (depthFunc != other.depthFunc)   diff |= static_cast<uint16_t>(RHIStateBits::DepthFunc);
        if (blendMode != other.blendMode)   diff |= static_cast<uint16_t>(RHIStateBits::BlendMode);
        if (cullEnable != other.cullEnable) diff |= static_cast<uint16_t>(RHIStateBits::CullEnable);
        if (frontFace != other.frontFace)   diff |= static_cast<uint16_t>(RHIStateBits::FrontFace);
        return static_cast<RHIStateBits>(diff);
    }

    // [OPT-262] Validación en tiempo de compilación y ejecución de invariantes de silicio
    [[nodiscard]] constexpr RHIValidationResult Validate() const noexcept {
        if (depthFunc > 7) {
            return RHIValidationResult::InvalidDepthFunc; // GX2_COMPARE_FUNC máximo es 7 (ALWAYS)
        }
        if (frontFace > 1) {
            return RHIValidationResult::InvalidFrontFace; // 0 = CW, 1 = CCW
        }
        // Detección de peligro: depthWrite activado con compare func ALWAYS en superficies translúcidas
        if (depthWrite && depthFunc == RHI_COMPARE_ALWAYS && blendMode != BlendMode::Opaque && blendMode != BlendMode::AlphaCutout) {
            return RHIValidationResult::HazardDepthWriteAlways;
        }
        return RHIValidationResult::Valid;
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return Validate() == RHIValidationResult::Valid;
    }

    [[nodiscard]] constexpr bool operator==(const RHIPipelineState& other) const noexcept {
        return depthTest == other.depthTest &&
               depthWrite == other.depthWrite &&
               depthFunc == other.depthFunc &&
               blendMode == other.blendMode &&
               cullEnable == other.cullEnable &&
               frontFace == other.frontFace;
    }

    [[nodiscard]] constexpr bool operator!=(const RHIPipelineState& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] static constexpr RHIPipelineState FromMaterial(const MaterialComponent& mat) noexcept {
        return RHIPipelineState(
            mat.depthTest,
            mat.depthWrite,
            RHI_COMPARE_LEQUAL, // 3 = GX2_COMPARE_FUNC_LEQUAL
            mat.blendMode,
            false,
            RHI_FRONT_FACE_CCW  // 1 = GX2_FRONT_FACE_CCW
        );
    }
};

static_assert(std::is_standard_layout_v<RHIPipelineState>, "RHIPipelineState must be standard layout POD");

} // namespace WiiU
