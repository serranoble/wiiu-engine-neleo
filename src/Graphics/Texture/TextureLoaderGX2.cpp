#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include <cstring>
#include <string>
#include <vector>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/surface.h>
#endif

namespace WiiU::Graphics::TextureLoaderGX2 {

namespace {

constexpr uint32_t WTEX_MAGIC = 0x57544558u; // 'WTEX'
constexpr uint32_t WCUB_MAGIC = 0x57435542u; // 'WCUB'

[[nodiscard]] inline uint32_t ReadBE32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

[[nodiscard]] inline const char* SafeLabel(const char* debugLabel, const char* fallback) noexcept {
    return (debugLabel && debugLabel[0] != '\0') ? debugLabel : fallback;
}

} // namespace

bool LoadCookedTextureFromMemory(const uint8_t* data,
                                 size_t size,
                                 GX2Texture& outTex,
                                 const char* debugLabel) noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!data || size < 16) {
        return false;
    }

    const uint32_t magic = ReadBE32(data);
    const uint32_t width = ReadBE32(data + 4);
    const uint32_t height = ReadBE32(data + 8);
    const uint32_t rawMipAndFmt = ReadBE32(data + 12);
    uint32_t fmtCode = (rawMipAndFmt >> 16) & 0xFFFFu;
    if (fmtCode == 0) {
        fmtCode = 1;
    }

    const bool isBC1 = (fmtCode == 2);
    const bool isBC3 = (fmtCode == 3);
    const bool isBlockCompressed = isBC1 || isBC3;
    const uint32_t blockBytes = isBC1 ? 8u : (isBC3 ? 16u : 4u);

    if (magic != WTEX_MAGIC || width == 0 || height == 0 || width > 2048 || height > 2048) {
        return false;
    }

    const uint32_t blocksW0 = isBlockCompressed ? ((width + 3u) >> 2) : width;
    const uint32_t blocksH0 = isBlockCompressed ? ((height + 3u) >> 2) : height;
    const size_t baseBytes = static_cast<size_t>(blocksW0) * static_cast<size_t>(blocksH0) * static_cast<size_t>(blockBytes);
    if (size < (16ULL + baseBytes)) {
        return false;
    }

    DestroyTexture(outTex);
    std::memset(&outTex, 0, sizeof(GX2Texture));

    outTex.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    outTex.surface.width = width;
    outTex.surface.height = height;
    outTex.surface.depth = 1;
    outTex.surface.mipLevels = 1;
    outTex.surface.format = isBC1 ? GX2_SURFACE_FORMAT_UNORM_BC1
                          : (isBC3 ? GX2_SURFACE_FORMAT_UNORM_BC3
                                   : GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8);
    outTex.surface.aa = GX2_AA_MODE1X;
    outTex.surface.use = GX2_SURFACE_USE_TEXTURE;
    outTex.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    GX2CalcSurfaceSizeAndAlignment(&outTex.surface);
    const uint32_t surfAlign = outTex.surface.alignment > 256 ? outTex.surface.alignment : 256;
    outTex.surface.image = MemoryManager::Instance().AllocAligned(
        surfAlign,
        outTex.surface.imageSize,
        MemoryTag::Texture,
        SafeLabel(debugLabel, "CookedTex")
    );
    if (!outTex.surface.image) {
        return false;
    }

    const uint8_t* srcCursor = data + 16;
    uint8_t* dstImage = static_cast<uint8_t*>(outTex.surface.image);
    const uint32_t pitchBytes0 = outTex.surface.pitch * blockBytes;
    const uint32_t rowBytes0 = blocksW0 * blockBytes;
    if (rowBytes0 > pitchBytes0 ||
        static_cast<size_t>(blocksH0) * static_cast<size_t>(pitchBytes0) > outTex.surface.imageSize) {
        DestroyTexture(outTex);
        return false;
    }

    std::memset(dstImage, 0, outTex.surface.imageSize);
    for (uint32_t y = 0; y < blocksH0; ++y) {
        std::memcpy(dstImage + y * pitchBytes0, srcCursor + y * rowBytes0, rowBytes0);
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, outTex.surface.image, outTex.surface.imageSize);

    outTex.surface.mipmaps = nullptr;
    outTex.surface.mipmapSize = 0;
    outTex.viewFirstMip = 0;
    outTex.viewNumMips = 1;
    outTex.viewFirstSlice = 0;
    outTex.viewNumSlices = 1;
    outTex.compMap = 0x00010203;
    GX2InitTextureRegs(&outTex);
    return true;
