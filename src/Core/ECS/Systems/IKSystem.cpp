#include "Core/ECS/Systems/IKSystem.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/SkeletalMeshComponent.hpp"
#include "Core/ECS/Components/IKRigComponent.hpp"
#include <algorithm>
#include <cmath>

namespace Neleo {

namespace {

Matrix4x4 GetUpToDateEntityModelMatrix(const TransformComponent& t) noexcept {
    if (!t.isDirty) {
        return t.worldMatrix;
    }
    const Matrix4x4 tMat = Matrix4x4::Translation(t.position.x, t.position.y, t.position.z);
    const Matrix4x4 rxMat = Matrix4x4::RotationX(t.rotation.x);
    const Matrix4x4 ryMat = Matrix4x4::RotationY(t.rotation.y);
    const Matrix4x4 rzMat = Matrix4x4::RotationZ(t.rotation.z);
    const Matrix4x4 sMat = Matrix4x4::Scale(t.scale.x, t.scale.y, t.scale.z);
    return tMat * (ryMat * (rxMat * rzMat)) * sMat;
}

bool IsValidJointIndex(const SkeletalMeshComponent& skelMesh, int16_t jointIdx) noexcept {
    return jointIdx >= 0 && static_cast<uint32_t>(jointIdx) < skelMesh.jointCount;
}

/**
 * @brief Aplica una rotación delta definida en Model Space al hueso `jointIdx`,
 * conjugándola al espacio local de su padre sin alterar la traslación ni la escala locales.
 */
void ApplyModelDeltaRotation(SkeletalMeshComponent& skelMesh, int16_t jointIdx, const Quaternion& qDeltaModel) noexcept {
    if (!IsValidJointIndex(skelMesh, jointIdx)) return;

    const auto& skeleton = *skelMesh.skeleton;
    const uint32_t idx = static_cast<uint32_t>(jointIdx);
    const int16_t parentIdx = skeleton.joints[idx].parentIndex;

    Quaternion parentWorldRot = Quaternion::Identity();
    if (parentIdx >= 0 && static_cast<uint32_t>(parentIdx) < skelMesh.jointCount) {
        parentWorldRot = skelMesh.worldPoses[parentIdx].ToQuaternion();
    }

    const Quaternion localDelta = (parentWorldRot.Conjugate() * qDeltaModel * parentWorldRot).Normalized();
    JointTransform& sqt = skelMesh.localSQT[idx];
    sqt.translation = skelMesh.localPoses[idx].GetTranslation();
    sqt.rotation = (localDelta * skelMesh.localPoses[idx].ToQuaternion()).Normalized();
    skelMesh.localPoses[idx] = Matrix4x4::FromTRS(sqt.translation, sqt.rotation, sqt.scale);
}

Quaternion ClampQuaternionAngle(const Quaternion& qIn, float maxAngleRad) noexcept {
    Quaternion q = qIn;
    if (q.w < 0.0f) {
        q = Quaternion{-q.x, -q.y, -q.z, -q.w};
    }
    const float clampedW = std::clamp(q.w, -1.0f, 1.0f);
    const float angle = 2.0f * std::acos(clampedW);
    if (angle <= maxAngleRad || angle < 1e-5f) {
        return q;
    }
    const float factor = std::clamp(maxAngleRad / angle, 0.0f, 1.0f);
    return Quaternion::Slerp(Quaternion::Identity(), q, factor);
}

void SolveLookAtSingleJoint(SkeletalMeshComponent& skelMesh, int16_t jointIdx,
                            const Point3D& targetModelPos, const Point3D& localForwardAxis,
                            float weight, float maxAngleRad) noexcept {
    if (!IsValidJointIndex(skelMesh, jointIdx) || weight <= 0.001f) return;

    const Point3D jointPos = skelMesh.worldPoses[jointIdx].GetTranslation();
    const Point3D toTarget = targetModelPos - jointPos;
    if (Point3D::Dot(toTarget, toTarget) < 1e-5f) return;

    const Point3D currentForward = skelMesh.worldPoses[jointIdx].TransformDirection(localForwardAxis).Normalized();
    const Point3D desiredForward = toTarget.Normalized();

    const Quaternion fullDelta = Quaternion::FromToRotation(currentForward, desiredForward);
    const Quaternion clampedDelta = ClampQuaternionAngle(fullDelta, maxAngleRad);
    const Quaternion weightedDelta = Quaternion::Slerp(Quaternion::Identity(), clampedDelta, std::clamp(weight, 0.0f, 1.0f));

    ApplyModelDeltaRotation(skelMesh, jointIdx, weightedDelta);
    skelMesh.RecomputeWorldAndSkinningMatrices();
}

void SolveTwoBoneChain(SkeletalMeshComponent& skelMesh, const TwoBoneIKSolverConfig& cfg,
                       const Matrix4x4& invEntityModel) noexcept {
    if (!cfg.enabled || cfg.weight <= 0.001f) return;
    if (!IsValidJointIndex(skelMesh, cfg.rootJoint) ||
        !IsValidJointIndex(skelMesh, cfg.midJoint) ||
        !IsValidJointIndex(skelMesh, cfg.endJoint)) {
        return;
    }

    const Point3D p0 = skelMesh.worldPoses[cfg.rootJoint].GetTranslation();
    const Point3D p1 = skelMesh.worldPoses[cfg.midJoint].GetTranslation();
    const Point3D p2 = skelMesh.worldPoses[cfg.endJoint].GetTranslation();

    const float len1 = (p1 - p0).Length();
    const float len2 = (p2 - p1).Length();
    if (len1 < 1e-4f || len2 < 1e-4f) return;

    const Point3D targetModel = invEntityModel.TransformPoint(cfg.targetWorldPos);
    const float w = std::clamp(cfg.weight, 0.0f, 1.0f);
    const Point3D blendedTarget = p2 + (targetModel - p2) * w;

    const Point3D rootToTarget = blendedTarget - p0;
    const float rawDist = rootToTarget.Length();
    if (rawDist < 1e-4f) return;

    const float minReach = std::abs(len1 - len2) + 0.005f;
    const float maxReach = (len1 + len2) * std::clamp(cfg.soften, 0.85f, 0.9999f);
    const float dist = std::clamp(rawDist, minReach, maxReach);

    const Point3D u = rootToTarget * (1.0f / rawDist);
    const float cosAlpha = std::clamp((len1 * len1 + dist * dist - len2 * len2) / (2.0f * len1 * dist), -1.0f, 1.0f);
    const float sinAlpha = std::sqrt(std::max(0.0f, 1.0f - cosAlpha * cosAlpha));

    // Construir plano de flexión ortogonal a u siguiendo poleVectorModel
    Point3D bendDir = cfg.poleVectorModel - u * Point3D::Dot(cfg.poleVectorModel, u);
    if (Point3D::Dot(bendDir, bendDir) < 1e-5f) {
        const Point3D currentLimb = p1 - p0;
        bendDir = currentLimb - u * Point3D::Dot(currentLimb, u);
    }
    if (Point3D::Dot(bendDir, bendDir) < 1e-5f) {
        bendDir = Point3D::Cross(Point3D{1.0f, 0.0f, 0.0f}, u);
        if (Point3D::Dot(bendDir, bendDir) < 1e-5f) {
            bendDir = Point3D::Cross(Point3D{0.0f, 1.0f, 0.0f}, u);
        }
    }
    bendDir = bendDir.Normalized();

    const Point3D p1Target = p0 + (u * cosAlpha + bendDir * sinAlpha) * len1;
    const Point3D p2Target = p0 + u * dist;

    // Paso 1: orientar hueso raíz hacia p1Target
    const Quaternion rootDelta = Quaternion::FromToRotation(p1 - p0, p1Target - p0);
    ApplyModelDeltaRotation(skelMesh, cfg.rootJoint, rootDelta);
    skelMesh.RecomputeWorldAndSkinningMatrices();

    // Paso 2: orientar hueso medio actualizado hacia p2Target
    const Point3D p1Updated = skelMesh.worldPoses[cfg.midJoint].GetTranslation();
    const Point3D p2Updated = skelMesh.worldPoses[cfg.endJoint].GetTranslation();
    const Quaternion midDelta = Quaternion::FromToRotation(p2Updated - p1Updated, p2Target - p1Updated);
    ApplyModelDeltaRotation(skelMesh, cfg.midJoint, midDelta);
    skelMesh.RecomputeWorldAndSkinningMatrices();
}

void SolveLookAt(SkeletalMeshComponent& skelMesh, const LookAtIKSolverConfig& cfg,
                 const Matrix4x4& invEntityModel) noexcept {
    if (!cfg.enabled) return;
    const Point3D targetModel = invEntityModel.TransformPoint(cfg.targetWorldPos);
    SolveLookAtSingleJoint(skelMesh, cfg.chestJoint, targetModel, cfg.localForwardAxis, cfg.chestWeight, cfg.maxAngleRad * 0.65f);
    SolveLookAtSingleJoint(skelMesh, cfg.headJoint, targetModel, cfg.localForwardAxis, cfg.headWeight, cfg.maxAngleRad);
}

uint32_t SolveFABRIK(SkeletalMeshComponent& skelMesh, const FABRIKChainConfig& cfg,
                     const Matrix4x4& invEntityModel) noexcept {
    if (!cfg.enabled || cfg.jointCount < 2 || cfg.weight <= 0.001f) return 0u;

    const uint32_t n = std::min(cfg.jointCount, FABRIKChainConfig::MAX_CHAIN_JOINTS);
    Point3D positions[FABRIKChainConfig::MAX_CHAIN_JOINTS]{};
    float lengths[FABRIKChainConfig::MAX_CHAIN_JOINTS]{};

    for (uint32_t k = 0; k < n; ++k) {
        if (!IsValidJointIndex(skelMesh, cfg.jointIndices[k])) return 0u;
        positions[k] = skelMesh.worldPoses[cfg.jointIndices[k]].GetTranslation();
    }

    float totalLength = 0.0f;
    for (uint32_t k = 0; k + 1 < n; ++k) {
        lengths[k] = (positions[k + 1] - positions[k]).Length();
        if (lengths[k] < 1e-4f) return 0u;
        totalLength += lengths[k];
    }

    const Point3D targetModel = invEntityModel.TransformPoint(cfg.targetWorldPos);
    const Point3D blendedTarget = positions[n - 1] + (targetModel - positions[n - 1]) * std::clamp(cfg.weight, 0.0f, 1.0f);
    const Point3D rootOrigin = positions[0];
    const float distToTarget = (blendedTarget - rootOrigin).Length();

    uint32_t iterationsUsed = 0;
    if (distToTarget >= totalLength) {
        const Point3D dir = (blendedTarget - rootOrigin).Normalized();
        for (uint32_t k = 0; k + 1 < n; ++k) {
            positions[k + 1] = positions[k] + dir * lengths[k];
        }
        iterationsUsed = 1;
    } else {
        for (uint32_t iter = 0; iter < cfg.maxIterations; ++iter) {
            if ((positions[n - 1] - blendedTarget).Length() <= cfg.tolerance) {
                break;
            }
            ++iterationsUsed;

            // Fase Backward (efector final -> raíz)
            positions[n - 1] = blendedTarget;
            for (int32_t k = static_cast<int32_t>(n) - 2; k >= 0; --k) {
                const Point3D dir = (positions[k] - positions[k + 1]).Normalized();
                positions[k] = positions[k + 1] + dir * lengths[k];
            }

            // Fase Forward (raíz fija -> efector final)
            positions[0] = rootOrigin;
            for (uint32_t k = 0; k + 1 < n; ++k) {
                const Point3D dir = (positions[k + 1] - positions[k]).Normalized();
                positions[k + 1] = positions[k] + dir * lengths[k];
            }
        }
    }

    // Reconstruir rotaciones locales a partir de las posiciones convergidas de FABRIK
    for (uint32_t k = 0; k + 1 < n; ++k) {
        const int16_t jCur = cfg.jointIndices[k];
        const int16_t jNext = cfg.jointIndices[k + 1];
        const Point3D curStart = skelMesh.worldPoses[jCur].GetTranslation();
        const Point3D curEnd = skelMesh.worldPoses[jNext].GetTranslation();
        const Quaternion deltaRot = Quaternion::FromToRotation(curEnd - curStart, positions[k + 1] - curStart);
        ApplyModelDeltaRotation(skelMesh, jCur, deltaRot);
        skelMesh.RecomputeWorldAndSkinningMatrices();
    }

    return iterationsUsed;
}

} // namespace

void IKSystem::Update(ECSWorld& world, float /*dt*/) noexcept {
    world.ForEach<TransformComponent, SkeletalMeshComponent, IKRigComponent>(
        [](Entity /*e*/, TransformComponent& transform, SkeletalMeshComponent& skelMesh, IKRigComponent& ikRig) {
            if (!ikRig.ikEnabled || !skelMesh.IsValid() || skelMesh.jointCount == 0) {
                return;
            }

            // Ajuste vertical opcional de pelvis para evitar hiperextensión de piernas sobre escalones
            if (std::abs(ikRig.pelvisOffsetY) > 1e-4f) {
                Point3D pelvisPos = skelMesh.localPoses[0].GetTranslation();
                pelvisPos.y += ikRig.pelvisOffsetY;
                skelMesh.localPoses[0].SetTranslation(pelvisPos);
                skelMesh.localSQT[0].translation = pelvisPos;
                skelMesh.RecomputeWorldAndSkinningMatrices();
            }

            const Matrix4x4 invEntityModel = Matrix4x4::InvertAffine(GetUpToDateEntityModelMatrix(transform));

            // 1. Look-At / Aim IK (pecho + cabeza)
            SolveLookAt(skelMesh, ikRig.lookAt, invEntityModel);

            // 2. Cadenas analíticas Two-Bone IK (piernas / brazos)
            const uint32_t chainCount = std::min(ikRig.twoBoneChainCount, IKRigComponent::MAX_TWO_BONE_CHAINS);
            for (uint32_t i = 0; i < chainCount; ++i) {
                SolveTwoBoneChain(skelMesh, ikRig.twoBoneChains[i], invEntityModel);
            }

            // 3. Solucionador iterativo multi-articulación FABRIK
            ikRig.lastSolvedIterations = SolveFABRIK(skelMesh, ikRig.fabrikChain, invEntityModel);
        });
}

} // namespace Neleo
