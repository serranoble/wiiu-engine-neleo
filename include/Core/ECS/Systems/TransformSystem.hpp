#pragma once

#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/HierarchyComponent.hpp"

namespace WiiU {

/**
 * @brief [OPT-245 / OPT-254] Sistema de Transformación y Cinemática Espacial 3D (TransformSystem).
 * 
 * Itera linealmente sobre el ComponentPool de TransformComponent y HierarchyComponent.
 * Ejecuta dirty-checking para recalcular matrices TRS únicamente en entidades modificadas,
 * y compone jerarquías espaciales padre-hijo (Flat Scene Graph) con CERO vtables ni allocations.
 */
class TransformSystem {
public:
    static void Update(ECSWorld& world);
};

} // namespace WiiU
