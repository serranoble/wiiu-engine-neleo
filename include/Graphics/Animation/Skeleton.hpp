#pragma once

#include "Graphics/Math3D.hpp"
#include <cstdint>
#include <cstring>

namespace WiiU {

constexpr uint32_t MAX_SKELETON_JOINTS = 24;

/**
 * @brief [OPT-332] Articulación individual de un esqueleto con ordenamiento topológico.
 * 
 * Basado en Jason Gregory (Game Engine Architecture, 3rd Ed., Cap. 11.2).
 * Garantiza que parentIndex < currentIndex (excepto raíz con -1),
 * permitiendo resolver la pose global en un único bucle lineal hacia adelante O(N) sin recursión.
 */
struct SkeletonJoint {
    char name[32]{};
    int16_t parentIndex{-1};
    int16_t reserved{0};
    Matrix4x4 invBindPose{Matrix4x4::Identity()};   // Matriz inversa de postura de reposo (Espacio Modelo -> Espacio Hueso)
    Matrix4x4 localBindPose{Matrix4x4::Identity()}; // Matriz local en postura de reposo
};

/**
 * @brief Estructura de datos POD que contiene la jerarquía de huesos completa.
 */
struct Skeleton {
    uint32_t jointCount{0};
    SkeletonJoint joints[MAX_SKELETON_JOINTS]{};

    int32_t FindJointIndex(const char* jointName) const noexcept {
        if (!jointName) return -1;
        for (uint32_t i = 0; i < jointCount; ++i) {
            if (std::strncmp(joints[i].name, jointName, sizeof(joints[i].name)) == 0) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }
};

} // namespace WiiU
