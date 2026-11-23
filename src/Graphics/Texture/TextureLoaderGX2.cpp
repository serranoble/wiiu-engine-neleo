#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include <cmath>
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

[[nodiscard]] inline float Smoothstep(float edge0, float edge1, float x) noexcept {
    const float denom = edge1 - edge0;
    if (denom <= 0.0f) return (x >= edge1) ? 1.0f : 0.0f;
    float t = (x - edge0) / denom;
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] inline uint8_t FloatToU8(float c) noexcept {
    if (c <= 0.0f) return 0u;
    if (c >= 1.0f) return 255u;
    return static_cast<uint8_t>(c * 255.0f + 0.5f);
}

inline void EvaluateProceduralSkyPixel(float dx,
                                       float dy,
                                       float dz,
                                       float sunX,
                                       float sunY,
                                       float sunZ,
                                       uint8_t* outRGBA) noexcept {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    const float sunDot = dx * sunX + dy * sunY + dz * sunZ;

    if (dy >= 0.0f) {
        // Gradiente cielo cenit azul profundo -> horizonte luminoso
        const float t = std::pow(dy, 0.55f);
        r = 0.68f * (1.0f - t) + 0.11f * t;
        g = 0.81f * (1.0f - t) + 0.35f * t;
        b = 0.92f * (1.0f - t) + 0.82f * t;

        // Nubes suaves multicapa (3 octavas continuas en bóveda + envolvente smoothstep sin cortes duros)
        const float elevMask = Smoothstep(0.04f, 0.22f, dy) * (1.0f - Smoothstep(0.40f, 0.78f, dy));
        if (elevMask > 0.0001f) {
            const float invProj = 1.0f / (dy + 0.35f);
            const float cx = dx * invProj;
            const float cz = dz * invProj;

            const float o1 = 0.5f + 0.5f * std::sin(cx * 1.45f + 0.4f) * std::cos(cz * 1.35f - 0.7f);
            const float o2 = 0.5f + 0.5f * std::sin(cx * 2.95f - cz * 1.85f + 1.3f) * std::cos(cz * 2.70f + cx * 1.15f);
            const float o3 = 0.5f + 0.5f * std::sin(cx * 5.60f + cz * 4.10f - 2.1f) * std::cos(cz * 5.20f - cx * 3.40f);
            const float fbm = o1 * 0.54f + o2 * 0.31f + o3 * 0.15f;

            const float cloudMask = Smoothstep(0.46f, 0.78f, fbm) * elevMask * 0.62f;
            const float sunRim = Smoothstep(0.25f, 0.96f, sunDot);
            const float cr = 0.93f + 0.07f * sunRim;
            const float cg = 0.94f + 0.05f * sunRim;
            const float cb = 0.97f + 0.01f * sunRim;

            r = r * (1.0f - cloudMask) + cr * cloudMask;
            g = g * (1.0f - cloudMask) + cg * cloudMask;
            b = b * (1.0f - cloudMask) + cb * cloudMask;
        }
    } else {
        // Hemisferio inferior (reflejo marino/horizonte)
        const float t = std::pow(-dy, 0.45f);
        r = 0.58f * (1.0f - t) + 0.05f * t;
        g = 0.70f * (1.0f - t) + 0.16f * t;
        b = 0.80f * (1.0f - t) + 0.28f * t;
    }

    // Halo solar suave + disco solar con borde suavizado (cero escalonado)
    if (sunDot > 0.86f) {
        const float halo = std::pow((sunDot - 0.86f) / 0.14f, 3.2f);
        r += 0.95f * halo;
        g += 0.78f * halo;
        b += 0.44f * halo;
    }
    const float sunDisc = Smoothstep(0.9945f, 0.9975f, sunDot);
    if (sunDisc > 0.0f) {
        r = r * (1.0f - sunDisc) + 1.00f * sunDisc;
        g = g * (1.0f - sunDisc) + 0.99f * sunDisc;
        b = b * (1.0f - sunDisc) + 0.92f * sunDisc;
    }

    outRGBA[0] = FloatToU8(r);
    outRGBA[1] = FloatToU8(g);
    outRGBA[2] = FloatToU8(b);
    outRGBA[3] = 255u;
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

bool CreateProceduralMipmapCheckerTexture(GX2Texture& outTex,
                                          uint32_t size,
                                          uint32_t checkerCells,
                                          bool colorCodeMips,
                                          const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    // Validar potencia de 2 en [16..1024]
    if (size < 16u || size > 1024u || (size & (size - 1u)) != 0u) {
        return false;
    }
    const uint32_t cells = (checkerCells == 0u) ? 8u : checkerCells;

    uint32_t mipLevels = 1u;
    for (uint32_t dim = size; dim > 1u && mipLevels < 13u; dim >>= 1u) {
        ++mipLevels;
    }

    DestroyTexture(outTex);
    std::memset(&outTex, 0, sizeof(GX2Texture));

    outTex.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    outTex.surface.width = size;
    outTex.surface.height = size;
    outTex.surface.depth = 1u;
    outTex.surface.mipLevels = mipLevels;
    outTex.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    outTex.surface.aa = GX2_AA_MODE1X;
    outTex.surface.use = GX2_SURFACE_USE_TEXTURE;
    outTex.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    GX2CalcSurfaceSizeAndAlignment(&outTex.surface);
    const uint32_t surfAlign = (outTex.surface.alignment > 256u) ? outTex.surface.alignment : 256u;

    outTex.surface.image = MemoryManager::Instance().AllocAligned(
        surfAlign,
        outTex.surface.imageSize,
        MemoryTag::Texture,
        SafeLabel(debugLabel, "MipCheckerBase")
    );
    if (!outTex.surface.image) {
        return false;
    }
    std::memset(outTex.surface.image, 0, outTex.surface.imageSize);

    if (outTex.surface.mipmapSize > 0u) {
        outTex.surface.mipmaps = MemoryManager::Instance().AllocAligned(
            surfAlign,
            outTex.surface.mipmapSize,
            MemoryTag::Texture,
            SafeLabel(debugLabel, "MipCheckerMips")
        );
        if (!outTex.surface.mipmaps) {
            DestroyTexture(outTex);
            return false;
        }
        std::memset(outTex.surface.mipmaps, 0, outTex.surface.mipmapSize);
    }

    // Paleta de inspeccion visual por nivel de mipmap (R, G, B)
    static constexpr uint8_t kMipPalette[8][3] = {
        {245, 245, 245}, // Mip 0: Blanco (maximo detalle)
        { 40, 230, 245}, // Mip 1: Cian
        { 50, 225,  95}, // Mip 2: Verde
        {250, 225,  45}, // Mip 3: Amarillo
        {250, 140,  35}, // Mip 4: Naranja
        {240,  55,  70}, // Mip 5: Rojo
        {200,  70, 235}, // Mip 6: Magenta
        { 90, 120, 250}  // Mip 7+: Azul
    };

    // Generar nivel 0 en buffer lineal RGBA8
    std::vector<uint8_t> prevLevel(static_cast<size_t>(size) * static_cast<size_t>(size) * 4u, 255u);
    const uint32_t cellPixels = (size >= cells) ? (size / cells) : 1u;
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const uint32_t cx = x / cellPixels;
            const uint32_t cy = y / cellPixels;
            const bool isLight = ((cx + cy) & 1u) == 0u;
            const bool isBorder = (x % cellPixels == 0u) || (y % cellPixels == 0u);
            uint8_t r = isLight ? 235u : 28u;
            uint8_t g = isLight ? 238u : 34u;
            uint8_t b = isLight ? 245u : 46u;
            if (colorCodeMips) {
                r = isLight ? kMipPalette[0][0] : 30u;
                g = isLight ? kMipPalette[0][1] : 34u;
                b = isLight ? kMipPalette[0][2] : 42u;
            } else if (isBorder) {
                r = 215u; g = 165u; b = 40u; // Grilla sutil para evaluar aliasing/anisotropia
            }
            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 4u;
            prevLevel[idx + 0u] = r;
            prevLevel[idx + 1u] = g;
            prevLevel[idx + 2u] = b;
            prevLevel[idx + 3u] = 255u;
        }
    }

    auto copyLevelToSurface = [&](uint32_t level, uint32_t w, uint32_t h, const std::vector<uint8_t>& pixels) -> bool {
        if (level == 0u) {
            const uint32_t pitchBytes = outTex.surface.pitch * 4u;
            const uint32_t rowBytes = w * 4u;
            if (rowBytes > pitchBytes || static_cast<size_t>(h) * static_cast<size_t>(pitchBytes) > outTex.surface.imageSize) {
                return false;
            }
            uint8_t* dst = static_cast<uint8_t*>(outTex.surface.image);
            for (uint32_t y = 0; y < h; ++y) {
                std::memcpy(dst + static_cast<size_t>(y) * pitchBytes,
                            pixels.data() + static_cast<size_t>(y) * rowBytes,
                            rowBytes);
            }
            return true;
        }

        if (!outTex.surface.mipmaps || level >= 13u) {
            return false;
        }

        // Calcular el pitch exacto de este nivel de mipmap para LINEAR_ALIGNED en GX2
        GX2Surface mipProbe{};
        mipProbe.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        mipProbe.width = w;
        mipProbe.height = h;
        mipProbe.depth = 1u;
        mipProbe.mipLevels = 1u;
        mipProbe.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        mipProbe.aa = GX2_AA_MODE1X;
        mipProbe.use = GX2_SURFACE_USE_TEXTURE;
        mipProbe.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
        GX2CalcSurfaceSizeAndAlignment(&mipProbe);

        const uint32_t mipOffset = (level == 1u) ? 0u : outTex.surface.mipLevelOffset[level - 1u];
        const uint32_t pitchBytes = mipProbe.pitch * 4u;
        const uint32_t rowBytes = w * 4u;
        if (rowBytes > pitchBytes ||
            static_cast<size_t>(mipOffset) + static_cast<size_t>(h) * static_cast<size_t>(pitchBytes) > outTex.surface.mipmapSize) {
            return false;
        }

        uint8_t* dst = static_cast<uint8_t*>(outTex.surface.mipmaps) + mipOffset;
        for (uint32_t y = 0; y < h; ++y) {
            std::memcpy(dst + static_cast<size_t>(y) * pitchBytes,
                        pixels.data() + static_cast<size_t>(y) * rowBytes,
                        rowBytes);
        }
        return true;
    };

    if (!copyLevelToSurface(0u, size, size, prevLevel)) {
        DestroyTexture(outTex);
        return false;
    }

    uint32_t curW = size;
    uint32_t curH = size;
    for (uint32_t level = 1u; level < mipLevels; ++level) {
        const uint32_t nextW = (curW > 1u) ? (curW >> 1u) : 1u;
        const uint32_t nextH = (curH > 1u) ? (curH >> 1u) : 1u;
        std::vector<uint8_t> nextLevel(static_cast<size_t>(nextW) * static_cast<size_t>(nextH) * 4u, 255u);

        const uint32_t palIdx = (level < 8u) ? level : 7u;
        for (uint32_t y = 0; y < nextH; ++y) {
            for (uint32_t x = 0; x < nextW; ++x) {
                const size_t dstIdx = (static_cast<size_t>(y) * static_cast<size_t>(nextW) + static_cast<size_t>(x)) * 4u;
                if (colorCodeMips) {
                    const uint32_t subCells = (nextW >= 4u) ? (nextW / 4u) : 1u;
                    const bool isLight = (((x / subCells) + (y / subCells)) & 1u) == 0u;
                    nextLevel[dstIdx + 0u] = isLight ? kMipPalette[palIdx][0] : static_cast<uint8_t>(kMipPalette[palIdx][0] / 3u);
                    nextLevel[dstIdx + 1u] = isLight ? kMipPalette[palIdx][1] : static_cast<uint8_t>(kMipPalette[palIdx][1] / 3u);
                    nextLevel[dstIdx + 2u] = isLight ? kMipPalette[palIdx][2] : static_cast<uint8_t>(kMipPalette[palIdx][2] / 3u);
                    nextLevel[dstIdx + 3u] = 255u;
                } else {
                    // Box filter 2x2 estandar (RTR4 Cap. 6.2.2)
                    const uint32_t sx0 = x * 2u;
                    const uint32_t sy0 = y * 2u;
                    const uint32_t sx1 = (sx0 + 1u < curW) ? (sx0 + 1u) : sx0;
                    const uint32_t sy1 = (sy0 + 1u < curH) ? (sy0 + 1u) : sy0;
                    const size_t i00 = (static_cast<size_t>(sy0) * curW + sx0) * 4u;
                    const size_t i10 = (static_cast<size_t>(sy0) * curW + sx1) * 4u;
                    const size_t i01 = (static_cast<size_t>(sy1) * curW + sx0) * 4u;
                    const size_t i11 = (static_cast<size_t>(sy1) * curW + sx1) * 4u;
                    for (uint32_t c = 0; c < 4u; ++c) {
                        const uint32_t sum = static_cast<uint32_t>(prevLevel[i00 + c]) +
                                             static_cast<uint32_t>(prevLevel[i10 + c]) +
                                             static_cast<uint32_t>(prevLevel[i01 + c]) +
                                             static_cast<uint32_t>(prevLevel[i11 + c]);
                        nextLevel[dstIdx + c] = static_cast<uint8_t>((sum + 2u) >> 2u);
                    }
                }
            }
        }

        if (!copyLevelToSurface(level, nextW, nextH, nextLevel)) {
            DestroyTexture(outTex);
            return false;
        }
        prevLevel = std::move(nextLevel);
        curW = nextW;
        curH = nextH;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, outTex.surface.image, outTex.surface.imageSize);
    if (outTex.surface.mipmaps && outTex.surface.mipmapSize > 0u) {
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, outTex.surface.mipmaps, outTex.surface.mipmapSize);
    }

    outTex.viewFirstMip = 0u;
    outTex.viewNumMips = mipLevels;
    outTex.viewFirstSlice = 0u;
    outTex.viewNumSlices = 1u;
    outTex.compMap = 0x00010203u;
    GX2InitTextureRegs(&outTex);
    return true;
