#pragma once

#include "Input/ActionMapping.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace WiiU {

/**
 * @brief [OPT-61 / OPT-204 / OPT-344 / OPT-400] Capa jerarquica de contexto de entrada.
 */
struct InputContextLayer {
    InputContext context{InputContext::Gameplay3D};
    uint8_t priority{0};
    bool blockLowerContexts{true};
    uint32_t allowedActionMask{0xFFFFFFFFu};
};

/**
 * @brief [OPT-61 / OPT-204 / OPT-344 / OPT-400] Pila jerarquica de contextos de entrada Zero-Alloc.
 *
 * Enruta y filtra acciones semanticas segun prioridad (ej. DebugConsole > UIModal > Gameplay3D),
 * permitiendo bloquear contextos inferiores y consumir acciones ya atendidas en el fotograma actual.
 */
class InputContextStack {
public:
    static constexpr size_t MAX_LAYERS = 8;

    void Clear() noexcept;

    bool PushContext(InputContext context, uint8_t priority = 0,
                     bool blockLowerContexts = true,
                     uint32_t allowedActionMask = 0xFFFFFFFFu) noexcept;

    bool PopContext() noexcept;
    bool RemoveContext(InputContext context) noexcept;

    void ResetFrameConsumptions() noexcept { m_consumedActionsMask = 0; }
    void ConsumeAction(InputAction action) noexcept;
    [[nodiscard]] bool IsActionConsumed(InputAction action) const noexcept;

    /**
     * @brief Evalua si el contexto `queryContext` tiene permiso para recibir `action` en este frame.
     */
    [[nodiscard]] bool IsActionAllowed(InputContext queryContext, InputAction action) const noexcept;

    [[nodiscard]] InputContext GetActiveContext() const noexcept;
    [[nodiscard]] size_t GetDepth() const noexcept { return m_count; }
    [[nodiscard]] bool IsEmpty() const noexcept { return m_count == 0; }

private:
    std::array<InputContextLayer, MAX_LAYERS> m_layers{};
    size_t m_count{0};
    uint32_t m_consumedActionsMask{0};
};

} // namespace WiiU
