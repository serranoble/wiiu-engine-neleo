#pragma once

#include "Graphics/Math3D.hpp"
#include "Graphics/Animation/Skeleton.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace WiiU {

constexpr uint32_t MAX_KEYFRAMES_PER_TRACK = 64;

/**
 * @brief Fotograma clave para una articulación específica.
 */
struct JointKeyframe {
    float time{0.0f};
    Point3D translation{0.0f, 0.0f, 0.0f};
    Quaternion rotation{Quaternion::Identity()};
    Point3D scale{1.0f, 1.0f, 1.0f};
};

/**
 * @brief Pose local SQT (Scale-Quaternion-Translation) sin conversión prematura a matriz 4x4.
 * Ref: Jason Gregory, Game Engine Architecture (3rd Ed., Cap. 12.2 & 12.4).
 */
struct JointTransform {
    Point3D translation{0.0f, 0.0f, 0.0f};
    Quaternion rotation{Quaternion::Identity()};
    Point3D scale{1.0f, 1.0f, 1.0f};

    [[nodiscard]] Matrix4x4 ToMatrix() const noexcept {
        return Matrix4x4::FromTRS(translation, rotation, scale);
    }

    static JointTransform Blend(const JointTransform& a, const JointTransform& b, float alpha) noexcept {
        const float w = std::clamp(alpha, 0.0f, 1.0f);
        if (w <= 0.0001f) return a;
        if (w >= 0.9999f) return b;
        return JointTransform{
            Point3D{
                a.translation.x + (b.translation.x - a.translation.x) * w,
                a.translation.y + (b.translation.y - a.translation.y) * w,
                a.translation.z + (b.translation.z - a.translation.z) * w
            },
            Quaternion::Slerp(a.rotation, b.rotation, w),
            Point3D{
                a.scale.x + (b.scale.x - a.scale.x) * w,
                a.scale.y + (b.scale.y - a.scale.y) * w,
                a.scale.z + (b.scale.z - a.scale.z) * w
            }
        };
    }
};

/**
 * @brief Pista de animación con curvas temporales muestreadas para un hueso.
 */
struct JointTrack {
    uint32_t keyframeCount{0};
    JointKeyframe keyframes[MAX_KEYFRAMES_PER_TRACK]{};

    void Sample(float time, Point3D& outTrans, Quaternion& outRot, Point3D& outScale) const noexcept {
        if (keyframeCount == 0) {
            outTrans = Point3D{0.0f, 0.0f, 0.0f};
            outRot = Quaternion::Identity();
            outScale = Point3D{1.0f, 1.0f, 1.0f};
            return;
        }

        if (keyframeCount == 1 || time <= keyframes[0].time) {
            outTrans = keyframes[0].translation;
            outRot = keyframes[0].rotation;
            outScale = keyframes[0].scale;
            return;
        }

        if (time >= keyframes[keyframeCount - 1].time) {
            outTrans = keyframes[keyframeCount - 1].translation;
            outRot = keyframes[keyframeCount - 1].rotation;
            outScale = keyframes[keyframeCount - 1].scale;
            return;
        }

        // Búsqueda lineal O(K) optimizada en cache L1 (K <= 64)
        uint32_t nextIdx = 1;
        while (nextIdx < keyframeCount && keyframes[nextIdx].time < time) {
            ++nextIdx;
        }
        uint32_t prevIdx = nextIdx - 1;

        const auto& kf0 = keyframes[prevIdx];
        const auto& kf1 = keyframes[nextIdx];
        float dt = kf1.time - kf0.time;
        float factor = (dt > 1e-5f) ? ((time - kf0.time) / dt) : 0.0f;
        factor = std::clamp(factor, 0.0f, 1.0f);

        // Interpolación LERP para traslación y escala
        outTrans = Point3D{
            kf0.translation.x + (kf1.translation.x - kf0.translation.x) * factor,
            kf0.translation.y + (kf1.translation.y - kf0.translation.y) * factor,
            kf0.translation.z + (kf1.translation.z - kf0.translation.z) * factor
        };
        outScale = Point3D{
            kf0.scale.x + (kf1.scale.x - kf0.scale.x) * factor,
            kf0.scale.y + (kf1.scale.y - kf0.scale.y) * factor,
            kf0.scale.z + (kf1.scale.z - kf0.scale.z) * factor
        };

        // Interpolación SLERP para rotación en SO(3)
        outRot = Quaternion::Slerp(kf0.rotation, kf1.rotation, factor);
    }
};

/**
 * @brief [OPT-333] Clip de Animación Esquelética con pistas multihueso.
 * 
 * Basado en Jason Gregory (Game Engine Architecture, 3rd Ed., Cap. 11.3).
 */
struct AnimationClip {
    char name[32]{};
    float duration{1.0f};
    float frameRate{30.0f};
    bool isLooping{true};
    uint32_t jointCount{0};
    JointTrack tracks[MAX_SKELETON_JOINTS]{};

    [[nodiscard]] float ResolveSampleTime(float time) const noexcept {
        if (isLooping && duration > 0.0f) {
            float animTime = std::fmod(time, duration);
            return (animTime < 0.0f) ? (animTime + duration) : animTime;
        }
        return std::clamp(time, 0.0f, duration);
    }

    void SampleSQTPose(float time, JointTransform* outPoses, uint32_t maxPoses) const noexcept {
        if (!outPoses || maxPoses == 0) return;

        const float animTime = ResolveSampleTime(time);
        const uint32_t count = std::min(jointCount, maxPoses);
        for (uint32_t i = 0; i < count; ++i) {
            tracks[i].Sample(animTime, outPoses[i].translation, outPoses[i].rotation, outPoses[i].scale);
        }
    }

    void SamplePose(float time, Matrix4x4* outLocalPoses, uint32_t maxPoses) const noexcept {
        if (!outLocalPoses || maxPoses == 0) return;

        const float animTime = ResolveSampleTime(time);
        const uint32_t count = std::min(jointCount, maxPoses);
        for (uint32_t i = 0; i < count; ++i) {
            Point3D t;
            Quaternion r;
            Point3D s;
            tracks[i].Sample(animTime, t, r, s);
            outLocalPoses[i] = Matrix4x4::FromTRS(t, r, s);
        }
    }
};

} // namespace WiiU