#else
    (void)outTex; (void)size; (void)checkerCells; (void)colorCodeMips; (void)debugLabel;
    return true;
#endif
}

bool CreateProceduralReliefTexture(GX2Texture& outTex,
                                   uint32_t size,
                                   const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (size < 16u || size > 1024u || (size & (size - 1u)) != 0u) {
        return false;
    }

    uint32_t mipLevels = 1u;
    for (uint32_t dim = size; dim > 1u && mipLevels < 13u; dim >>= 1u) {
        ++mipLevels;
    }

    DestroyTexture(outTex);
    std::memset(&outTex, 0, sizeof(GX2Texture));

    outTex.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    outTex.surface.width = size;
    outTex.surface.height = size;
    outTex.surface.depth = 1u;
    outTex.surface.mipLevels = mipLevels;
    outTex.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    outTex.surface.aa = GX2_AA_MODE1X;
    outTex.surface.use = GX2_SURFACE_USE_TEXTURE;
    outTex.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    GX2CalcSurfaceSizeAndAlignment(&outTex.surface);
    const uint32_t surfAlign = (outTex.surface.alignment > 256u) ? outTex.surface.alignment : 256u;

    outTex.surface.image = MemoryManager::Instance().AllocAligned(
        surfAlign, outTex.surface.imageSize, MemoryTag::Texture, SafeLabel(debugLabel, "ReliefBase"));
    if (!outTex.surface.image) {
        return false;
    }
    std::memset(outTex.surface.image, 0, outTex.surface.imageSize);

    if (outTex.surface.mipmapSize > 0u) {
        outTex.surface.mipmaps = MemoryManager::Instance().AllocAligned(
            surfAlign, outTex.surface.mipmapSize, MemoryTag::Texture, SafeLabel(debugLabel, "ReliefMips"));
        if (!outTex.surface.mipmaps) {
            DestroyTexture(outTex);
            return false;
        }
        std::memset(outTex.surface.mipmaps, 0, outTex.surface.mipmapSize);
    }

    // Nivel 0: ladrillos biselados con juntas de mortero hundidas para alto contraste en gradiente de luminancia TBN
    std::vector<uint8_t> prevLevel(static_cast<size_t>(size) * static_cast<size_t>(size) * 4u, 255u);
    const uint32_t rows = 8u;
    const uint32_t cols = 4u;
    const float fSize = static_cast<float>(size);

    for (uint32_t y = 0; y < size; ++y) {
        const float fy = (static_cast<float>(y) / fSize) * static_cast<float>(rows);
        const uint32_t rowIdx = static_cast<uint32_t>(fy);
        const float ly = fy - static_cast<float>(rowIdx);
        const float rowOffset = ((rowIdx & 1u) != 0u) ? 0.5f : 0.0f;

        for (uint32_t x = 0; x < size; ++x) {
            const float fx = (static_cast<float>(x) / fSize) * static_cast<float>(cols) + rowOffset;
            const uint32_t colIdx = static_cast<uint32_t>(fx);
            const float lx = fx - static_cast<float>(colIdx);

            // Distancia al borde de la junta (mortero hundido + bisel suave)
            const float distX = (lx < 0.5f) ? lx : (1.0f - lx);
            const float distY = (ly < 0.5f) ? ly : (1.0f - ly);
            const float bevelX = (distX < 0.06f) ? 0.0f : ((distX < 0.14f) ? ((distX - 0.06f) / 0.08f) : 1.0f);
            const float bevelY = (distY < 0.08f) ? 0.0f : ((distY < 0.18f) ? ((distY - 0.08f) / 0.10f) : 1.0f);
            const float height = bevelX * bevelY;

            // Variación tonal determinista por ladrillo
            const uint32_t brickHash = ((rowIdx * 73856093u) ^ (colIdx * 19349663u)) & 0x1Fu;
            const float shade = 0.85f + (static_cast<float>(brickHash) / 31.0f) * 0.25f;

            const float baseR = 52.0f + height * 165.0f * shade;
            const float baseG = 48.0f + height * 118.0f * shade;
            const float baseB = 46.0f + height * 92.0f * shade;

            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 4u;
            prevLevel[idx + 0u] = static_cast<uint8_t>(baseR > 255.0f ? 255.0f : baseR);
            prevLevel[idx + 1u] = static_cast<uint8_t>(baseG > 255.0f ? 255.0f : baseG);
            prevLevel[idx + 2u] = static_cast<uint8_t>(baseB > 255.0f ? 255.0f : baseB);
            prevLevel[idx + 3u] = 255u;
        }
    }

    auto copyLevel = [&](uint32_t level, uint32_t w, uint32_t h, const std::vector<uint8_t>& pixels) -> bool {
        if (level == 0u) {
            const uint32_t pitchBytes = outTex.surface.pitch * 4u;
            const uint32_t rowBytes = w * 4u;
            if (rowBytes > pitchBytes || static_cast<size_t>(h) * static_cast<size_t>(pitchBytes) > outTex.surface.imageSize) {
                return false;
            }
            uint8_t* dst = static_cast<uint8_t*>(outTex.surface.image);
            for (uint32_t y = 0; y < h; ++y) {
                std::memcpy(dst + static_cast<size_t>(y) * pitchBytes,
                            pixels.data() + static_cast<size_t>(y) * rowBytes,
                            rowBytes);
            }
            return true;
        }
        if (!outTex.surface.mipmaps || level >= 13u) return false;

        GX2Surface mipProbe{};
        mipProbe.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        mipProbe.width = w;
        mipProbe.height = h;
        mipProbe.depth = 1u;
        mipProbe.mipLevels = 1u;
        mipProbe.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        mipProbe.aa = GX2_AA_MODE1X;
        mipProbe.use = GX2_SURFACE_USE_TEXTURE;
        mipProbe.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
        GX2CalcSurfaceSizeAndAlignment(&mipProbe);

        const uint32_t mipOffset = (level == 1u) ? 0u : outTex.surface.mipLevelOffset[level - 1u];
        const uint32_t pitchBytes = mipProbe.pitch * 4u;
        const uint32_t rowBytes = w * 4u;
        if (rowBytes > pitchBytes ||
            static_cast<size_t>(mipOffset) + static_cast<size_t>(h) * static_cast<size_t>(pitchBytes) > outTex.surface.mipmapSize) {
            return false;
        }
        uint8_t* dst = static_cast<uint8_t*>(outTex.surface.mipmaps) + mipOffset;
        for (uint32_t y = 0; y < h; ++y) {
            std::memcpy(dst + static_cast<size_t>(y) * pitchBytes,
                        pixels.data() + static_cast<size_t>(y) * rowBytes,
                        rowBytes);
        }
        return true;
    };

    if (!copyLevel(0u, size, size, prevLevel)) {
        DestroyTexture(outTex);
        return false;
    }

    uint32_t curW = size;
    uint32_t curH = size;
    for (uint32_t level = 1u; level < mipLevels; ++level) {
        const uint32_t nextW = (curW > 1u) ? (curW >> 1u) : 1u;
        const uint32_t nextH = (curH > 1u) ? (curH >> 1u) : 1u;
        std::vector<uint8_t> nextLevel(static_cast<size_t>(nextW) * static_cast<size_t>(nextH) * 4u, 255u);
        for (uint32_t y = 0; y < nextH; ++y) {
            for (uint32_t x = 0; x < nextW; ++x) {
                const uint32_t sx0 = x * 2u;
                const uint32_t sy0 = y * 2u;
                const uint32_t sx1 = (sx0 + 1u < curW) ? (sx0 + 1u) : sx0;
                const uint32_t sy1 = (sy0 + 1u < curH) ? (sy0 + 1u) : sy0;
                const size_t i00 = (static_cast<size_t>(sy0) * curW + sx0) * 4u;
                const size_t i10 = (static_cast<size_t>(sy0) * curW + sx1) * 4u;
                const size_t i01 = (static_cast<size_t>(sy1) * curW + sx0) * 4u;
                const size_t i11 = (static_cast<size_t>(sy1) * curW + sx1) * 4u;
                const size_t dstIdx = (static_cast<size_t>(y) * nextW + x) * 4u;
                for (uint32_t c = 0; c < 4u; ++c) {
                    const uint32_t sum = static_cast<uint32_t>(prevLevel[i00 + c]) +
                                         static_cast<uint32_t>(prevLevel[i10 + c]) +
                                         static_cast<uint32_t>(prevLevel[i01 + c]) +
                                         static_cast<uint32_t>(prevLevel[i11 + c]);
                    nextLevel[dstIdx + c] = static_cast<uint8_t>((sum + 2u) >> 2u);
                }
            }
        }
        if (!copyLevel(level, nextW, nextH, nextLevel)) {
            DestroyTexture(outTex);
            return false;
        }
        prevLevel = std::move(nextLevel);
        curW = nextW;
        curH = nextH;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, outTex.surface.image, outTex.surface.imageSize);
    if (outTex.surface.mipmaps && outTex.surface.mipmapSize > 0u) {
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, outTex.surface.mipmaps, outTex.surface.mipmapSize);
    }

    outTex.viewFirstMip = 0u;
    outTex.viewNumMips = mipLevels;
    outTex.viewFirstSlice = 0u;
    outTex.viewNumSlices = 1u;
    outTex.compMap = 0x00010203u;
    GX2InitTextureRegs(&outTex);
    return true;