#else
    (void)data; (void)size; (void)outTex; (void)debugLabel;
    return true;
#endif
}

bool LoadCookedTextureFromFile(std::string_view filepath,
                               GX2Texture& outTex,
                               const char* debugLabel) noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    std::vector<uint8_t> buffer;
    if (!AssetManager::LoadBinaryFile(std::string(filepath), buffer)) {
        return false;
    }
    return LoadCookedTextureFromMemory(buffer.data(), buffer.size(), outTex, debugLabel);
#else
    (void)filepath; (void)outTex; (void)debugLabel;
    return true;
#endif
}

bool CreateSolidColorTexture(GX2Texture& outTex,
                             uint32_t rgba8Color,
                             const char* debugLabel) noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    DestroyTexture(outTex);
    std::memset(&outTex, 0, sizeof(GX2Texture));

    outTex.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    outTex.surface.width = 4;
    outTex.surface.height = 4;
    outTex.surface.depth = 1;
    outTex.surface.mipLevels = 1;
    outTex.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    outTex.surface.aa = GX2_AA_MODE1X;
    outTex.surface.use = GX2_SURFACE_USE_TEXTURE;
    outTex.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    GX2CalcSurfaceSizeAndAlignment(&outTex.surface);
    const uint32_t surfAlign = outTex.surface.alignment > 256 ? outTex.surface.alignment : 256;
    outTex.surface.image = MemoryManager::Instance().AllocAligned(
        surfAlign,
        outTex.surface.imageSize,
        MemoryTag::Texture,
        SafeLabel(debugLabel, "SolidColorTex")
    );
    if (!outTex.surface.image) {
        return false;
    }

    const uint8_t pixelBytes[4] = {
        static_cast<uint8_t>((rgba8Color >> 24) & 0xFFu),
        static_cast<uint8_t>((rgba8Color >> 16) & 0xFFu),
        static_cast<uint8_t>((rgba8Color >> 8) & 0xFFu),
        static_cast<uint8_t>(rgba8Color & 0xFFu)
    };

    uint8_t* dst = static_cast<uint8_t*>(outTex.surface.image);
    for (uint32_t y = 0; y < outTex.surface.height; ++y) {
        uint8_t* row = dst + y * outTex.surface.pitch * 4u;
        for (uint32_t x = 0; x < outTex.surface.width; ++x) {
            std::memcpy(row + x * 4u, pixelBytes, 4u);
        }
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, outTex.surface.image, outTex.surface.imageSize);

    outTex.surface.mipmaps = nullptr;
    outTex.surface.mipmapSize = 0;
    outTex.viewFirstMip = 0;
    outTex.viewNumMips = 1;
    outTex.viewFirstSlice = 0;
    outTex.viewNumSlices = 1;
    outTex.compMap = 0x00010203;
    GX2InitTextureRegs(&outTex);
    return true;
#else
    (void)outTex; (void)rgba8Color; (void)debugLabel;
    return true;
#endif
}

