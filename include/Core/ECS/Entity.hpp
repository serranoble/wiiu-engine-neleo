#pragma once

#include <compare>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <type_traits>

namespace Neleo {

// =============================================================================
// [OPT-266] Envoltorio de Coste Cero con Tipado Fuerte (StrongId / Phantom Type)
// =============================================================================
template <typename Tag, typename Underlying = uint32_t>
struct StrongId {
    Underlying value{ static_cast<Underlying>(~Underlying{0}) };

    constexpr StrongId() noexcept = default;
    constexpr explicit StrongId(Underlying val) noexcept : value(val) {}

    [[nodiscard]] constexpr Underlying Get() const noexcept { return value; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != static_cast<Underlying>(~Underlying{0}); }
    constexpr explicit operator bool() const noexcept { return IsValid(); }

    constexpr bool operator==(const StrongId& other) const noexcept = default;
    constexpr bool operator!=(const StrongId& other) const noexcept = default;
    constexpr auto operator<=>(const StrongId& other) const noexcept = default;

    [[nodiscard]] static constexpr StrongId Invalid() noexcept {
        return StrongId{ static_cast<Underlying>(~Underlying{0}) };
    }
};

// Tag para entidades ECS
struct EntityTag {};

using StrongEntityId = StrongId<EntityTag, uint32_t>;

/**
 * @brief [OPT-235 / OPT-266] Identificador generacional de entidad para arquitectura ECS (Entity Component System).
 * 
 * Empaquetado compacto de 32 bits:
 * - Bits 0..19 (20 bits): Índice de ranura en el mundo ECS (hasta 1,048,575 entidades).
 * - Bits 20..31 (12 bits): Contador generacional (hasta 4,095 reutilizaciones por ranura).
 * 
 * Si una entidad es destruida y su ranura reciclada, su generación se incrementa.
 * Cualquier sistema o referencia externa que retenga un handle obsoleto detectará la
 * desincronización en O(1) y fallará de forma segura, erradicando al 100% los errores
 * de Use-After-Free y punteros colgantes en el motor.
 */
struct Entity {
    uint32_t id{ 0xFFFFFFFF };

    static constexpr uint32_t INDEX_MASK = 0x000FFFFF; // 20 bits
    static constexpr uint32_t GEN_SHIFT  = 20;
    static constexpr uint32_t GEN_MASK   = 0x00000FFF; // 12 bits
    static constexpr uint32_t NULL_ID    = 0xFFFFFFFF;

    constexpr Entity() noexcept = default;
    constexpr explicit Entity(uint32_t rawId) noexcept : id(rawId) {}
    constexpr Entity(uint32_t index, uint32_t generation) noexcept
        : id((index & INDEX_MASK) | ((generation & GEN_MASK) << GEN_SHIFT)) {}

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return id != NULL_ID;
    }

    [[nodiscard]] constexpr uint32_t Index() const noexcept {
        return id & INDEX_MASK;
    }

    [[nodiscard]] constexpr uint32_t Generation() const noexcept {
        return (id >> GEN_SHIFT) & GEN_MASK;
    }

    constexpr explicit operator bool() const noexcept {
        return IsValid();
    }

    [[nodiscard]] constexpr StrongEntityId ToStrongId() const noexcept {
        return StrongEntityId{ id };
    }

    [[nodiscard]] static constexpr Entity FromStrongId(StrongEntityId strongId) noexcept {
        return Entity{ strongId.Get() };
    }

    constexpr bool operator==(const Entity& other) const noexcept = default;
    constexpr bool operator!=(const Entity& other) const noexcept = default;

    [[nodiscard]] static constexpr Entity Invalid() noexcept {
        return Entity{ NULL_ID };
    }

    static const Entity Null;
};

inline constexpr Entity Entity::Null{ 0xFFFFFFFF };

// [OPT-266] Verificaciones estáticas de diseño de memoria de coste cero
static_assert(sizeof(Entity) == sizeof(uint32_t), "Entity must be zero-cost 32-bit scalar wrapper");
static_assert(std::is_standard_layout_v<Entity>, "Entity must have standard layout");
static_assert(std::is_trivially_copyable_v<Entity>, "Entity must be trivially copyable");
static_assert(sizeof(StrongEntityId) == sizeof(uint32_t), "StrongEntityId must be zero-cost 32-bit wrapper");
static_assert(std::is_standard_layout_v<StrongEntityId>, "StrongEntityId must have standard layout");

} // namespace Neleo

namespace std {
template <>
struct hash<Neleo::Entity> {
    [[nodiscard]] size_t operator()(const Neleo::Entity& entity) const noexcept {
        return static_cast<size_t>(entity.id);
    }
};

template <typename Tag, typename Underlying>
struct hash<Neleo::StrongId<Tag, Underlying>> {
    [[nodiscard]] size_t operator()(const Neleo::StrongId<Tag, Underlying>& id) const noexcept {
        return static_cast<size_t>(id.Get());
    }
};
} // namespace std