#else
    (void)outTex; (void)size; (void)debugLabel;
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

bool CreateProceduralSkyCubeMap(GX2Texture& outTex,
                                uint32_t faceSize,
                                const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (faceSize < 16u || faceSize > 512u || (faceSize & (faceSize - 1u)) != 0u) {
        return false;
    }

    DestroyTexture(outTex);
    std::memset(&outTex, 0, sizeof(GX2Texture));

    outTex.surface.dim = GX2_SURFACE_DIM_TEXTURE_CUBE;
    outTex.surface.width = faceSize;
    outTex.surface.height = faceSize;
    outTex.surface.depth = 6u;
    outTex.surface.mipLevels = 1u;
    outTex.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    outTex.surface.aa = GX2_AA_MODE1X;
    outTex.surface.use = GX2_SURFACE_USE_TEXTURE;
    outTex.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    GX2CalcSurfaceSizeAndAlignment(&outTex.surface);
    const uint32_t surfAlign = (outTex.surface.alignment > 256u) ? outTex.surface.alignment : 256u;
    const uint32_t pitchBytes = outTex.surface.pitch * 4u;
    const uint32_t rowBytes = faceSize * 4u;
    const uint32_t hwSliceBytes = outTex.surface.imageSize / 6u;
    if (rowBytes > pitchBytes || static_cast<size_t>(faceSize) * static_cast<size_t>(pitchBytes) > hwSliceBytes) {
        return false;
    }

    outTex.surface.image = MemoryManager::Instance().AllocAligned(
        surfAlign, outTex.surface.imageSize, MemoryTag::Texture, SafeLabel(debugLabel, "ProcSkyCube"));
    if (!outTex.surface.image) {
        return false;
    }
    std::memset(outTex.surface.image, 0, outTex.surface.imageSize);

    // Dirección del sol normalizada
    const float sunLen = std::sqrt(0.45f * 0.45f + 0.52f * 0.52f + 0.72f * 0.72f);
    const float sunX = 0.45f / sunLen;
    const float sunY = 0.52f / sunLen;
    const float sunZ = -0.72f / sunLen;

    uint8_t* dstBase = static_cast<uint8_t*>(outTex.surface.image);
    const float invSize = 2.0f / static_cast<float>(faceSize);

    for (uint32_t face = 0u; face < 6u; ++face) {
        uint8_t* faceDst = dstBase + static_cast<size_t>(face) * hwSliceBytes;
        for (uint32_t y = 0u; y < faceSize; ++y) {
            const float v = (static_cast<float>(y) + 0.5f) * invSize - 1.0f;
            uint8_t* rowPtr = faceDst + static_cast<size_t>(y) * pitchBytes;
            for (uint32_t x = 0u; x < faceSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) * invSize - 1.0f;
                float dx = 0.0f, dy = 0.0f, dz = 0.0f;
                switch (face) {
                    case 0u: dx =  1.0f; dy = -v;    dz = -u;    break; // +X
                    case 1u: dx = -1.0f; dy = -v;    dz =  u;    break; // -X
                    case 2u: dx =  u;    dy =  1.0f; dz =  v;    break; // +Y
                    case 3u: dx =  u;    dy = -1.0f; dz = -v;    break; // -Y
                    case 4u: dx =  u;    dy = -v;    dz =  1.0f; break; // +Z
                    case 5u: dx = -u;    dy = -v;    dz = -1.0f; break; // -Z
                }
                const float invLen = 1.0f / std::sqrt(dx * dx + dy * dy + dz * dz);
                dx *= invLen; dy *= invLen; dz *= invLen;

                EvaluateProceduralSkyPixel(dx, dy, dz, sunX, sunY, sunZ, &rowPtr[x * 4u]);
            }
        }
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, outTex.surface.image, outTex.surface.imageSize);
    outTex.surface.mipmaps = nullptr;
    outTex.surface.mipmapSize = 0u;
    outTex.viewFirstMip = 0u;
    outTex.viewNumMips = 1u;
    outTex.viewFirstSlice = 0u;
    outTex.viewNumSlices = 6u;
    outTex.compMap = 0x00010203u;
    GX2InitTextureRegs(&outTex);
    return true;
