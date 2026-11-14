#pragma once

#include "Graphics/RHI/RHIPipelineState.hpp"
#include <cstdint>

namespace WiiU {

/**
 * @brief [OPT-255] Gestor de Caché y Registro de Estados de Pipeline (PSO Cache).
 * 
 * Previene el despacho repetitivo de comandos de cambio de registro de hardware
 * a la GPU AMD Latte (`GX2SetDepthOnlyControl`, `GX2SetBlendControl`, etc.)
 * cuando el estado solicitado coincide con el actualmente configurado en silicio.
 */
class RHIPipelineCache {
public:
    static RHIPipelineCache& Instance() noexcept;

    // Aplica el estado de pipeline solicitado suprimiendo llamadas redundantes
    void BindState(const RHIPipelineState& state);

    // Invalida el estado en caché forzando reconfiguración en la próxima llamada
    void Invalidate() noexcept;

    [[nodiscard]] uint64_t GetCurrentHash() const noexcept { return m_currentHash; }
    [[nodiscard]] const RHIPipelineState& GetCurrentState() const noexcept { return m_currentState; }
    [[nodiscard]] uint32_t GetRedundantBypasses() const noexcept { return m_redundantBypasses; }
    [[nodiscard]] uint32_t GetStateChanges() const noexcept { return m_stateChanges; }
    [[nodiscard]] RHIStateBits GetLastDirtyBits() const noexcept { return m_lastDirtyBits; }

    void ResetStats() noexcept {
        m_redundantBypasses = 0;
        m_stateChanges = 0;
        m_lastDirtyBits = RHIStateBits::None;
    }

private:
    RHIPipelineCache() noexcept = default;
    ~RHIPipelineCache() = default;

    RHIPipelineCache(const RHIPipelineCache&) = delete;
    RHIPipelineCache& operator=(const RHIPipelineCache&) = delete;
    RHIPipelineCache(RHIPipelineCache&&) = delete;
    RHIPipelineCache& operator=(RHIPipelineCache&&) = delete;

    RHIPipelineState m_currentState{};
    uint64_t m_currentHash{0xFFFFFFFFFFFFFFFFULL}; // Hash inválido inicial
    uint32_t m_redundantBypasses{0};
    uint32_t m_stateChanges{0};
    RHIStateBits m_lastDirtyBits{RHIStateBits::None};
};

} // namespace WiiU
