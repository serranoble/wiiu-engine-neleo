#pragma once

#include "Core/ECS/Entity.hpp"
#include <cstdint>

namespace Neleo {

/**
 * @brief [OPT-254] Componente de Jerarquía Espacial de Escena orientado a datos (Flat Scene Graph).
 * 
 * Modela relaciones jerárquicas padre-hijo sin punteros recursivos ni asignaciones en heap.
 * Utiliza handles Entity generacionales en lista doblemente enlazada de hermanos.
 */
struct HierarchyComponent {
    Entity parent{Entity::Null};
    Entity firstChild{Entity::Null};
    Entity nextSibling{Entity::Null};
    Entity prevSibling{Entity::Null};
    uint16_t depth{0};

    constexpr HierarchyComponent() noexcept = default;
    constexpr explicit HierarchyComponent(Entity parentEntity, uint16_t entityDepth = 0) noexcept
        : parent(parentEntity), firstChild(Entity::Null), nextSibling(Entity::Null),
          prevSibling(Entity::Null), depth(entityDepth) {}

    [[nodiscard]] constexpr bool HasParent() const noexcept {
        return parent.IsValid();
    }

    [[nodiscard]] constexpr bool HasChildren() const noexcept {
        return firstChild.IsValid();
    }
};

} // namespace Neleo