#else
    (void)outTex; (void)faceSize; (void)debugLabel;
    return true;
#endif
}

bool CreateProceduralSkyCubeAtlas(GX2Texture& outTex,
                                  uint32_t faceSize,
                                  const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (faceSize < 16u || faceSize > 512u || (faceSize & (faceSize - 1u)) != 0u) {
        return false;
    }

    DestroyTexture(outTex);
    std::memset(&outTex, 0, sizeof(GX2Texture));

    const uint32_t atlasW = faceSize * 3u;
    const uint32_t atlasH = faceSize * 2u;

    outTex.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    outTex.surface.width = atlasW;
    outTex.surface.height = atlasH;
    outTex.surface.depth = 1u;
    outTex.surface.mipLevels = 1u;
    outTex.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    outTex.surface.aa = GX2_AA_MODE1X;
    outTex.surface.use = GX2_SURFACE_USE_TEXTURE;
    outTex.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    GX2CalcSurfaceSizeAndAlignment(&outTex.surface);
    const uint32_t surfAlign = (outTex.surface.alignment > 256u) ? outTex.surface.alignment : 256u;
    const uint32_t pitchBytes = outTex.surface.pitch * 4u;
    const uint32_t rowBytes = atlasW * 4u;
    if (rowBytes > pitchBytes || static_cast<size_t>(atlasH) * static_cast<size_t>(pitchBytes) > outTex.surface.imageSize) {
        return false;
    }

    outTex.surface.image = MemoryManager::Instance().AllocAligned(
        surfAlign, outTex.surface.imageSize, MemoryTag::Texture, SafeLabel(debugLabel, "ProcSkyAtlas"));
    if (!outTex.surface.image) {
        return false;
    }
    std::memset(outTex.surface.image, 0, outTex.surface.imageSize);

    const float sunLen = std::sqrt(0.45f * 0.45f + 0.52f * 0.52f + 0.72f * 0.72f);
    const float sunX = 0.45f / sunLen;
    const float sunY = 0.52f / sunLen;
    const float sunZ = -0.72f / sunLen;

    uint8_t* dstBase = static_cast<uint8_t*>(outTex.surface.image);
    const float invSize = 2.0f / static_cast<float>(faceSize);

    for (uint32_t face = 0u; face < 6u; ++face) {
        const uint32_t cellCol = face % 3u;
        const uint32_t cellRow = face / 3u;
        const uint32_t baseX = cellCol * faceSize;
        const uint32_t baseY = cellRow * faceSize;

        for (uint32_t y = 0u; y < faceSize; ++y) {
            const float v = (static_cast<float>(y) + 0.5f) * invSize - 1.0f;
            uint8_t* rowPtr = dstBase + static_cast<size_t>(baseY + y) * pitchBytes + static_cast<size_t>(baseX) * 4u;
            for (uint32_t x = 0u; x < faceSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) * invSize - 1.0f;
                float dx = 0.0f, dy = 0.0f, dz = 0.0f;
                switch (face) {
                    case 0u: dx =  1.0f; dy = -v;    dz = -u;    break; // +X
                    case 1u: dx = -1.0f; dy = -v;    dz =  u;    break; // -X
                    case 2u: dx =  u;    dy =  1.0f; dz =  v;    break; // +Y
                    case 3u: dx =  u;    dy = -1.0f; dz = -v;    break; // -Y
                    case 4u: dx =  u;    dy = -v;    dz =  1.0f; break; // +Z
                    case 5u: dx = -u;    dy = -v;    dz = -1.0f; break; // -Z
                }
                const float invLen = 1.0f / std::sqrt(dx * dx + dy * dy + dz * dz);
                dx *= invLen; dy *= invLen; dz *= invLen;

                EvaluateProceduralSkyPixel(dx, dy, dz, sunX, sunY, sunZ, &rowPtr[x * 4u]);
            }
        }
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, outTex.surface.image, outTex.surface.imageSize);
    outTex.surface.mipmaps = nullptr;
    outTex.surface.mipmapSize = 0u;
    outTex.viewFirstMip = 0u;
    outTex.viewNumMips = 1u;
    outTex.viewFirstSlice = 0u;
    outTex.viewNumSlices = 1u;
    outTex.compMap = 0x00010203u;
    GX2InitTextureRegs(&outTex);
    return true;
