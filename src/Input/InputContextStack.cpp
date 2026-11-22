#include "Input/InputContextStack.hpp"

namespace WiiU {

void InputContextStack::Clear() noexcept {
    m_count = 0;
    m_consumedActionsMask = 0;
}

bool InputContextStack::PushContext(InputContext context, uint8_t priority,
                                    bool blockLowerContexts,
                                    uint32_t allowedActionMask) noexcept {
    // Evitar duplicados exactos: actualizar en su lugar y reordenar por prioridad
    for (size_t i = 0; i < m_count; ++i) {
        if (m_layers[i].context == context) {
            m_layers[i].priority = priority;
            m_layers[i].blockLowerContexts = blockLowerContexts;
            m_layers[i].allowedActionMask = allowedActionMask;
            // Reordenar estable ascendente por prioridad (el tope es m_count - 1)
            for (size_t a = 1; a < m_count; ++a) {
                InputContextLayer key = m_layers[a];
                size_t b = a;
                while (b > 0 && m_layers[b - 1].priority > key.priority) {
                    m_layers[b] = m_layers[b - 1];
                    --b;
                }
                m_layers[b] = key;
            }
            return true;
        }
    }

    if (m_count >= MAX_LAYERS) {
        return false;
    }

    InputContextLayer layer{context, priority, blockLowerContexts, allowedActionMask};
    size_t insertPos = m_count;
    while (insertPos > 0 && m_layers[insertPos - 1].priority > priority) {
        m_layers[insertPos] = m_layers[insertPos - 1];
        --insertPos;
    }
    m_layers[insertPos] = layer;
    ++m_count;
    return true;
}

bool InputContextStack::PopContext() noexcept {
    if (m_count == 0) return false;
    --m_count;
    return true;
}

bool InputContextStack::RemoveContext(InputContext context) noexcept {
    for (size_t i = 0; i < m_count; ++i) {
        if (m_layers[i].context == context) {
            for (size_t j = i + 1; j < m_count; ++j) {
                m_layers[j - 1] = m_layers[j];
            }
            --m_count;
            return true;
        }
    }
    return false;
}

void InputContextStack::ConsumeAction(InputAction action) noexcept {
    m_consumedActionsMask |= ActionToBit(action);
}

bool InputContextStack::IsActionConsumed(InputAction action) const noexcept {
    return (m_consumedActionsMask & ActionToBit(action)) != 0u;
}

bool InputContextStack::IsActionAllowed(InputContext queryContext, InputAction action) const noexcept {
    const uint32_t bit = ActionToBit(action);
    if ((m_consumedActionsMask & bit) != 0u) {
        return false;
    }
    // Si la pila esta vacia, permitir retrocompatibilidad total
    if (m_count == 0) {
        return true;
    }

    // Recorrer de mayor a menor prioridad (top-down)
    for (size_t i = m_count; i > 0; --i) {
        const InputContextLayer& layer = m_layers[i - 1];
        if (layer.context == queryContext) {
            return (layer.allowedActionMask & bit) != 0u;
        }
        if (layer.blockLowerContexts) {
            return false;
        }
    }
    return false;
}

InputContext InputContextStack::GetActiveContext() const noexcept {
    if (m_count == 0) {
        return InputContext::Gameplay3D;
    }
    return m_layers[m_count - 1].context;
}

} // namespace WiiU
