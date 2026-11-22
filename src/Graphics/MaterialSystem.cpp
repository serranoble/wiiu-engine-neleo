#include "Graphics/MaterialSystem.hpp"

namespace WiiU {

MaterialRegistry& MaterialRegistry::Instance() noexcept {
    static MaterialRegistry instance;
    return instance;
}

MaterialHandle MaterialRegistry::CreateMaterial(const MaterialPropertyBlock& desc) noexcept {
    for (uint16_t i = 0; i < static_cast<uint16_t>(MAX_MATERIALS); ++i) {
        if (!m_slots[i].active) {
            m_slots[i].data = desc;
            m_slots[i].active = true;
            ++m_activeCount;
            return MaterialHandle(i, m_slots[i].generation);
        }
    }
    return MaterialHandle{};
}

MaterialPropertyBlock* MaterialRegistry::GetMaterial(MaterialHandle handle) noexcept {
    const uint16_t idx = handle.GetIndex();
    if (!handle.IsValid() || idx >= MAX_MATERIALS) return nullptr;
    Slot& s = m_slots[idx];
    if (!s.active || s.generation != handle.GetGeneration()) return nullptr;
    return &s.data;
}

const MaterialPropertyBlock* MaterialRegistry::GetMaterial(MaterialHandle handle) const noexcept {
    const uint16_t idx = handle.GetIndex();
    if (!handle.IsValid() || idx >= MAX_MATERIALS) return nullptr;
    const Slot& s = m_slots[idx];
    if (!s.active || s.generation != handle.GetGeneration()) return nullptr;
    return &s.data;
}

void MaterialRegistry::DestroyMaterial(MaterialHandle handle) noexcept {
    const uint16_t idx = handle.GetIndex();
    if (!handle.IsValid() || idx >= MAX_MATERIALS) return;
    Slot& s = m_slots[idx];
    if (!s.active || s.generation != handle.GetGeneration()) return;
    s.active = false;
    s.generation = static_cast<uint16_t>((s.generation + 1u) == 0u ? 1u : (s.generation + 1u));
    if (m_activeCount > 0) --m_activeCount;
}

void MaterialRegistry::Clear() noexcept {
    for (auto& s : m_slots) {
        if (s.active) {
            s.active = false;
            s.generation = static_cast<uint16_t>((s.generation + 1u) == 0u ? 1u : (s.generation + 1u));
        }
    }
    m_activeCount = 0;
    m_lightBlock.ClearPointLights();
}

} // namespace WiiU
