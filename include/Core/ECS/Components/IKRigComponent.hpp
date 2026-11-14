#pragma once

#include "Graphics/Math3D.hpp"
#include <algorithm>
#include <cstdint>

namespace WiiU {

/**
 * @brief Configuración analítica de cadena Two-Bone IK (Cadera-Rodilla-Pie / Hombro-Codo-Mano).
 *
 * Basado en Jason Gregory (Game Engine Architecture, 3ª Ed., Cap. 12.5.1).
 */
struct TwoBoneIKSolverConfig {
    int16_t rootJoint{-1};
    int16_t midJoint{-1};
    int16_t endJoint{-1};
    Point3D targetWorldPos{0.0f, 0.0f, 0.0f};
    Point3D poleVectorModel{0.0f, 0.0f, 1.0f};
    float weight{1.0f};
    float soften{0.995f};
    bool enabled{false};
};

/**
 * @brief Configuración analítica Look-At / Aim IK para cabeza y torso superior.
 *
 * Basado en Jason Gregory (Game Engine Architecture, 3ª Ed., Cap. 12.5.1).
 */
struct LookAtIKSolverConfig {
    int16_t chestJoint{-1};
    int16_t headJoint{-1};
    Point3D targetWorldPos{0.0f, 2.0f, 3.0f};
    Point3D localForwardAxis{0.0f, 0.0f, 1.0f};
    float chestWeight{0.30f};
    float headWeight{0.75f};
    float maxAngleRad{1.05f};
    bool enabled{false};
};

/**
 * @brief Configuración iterativa FABRIK (Forward And Backward Reaching Inverse Kinematics).
 *
 * Basado en Aristidou & Lasenby (2011) y Jason Gregory (3ª Ed., Cap. 12.5.2).
 * Opera en arreglos fijos en pila (MAX_CHAIN_JOINTS = 8) con 0 heap allocations.
 */
struct FABRIKChainConfig {
    static constexpr uint32_t MAX_CHAIN_JOINTS = 8;

    int16_t jointIndices[MAX_CHAIN_JOINTS]{-1, -1, -1, -1, -1, -1, -1, -1};
    uint32_t jointCount{0};
    Point3D targetWorldPos{0.0f, 0.0f, 0.0f};
    float weight{1.0f};
    float tolerance{0.002f};
    uint32_t maxIterations{6};
    bool enabled{false};
};

/**
 * @brief [OPT-125] Componente ECS para Cinemática Inversa Esquelética en Post-Procesamiento.
 *
 * Soporta hasta 4 cadenas analíticas Two-Bone IK (pies sobre terreno y brazos),
 * seguimiento Look-At IK acotado para pecho/cabeza y una cadena multi-hueso FABRIK.
 */
struct IKRigComponent {
    static constexpr uint32_t MAX_TWO_BONE_CHAINS = 4;

    TwoBoneIKSolverConfig twoBoneChains[MAX_TWO_BONE_CHAINS]{};
    uint32_t twoBoneChainCount{0};
    LookAtIKSolverConfig lookAt{};
    FABRIKChainConfig fabrikChain{};
    float pelvisOffsetY{0.0f};
    bool ikEnabled{true};
    uint32_t lastSolvedIterations{0};

    /**
     * @brief Registra una nueva cadena Two-Bone IK en el rig y retorna su índice (o -1 si está lleno).
     */
    int32_t AddTwoBoneChain(int16_t rootJoint, int16_t midJoint, int16_t endJoint,
                            const Point3D& poleVectorModel, float weight = 1.0f, bool enabled = true) noexcept {
        if (twoBoneChainCount >= MAX_TWO_BONE_CHAINS) return -1;
        const uint32_t idx = twoBoneChainCount++;
        twoBoneChains[idx].rootJoint = rootJoint;
        twoBoneChains[idx].midJoint = midJoint;
        twoBoneChains[idx].endJoint = endJoint;
        twoBoneChains[idx].poleVectorModel = poleVectorModel;
        twoBoneChains[idx].weight = std::clamp(weight, 0.0f, 1.0f);
        twoBoneChains[idx].soften = 0.995f;
        twoBoneChains[idx].enabled = enabled;
        return static_cast<int32_t>(idx);
    }

    /**
     * @brief Configura el solver Look-At / Aim IK de torso y cabeza.
     */
    void ConfigureLookAt(int16_t chestJoint, int16_t headJoint,
                         const Point3D& localForward = Point3D{0.0f, 0.0f, 1.0f},
                         float chestWeight = 0.30f, float headWeight = 0.75f,
                         float maxAngleRad = 1.05f, bool enabled = true) noexcept {
        lookAt.chestJoint = chestJoint;
        lookAt.headJoint = headJoint;
        lookAt.localForwardAxis = localForward;
        lookAt.chestWeight = std::clamp(chestWeight, 0.0f, 1.0f);
        lookAt.headWeight = std::clamp(headWeight, 0.0f, 1.0f);
        lookAt.maxAngleRad = maxAngleRad;
        lookAt.enabled = enabled;
    }

    /**
     * @brief Configura la cadena multi-articulación FABRIK ordenada desde raíz hasta efector final.
     */
    void ConfigureFABRIK(const int16_t* joints, uint32_t count,
                         uint32_t maxIters = 6, float tol = 0.002f, bool enabled = true) noexcept {
        if (!joints || count < 2) {
            fabrikChain.jointCount = 0;
            fabrikChain.enabled = false;
            return;
        }
        const uint32_t clampedCount = std::min(count, FABRIKChainConfig::MAX_CHAIN_JOINTS);
        fabrikChain.jointCount = clampedCount;
        for (uint32_t i = 0; i < clampedCount; ++i) {
            fabrikChain.jointIndices[i] = joints[i];
        }
        fabrikChain.maxIterations = std::max(maxIters, 1u);
        fabrikChain.tolerance = std::max(tol, 1e-4f);
        fabrikChain.enabled = enabled;
    }
};

} // namespace WiiU
