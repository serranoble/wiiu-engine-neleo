#pragma once

#include "Graphics/Math3D.hpp"

namespace Neleo {

/**
 * @brief [OPT-241] Componente de Transformación Espacial 3D orientado a datos (Data-Oriented POD).
 * 
 * Modela la posición, rotación Euler (radianes) y escala en espacio local/mundo.
 * Incluye bandera sucia (`isDirty`) para evitar recálculos matriciales innecesarios
 * cuando los actores permanecen estáticos.
 */
struct TransformComponent {
    Point3D position{0.0f, 0.0f, 0.0f};
    Point3D rotation{0.0f, 0.0f, 0.0f}; // Radianes (rotX, rotY, rotZ)
    Point3D scale{1.0f, 1.0f, 1.0f};
    Matrix4x4 worldMatrix{Matrix4x4::Identity()};
    bool isDirty{true};

    constexpr TransformComponent() noexcept = default;
    constexpr TransformComponent(const Point3D& pos, const Point3D& rot = {0,0,0}, const Point3D& scl = {1,1,1}) noexcept
        : position(pos), rotation(rot), scale(scl), worldMatrix(Matrix4x4::Identity()), isDirty(true) {}

    void SetPosition(const Point3D& pos) noexcept {
        position = pos;
        isDirty = true;
    }

    void SetRotation(const Point3D& rot) noexcept {
        rotation = rot;
        isDirty = true;
    }

    void SetScale(const Point3D& scl) noexcept {
        scale = scl;
        isDirty = true;
    }

    void MarkDirty() noexcept {
        isDirty = true;
    }
};

} // namespace Neleo
