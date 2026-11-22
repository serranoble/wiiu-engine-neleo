#pragma once

#include "Graphics/Animation/Skeleton.hpp"
#include "Graphics/Animation/AnimationClip.hpp"
#include <cstdint>
#include <memory>

namespace WiiU {

/**
 * @brief Estructura de almacenamiento en heap para matrices de pose esquelética.
 * 
 * Separa el almacenamiento denso de matrices del struct SkeletalMeshComponent
 * para evitar que ComponentPool<SkeletalMeshComponent, 2048> reserve 25.7 MB contiguos.
 */
struct SkeletalPoseData {
    JointTransform localSQT[MAX_SKELETON_JOINTS]{};
    Matrix4x4 localPoses[MAX_SKELETON_JOINTS]{};
    Matrix4x4 worldPoses[MAX_SKELETON_JOINTS]{};
    alignas(256) Matrix4x4 skinningMatrices[MAX_SKELETON_JOINTS]{};
};

/**
 * @brief [OPT-334] Componente ECS de Malla Esquelética y Paleta de Articulaciones.
 * 
 * Almacena el esqueleto de referencia, las matrices de pose local, pose global
 * y la paleta de matrices de deformación (skinningMatrices = worldPose * invBindPose)
 * alineada para carga directa a registros uniformes KC2 en silicio Latte GX2.
 */
struct SkeletalMeshComponent {
    const Skeleton* skeleton{nullptr};
    uint32_t jointCount{0};
    std::unique_ptr<SkeletalPoseData> poseData{nullptr};
    JointTransform* localSQT{nullptr};
    Matrix4x4* localPoses{nullptr};
    Matrix4x4* worldPoses{nullptr};
    Matrix4x4* skinningMatrices{nullptr};

    SkeletalMeshComponent() noexcept = default;
    ~SkeletalMeshComponent() = default;

    SkeletalMeshComponent(SkeletalMeshComponent&& other) noexcept
        : skeleton(other.skeleton), jointCount(other.jointCount), poseData(std::move(other.poseData)) {
        SyncPointers();
        other.skeleton = nullptr;
        other.jointCount = 0;
        other.SyncPointers();
    }

    SkeletalMeshComponent& operator=(SkeletalMeshComponent&& other) noexcept {
        if (this != &other) {
            skeleton = other.skeleton;
            jointCount = other.jointCount;
            poseData = std::move(other.poseData);
            SyncPointers();
            other.skeleton = nullptr;
            other.jointCount = 0;
            other.SyncPointers();
        }
        return *this;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return skeleton != nullptr && poseData != nullptr && localPoses != nullptr;
    }

    SkeletalMeshComponent(const SkeletalMeshComponent& other)
        : skeleton(other.skeleton), jointCount(other.jointCount) {
        if (other.poseData) {
            poseData = std::make_unique<SkeletalPoseData>(*other.poseData);
        }
        SyncPointers();
    }

    SkeletalMeshComponent& operator=(const SkeletalMeshComponent& other) {
        if (this != &other) {
            skeleton = other.skeleton;
            jointCount = other.jointCount;
            if (other.poseData) {
                poseData = std::make_unique<SkeletalPoseData>(*other.poseData);
            } else {
                poseData.reset();
            }
            SyncPointers();
        }
        return *this;
    }

    explicit SkeletalMeshComponent(const Skeleton* skel)
        : skeleton(skel),
          jointCount(skel ? (skel->jointCount > MAX_SKELETON_JOINTS ? MAX_SKELETON_JOINTS : skel->jointCount) : 0),
          poseData(std::make_unique<SkeletalPoseData>()) {
        SyncPointers();
        if (skel && poseData) {
            for (uint32_t i = 0; i < jointCount; ++i) {
                localPoses[i] = skel->joints[i].localBindPose;
                localSQT[i].translation = localPoses[i].GetTranslation();
                localSQT[i].rotation = localPoses[i].ToQuaternion();
                localSQT[i].scale = Point3D{1.0f, 1.0f, 1.0f};
                worldPoses[i] = Matrix4x4::Identity();
                skinningMatrices[i] = Matrix4x4::Identity();
            }
        }
    }

    /**
     * @brief Recomputes worldPoses and skinningMatrices in topological O(N) order from localPoses.
     */
    void RecomputeWorldAndSkinningMatrices() noexcept {
        if (!IsValid()) return;
        const uint32_t safeCount = (jointCount > MAX_SKELETON_JOINTS) ? MAX_SKELETON_JOINTS : jointCount;
        for (uint32_t i = 0; i < safeCount; ++i) {
            const int16_t parentIdx = skeleton->joints[i].parentIndex;
            if (parentIdx >= 0 && parentIdx < static_cast<int16_t>(i)) {
                worldPoses[i] = worldPoses[parentIdx] * localPoses[i];
            } else {
                worldPoses[i] = localPoses[i];
            }
            skinningMatrices[i] = worldPoses[i] * skeleton->joints[i].invBindPose;
        }
    }

private:
    void SyncPointers() noexcept {
        if (poseData) {
            localSQT = poseData->localSQT;
            localPoses = poseData->localPoses;
            worldPoses = poseData->worldPoses;
            skinningMatrices = poseData->skinningMatrices;
        } else {
            localSQT = nullptr;
            localPoses = nullptr;
            worldPoses = nullptr;
            skinningMatrices = nullptr;
        }
    }
};

} // namespace WiiU