bool LoadCookedCubeMapFromMemory(const uint8_t* data,
                                 size_t size,
                                 GX2Texture& outTex,
                                 const char* debugLabel) noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!data || size < 32) {
        return false;
    }

    const uint32_t magic = ReadBE32(data);
    const uint32_t faceSize = ReadBE32(data + 8);
    if (magic != WCUB_MAGIC || faceSize == 0 || faceSize > 1024) {
        return false;
    }

    const size_t faceRawBytes = static_cast<size_t>(faceSize) * static_cast<size_t>(faceSize) * 4ULL;
    const size_t totalSrcBytes = faceRawBytes * 6ULL;
    if (size < (32ULL + totalSrcBytes)) {
        return false;
    }

    DestroyTexture(outTex);
    std::memset(&outTex, 0, sizeof(GX2Texture));

    outTex.surface.dim = GX2_SURFACE_DIM_TEXTURE_CUBE;
    outTex.surface.width = faceSize;
    outTex.surface.height = faceSize;
    outTex.surface.depth = 6;
    outTex.surface.mipLevels = 1;
    outTex.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    outTex.surface.aa = GX2_AA_MODE1X;
    outTex.surface.use = GX2_SURFACE_USE_TEXTURE;
    outTex.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    GX2CalcSurfaceSizeAndAlignment(&outTex.surface);

    const uint32_t facePitchBytes = outTex.surface.pitch * 4u;
    const uint32_t faceRowBytes = faceSize * 4u;
    const uint32_t faceSliceBytes = facePitchBytes * faceSize;
    if (faceRowBytes > facePitchBytes ||
        static_cast<size_t>(faceSliceBytes) * 6u > outTex.surface.imageSize) {
        return false;
    }

    const uint32_t align = outTex.surface.alignment > 256 ? outTex.surface.alignment : 256;
    outTex.surface.image = MemoryManager::Instance().AllocAligned(
        align,
        outTex.surface.imageSize,
        MemoryTag::Texture,
        SafeLabel(debugLabel, "CookedCubeMap")
    );
    if (!outTex.surface.image) {
        return false;
    }

    const uint8_t* srcPtr = data + 32;
    uint8_t* dstPtr = static_cast<uint8_t*>(outTex.surface.image);
    std::memset(dstPtr, 0, outTex.surface.imageSize);
    const uint32_t hwSliceBytes = outTex.surface.imageSize / 6u;

    for (uint32_t face = 0; face < 6; ++face) {
        uint8_t* faceDst = dstPtr + face * hwSliceBytes;
        const uint8_t* faceSrc = srcPtr + face * faceRawBytes;
        for (uint32_t y = 0; y < faceSize; ++y) {
            std::memcpy(faceDst + y * facePitchBytes, faceSrc + y * faceRowBytes, faceRowBytes);
        }
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, outTex.surface.image, outTex.surface.imageSize);

    outTex.surface.mipmaps = nullptr;
    outTex.surface.mipmapSize = 0;
    outTex.viewFirstMip = 0;
    outTex.viewNumMips = 1;
    outTex.viewFirstSlice = 0;
    outTex.viewNumSlices = 6;
    outTex.compMap = 0x00010203;
    GX2InitTextureRegs(&outTex);
    return true;
#else
    (void)data; (void)size; (void)outTex; (void)debugLabel;
    return true;
#endif
}

bool LoadCookedCubeMapFromFile(std::string_view filepath,
                               GX2Texture& outTex,
                               const char* debugLabel) noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    std::vector<uint8_t> buffer;
    if (!AssetManager::LoadBinaryFile(std::string(filepath), buffer)) {
        return false;
    }
    return LoadCookedCubeMapFromMemory(buffer.data(), buffer.size(), outTex, debugLabel);
#else
    (void)filepath; (void)outTex; (void)debugLabel;
    return true;
#endif
}

void DestroyTexture(GX2Texture& tex) noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (tex.surface.mipmaps) {
        MemoryManager::Instance().FreeAligned(tex.surface.mipmaps);
        tex.surface.mipmaps = nullptr;
    }
    if (tex.surface.image) {
        MemoryManager::Instance().FreeAligned(tex.surface.image);
        tex.surface.image = nullptr;
    }
    std::memset(&tex, 0, sizeof(GX2Texture));
#else
    (void)tex;
#endif
}

} // namespace WiiU::Graphics::TextureLoaderGX2
