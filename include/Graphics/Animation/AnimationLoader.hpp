#pragma once

#include "Graphics/Animation/Skeleton.hpp"
#include "Graphics/Animation/AnimationClip.hpp"
#include "Core/AssetManager.hpp"
#include <vector>
#include <cstring>
#include <algorithm>

namespace WiiU {

/**
 * @brief [OPT-337] Cargador de Formatos Binarios de Esqueleto (.wskel) y Animación (.wanim).
 * 
 * Implementa lectura branchless con decodificación big-endian nativa,
 * sin alocaciones dinámicas en caliente y con verificación de magia de 32 bits.
 */
class AnimationLoader {
public:
    static bool LoadSkeleton(const char* path, Skeleton& outSkeleton) {
        std::vector<uint8_t> buffer;
        if (!AssetManager::LoadBinaryFile(path, buffer) || buffer.size() < 32) {
            return false;
        }

        const uint8_t* ptr = buffer.data();
        if (std::memcmp(ptr, "WSKL", 4) != 0) return false;

        auto readU16 = [](const uint8_t* p) -> uint16_t {
            return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
        };
        auto readF32 = [](const uint8_t* p) -> float {
            uint32_t u = (static_cast<uint32_t>(p[0]) << 24) |
                         (static_cast<uint32_t>(p[1]) << 16) |
                         (static_cast<uint32_t>(p[2]) << 8)  |
                         static_cast<uint32_t>(p[3]);
            float f;
            std::memcpy(&f, &u, 4);
            return f;
        };

        uint16_t version = readU16(ptr + 4);
        uint16_t jointCount = readU16(ptr + 6);
        constexpr size_t JOINT_RECORD_SIZE = 164; // 32(name) + 2(parent) + 2(res) + 64(inv) + 64(local)
        if (version != 1 || jointCount == 0 || jointCount > MAX_SKELETON_JOINTS ||
            buffer.size() < (32u + static_cast<size_t>(jointCount) * JOINT_RECORD_SIZE)) {
            return false;
        }

        outSkeleton.jointCount = jointCount;
        const uint8_t* jointPtr = ptr + 32;
        const uint8_t* bufferEnd = ptr + buffer.size();

        for (uint16_t i = 0; i < jointCount; ++i) {
            if (jointPtr + JOINT_RECORD_SIZE > bufferEnd) {
                return false;
            }

            auto& joint = outSkeleton.joints[i];
            std::memcpy(joint.name, jointPtr, 32);
            joint.name[31] = '\0';
            joint.parentIndex = static_cast<int16_t>(readU16(jointPtr + 32));
            joint.reserved = 0;

            const uint8_t* invMatPtr = jointPtr + 36;
            for (int k = 0; k < 16; ++k) {
                joint.invBindPose.m[k] = readF32(invMatPtr + k * 4);
            }

            const uint8_t* localMatPtr = jointPtr + 36 + 64;
            for (int k = 0; k < 16; ++k) {
                joint.localBindPose.m[k] = readF32(localMatPtr + k * 4);
            }

            jointPtr += 36 + 128; // 164 bytes
        }

        return true;
    }

    static bool LoadClips(const char* path, std::vector<AnimationClip>& outClips) {
        std::vector<uint8_t> buffer;
        if (!AssetManager::LoadBinaryFile(path, buffer) || buffer.size() < 32) {
            return false;
        }

        const uint8_t* ptr = buffer.data();
        if (std::memcmp(ptr, "WANM", 4) != 0) return false;

        auto readU16 = [](const uint8_t* p) -> uint16_t {
            return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
        };
        auto readU32 = [](const uint8_t* p) -> uint32_t {
            return (static_cast<uint32_t>(p[0]) << 24) |
                   (static_cast<uint32_t>(p[1]) << 16) |
                   (static_cast<uint32_t>(p[2]) << 8)  |
                   static_cast<uint32_t>(p[3]);
        };
        auto readF32 = [readU32](const uint8_t* p) -> float {
            uint32_t u = readU32(p);
            float f;
            std::memcpy(&f, &u, 4);
            return f;
        };

        uint16_t clipCount = readU16(ptr + 6);
        constexpr uint16_t MAX_SUPPORTED_CLIPS = 32;
        if (clipCount == 0 || clipCount > MAX_SUPPORTED_CLIPS || buffer.size() < (32u + static_cast<size_t>(clipCount) * 48u)) {
            return false;
        }
        outClips.clear();
        outClips.resize(clipCount);

        const uint8_t* cur = ptr + 32;
        const uint8_t* bufferEnd = ptr + buffer.size();

        for (uint16_t c = 0; c < clipCount; ++c) {
            if (cur + 48 > bufferEnd) return false;

            auto& clip = outClips[c];
            std::memcpy(clip.name, cur, 32);
            clip.name[31] = '\0';
            clip.duration = readF32(cur + 32);
            clip.frameRate = readF32(cur + 36);
            clip.isLooping = (readU32(cur + 40) != 0);
            clip.jointCount = readU32(cur + 44);
            cur += 48;

            uint32_t jCount = clip.jointCount;
            for (uint32_t j = 0; j < jCount; ++j) {
                if (cur + 4 > bufferEnd) return false;
                uint32_t kfCount = readU32(cur);
                cur += 4;
                if (cur + static_cast<size_t>(kfCount) * 44u > bufferEnd) return false;

                if (j < MAX_SKELETON_JOINTS) {
                    clip.tracks[j].keyframeCount = std::min(kfCount, MAX_KEYFRAMES_PER_TRACK);
                    for (uint32_t k = 0; k < kfCount; ++k) {
                        if (k < MAX_KEYFRAMES_PER_TRACK) {
                            auto& kf = clip.tracks[j].keyframes[k];
                            kf.time = readF32(cur + 0);
                            kf.translation = Point3D{readF32(cur + 4), readF32(cur + 8), readF32(cur + 12)};
                            kf.rotation = Quaternion{readF32(cur + 16), readF32(cur + 20), readF32(cur + 24), readF32(cur + 28)};
                            kf.scale = Point3D{readF32(cur + 32), readF32(cur + 36), readF32(cur + 40)};
                        }
                        cur += 44;
                    }
                } else {
                    cur += static_cast<size_t>(kfCount) * 44u;
                }
            }
        }

        return true;
    }
};

} // namespace WiiU