#else
    (void)outTex; (void)faceSize; (void)debugLabel;
    return true;
#endif
}

bool LoadCookedCubeMapAsAtlasFromMemory(const uint8_t* data,
                                        size_t size,
                                        GX2Texture& outTex,
                                        const char* debugLabel) noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!data || size < 32u) {
        return false;
    }

    const uint32_t magic = ReadBE32(data);
    const uint32_t faceSize = ReadBE32(data + 8u);
    if (magic != WCUB_MAGIC || faceSize == 0u || faceSize > 1024u) {
        return false;
    }

    const size_t faceRowBytes = static_cast<size_t>(faceSize) * 4u;
    const size_t faceRawBytes = faceRowBytes * static_cast<size_t>(faceSize);
    if (size < (32u + faceRawBytes * 6u)) {
        return false;
    }

    DestroyTexture(outTex);
    std::memset(&outTex, 0, sizeof(GX2Texture));

    const uint32_t atlasW = faceSize * 3u;
    const uint32_t atlasH = faceSize * 2u;

    outTex.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    outTex.surface.width = atlasW;
    outTex.surface.height = atlasH;
    outTex.surface.depth = 1u;
    outTex.surface.mipLevels = 1u;
    outTex.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    outTex.surface.aa = GX2_AA_MODE1X;
    outTex.surface.use = GX2_SURFACE_USE_TEXTURE;
    outTex.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    GX2CalcSurfaceSizeAndAlignment(&outTex.surface);
    const uint32_t pitchBytes = outTex.surface.pitch * 4u;
    if (atlasW * 4u > pitchBytes || static_cast<size_t>(atlasH) * static_cast<size_t>(pitchBytes) > outTex.surface.imageSize) {
        return false;
    }

    const uint32_t align = (outTex.surface.alignment > 256u) ? outTex.surface.alignment : 256u;
    outTex.surface.image = MemoryManager::Instance().AllocAligned(
        align, outTex.surface.imageSize, MemoryTag::Texture, SafeLabel(debugLabel, "CookedCubeAtlas"));
    if (!outTex.surface.image) {
        return false;
    }

    uint8_t* dstBase = static_cast<uint8_t*>(outTex.surface.image);
    std::memset(dstBase, 0, outTex.surface.imageSize);
    const uint8_t* srcBase = data + 32u;

    for (uint32_t face = 0u; face < 6u; ++face) {
        const uint32_t cellCol = face % 3u;
        const uint32_t cellRow = face / 3u;
        const uint32_t baseX = cellCol * faceSize;
        const uint32_t baseY = cellRow * faceSize;
        const uint8_t* faceSrc = srcBase + static_cast<size_t>(face) * faceRawBytes;
        for (uint32_t y = 0u; y < faceSize; ++y) {
            uint8_t* rowDst = dstBase + static_cast<size_t>(baseY + y) * pitchBytes + static_cast<size_t>(baseX) * 4u;
            std::memcpy(rowDst, faceSrc + static_cast<size_t>(y) * faceRowBytes, faceRowBytes);
        }
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, outTex.surface.image, outTex.surface.imageSize);
    outTex.surface.mipmaps = nullptr;
    outTex.surface.mipmapSize = 0u;
    outTex.viewFirstMip = 0u;
    outTex.viewNumMips = 1u;
    outTex.viewFirstSlice = 0u;
    outTex.viewNumSlices = 1u;
    outTex.compMap = 0x00010203u;
    GX2InitTextureRegs(&outTex);
    return true;
