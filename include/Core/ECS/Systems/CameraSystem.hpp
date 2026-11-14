#pragma once

#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/CameraComponent.hpp"

namespace WiiU {

/**
 * @brief [OPT-253] Sistema de Cálculo de Cámaras 3D orientado a datos (CameraSystem).
 * 
 * Itera linealmente sobre (TransformComponent, CameraComponent).
 * Calcula matrices View, Projection y View-Projection en columna-mayor,
 * eliminando cálculos duplicados en escenas y permitiendo cámaras multi-objetivo.
 */
class CameraSystem {
public:
    static void Update(ECSWorld& world);
};

} // namespace WiiU