#else
    (void)data; (void)size; (void)outTex; (void)debugLabel;
    return true;
#endif
}

bool LoadCookedCubeMapAsAtlasFromFile(std::string_view filepath,
                                      GX2Texture& outTex,
                                      const char* debugLabel) noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    std::vector<uint8_t> buffer;
    if (!AssetManager::LoadBinaryFile(std::string(filepath), buffer)) {
        return false;
    }
    return LoadCookedCubeMapAsAtlasFromMemory(buffer.data(), buffer.size(), outTex, debugLabel);
#else
    (void)filepath; (void)outTex; (void)debugLabel;
    return true;
#endif
}

bool CreateProceduralAlphaCutoutTexture(GX2Texture& outTex,
                                        uint32_t size,
                                        const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (size < 32u || size > 512u || (size & (size - 1u)) != 0u) {
        return false;
    }

    DestroyTexture(outTex);
    std::memset(&outTex, 0, sizeof(GX2Texture));

    outTex.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    outTex.surface.width = size;
    outTex.surface.height = size;
    outTex.surface.depth = 1u;
    outTex.surface.mipLevels = 1u;
    outTex.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    outTex.surface.aa = GX2_AA_MODE1X;
    outTex.surface.use = GX2_SURFACE_USE_TEXTURE;
    outTex.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    GX2CalcSurfaceSizeAndAlignment(&outTex.surface);
    const uint32_t pitchBytes = outTex.surface.pitch * 4u;
    if (size * 4u > pitchBytes || static_cast<size_t>(size) * static_cast<size_t>(pitchBytes) > outTex.surface.imageSize) {
        return false;
    }

    const uint32_t align = (outTex.surface.alignment > 256u) ? outTex.surface.alignment : 256u;
    outTex.surface.image = MemoryManager::Instance().AllocAligned(
        align, outTex.surface.imageSize, MemoryTag::Texture, SafeLabel(debugLabel, "AlphaCutoutTex"));
    if (!outTex.surface.image) {
        return false;
    }

    uint8_t* dstBase = static_cast<uint8_t*>(outTex.surface.image);
    std::memset(dstBase, 0, outTex.surface.imageSize);
    const float invSize = 1.0f / static_cast<float>(size);

    for (uint32_t y = 0u; y < size; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) * invSize;
        uint8_t* rowPtr = dstBase + static_cast<size_t>(y) * pitchBytes;
        for (uint32_t x = 0u; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) * invSize;

            // Marco metálico + celosía diagonal + hojas/pétalos centrales con alpha binario 0/255
            const bool border = (u < 0.08f || u > 0.92f || v < 0.08f || v > 0.92f);
            const float gx = std::fabs(std::fmod(u * 4.0f, 1.0f) - 0.5f);
            const float gy = std::fabs(std::fmod(v * 4.0f, 1.0f) - 0.5f);
            const bool lattice = (gx > 0.38f || gy > 0.38f);

            const float cx = u - 0.5f;
            const float cy = v - 0.5f;
            const float r = std::sqrt(cx * cx + cy * cy);
            const float angle = std::atan2(cy, cx);
            const float leafRadius = 0.24f + 0.08f * std::cos(angle * 6.0f);
            const bool foliage = (r < leafRadius) && (r > 0.06f);

            if (border || lattice) {
                // Celosía verde-bronce oscura
                rowPtr[x * 4u + 0u] = 48u;
                rowPtr[x * 4u + 1u] = 84u;
                rowPtr[x * 4u + 2u] = 68u;
                rowPtr[x * 4u + 3u] = 255u;
            } else if (foliage) {
                // Follaje esmeralda luminoso
                const float shade = 0.72f + 0.28f * (1.0f - r / leafRadius);
                rowPtr[x * 4u + 0u] = FloatToU8(0.18f * shade);
                rowPtr[x * 4u + 1u] = FloatToU8(0.76f * shade);
                rowPtr[x * 4u + 2u] = FloatToU8(0.32f * shade);
                rowPtr[x * 4u + 3u] = 255u;
            } else {
                // Hueco completamente transparente (dispara discard en Alpha-Cutout)
                rowPtr[x * 4u + 0u] = 0u;
                rowPtr[x * 4u + 1u] = 0u;
                rowPtr[x * 4u + 2u] = 0u;
                rowPtr[x * 4u + 3u] = 0u;
            }
        }
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, outTex.surface.image, outTex.surface.imageSize);
    outTex.surface.mipmaps = nullptr;
    outTex.surface.mipmapSize = 0u;
    outTex.viewFirstMip = 0u;
    outTex.viewNumMips = 1u;
    outTex.viewFirstSlice = 0u;
    outTex.viewNumSlices = 1u;
    outTex.compMap = 0x00010203u;
    GX2InitTextureRegs(&outTex);
    return true;
#else
    (void)outTex; (void)size; (void)debugLabel;
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
