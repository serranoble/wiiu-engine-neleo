#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include "Graphics/GL/GLFunctions.hpp"
#include "Core/AssetManager.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace Neleo::Graphics::TextureLoaderGX2 {

namespace {

constexpr uint32_t WTEX_MAGIC = 0x57544558u; // 'WTEX'
constexpr uint32_t WCUB_MAGIC = 0x57435542u; // 'WCUB'

[[nodiscard]] inline uint32_t ReadBE32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
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

[[nodiscard]] inline uint32_t CalcMipCount(uint32_t w, uint32_t h) noexcept {
    uint32_t levels = 1u;
    while ((w > 1u || h > 1u) && levels < 13u) {
        w = (w > 1u) ? (w >> 1u) : 1u;
        h = (h > 1u) ? (h >> 1u) : 1u;
        ++levels;
    }
    return levels;
}

bool UploadRGBA8ToGL(GX2Texture& outTex,
                     const uint8_t* rgba,
                     uint32_t width,
                     uint32_t height,
                     bool generateMips,
                     bool isCubeAtlas) noexcept {
    if (!rgba || width == 0u || height == 0u) {
        return false;
    }

    DestroyTexture(outTex);
    outTex.width = width;
    outTex.height = height;
    outTex.mipLevels = generateMips ? CalcMipCount(width, height) : 1u;
    outTex.isCubeAtlas = isCubeAtlas;

    if (!GL::IsLoaded()) {
        return true;
    }

    const auto& gl = GL::GetFunctions();
    if (!gl.GenTextures || !gl.BindTexture || !gl.TexImage2D || !gl.TexParameteri) {
        return false;
    }

    GL::GLuint texId = 0u;
    gl.GenTextures(1, &texId);
    if (texId == 0u) {
        return false;
    }

    if (gl.ActiveTexture) {
        gl.ActiveTexture(GL::GL_TEXTURE0_VAL);
    }
    gl.BindTexture(GL::GL_TEXTURE_2D_VAL, texId);
    if (gl.PixelStorei) {
        gl.PixelStorei(GL::GL_UNPACK_ALIGNMENT_VAL, 1);
    }

    gl.TexImage2D(GL::GL_TEXTURE_2D_VAL,
                  0,
                  static_cast<GL::GLint>(GL::GL_RGBA8_VAL),
                  static_cast<GL::GLsizei>(width),
                  static_cast<GL::GLsizei>(height),
                  0,
                  GL::GL_RGBA_VAL,
                  GL::GL_UNSIGNED_BYTE_VAL,
                  rgba);

    if (generateMips && gl.GenerateMipmap) {
        gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_MAX_LEVEL_VAL,
                         static_cast<GL::GLint>(outTex.mipLevels - 1u));
        gl.GenerateMipmap(GL::GL_TEXTURE_2D_VAL);
        gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_MIN_FILTER_VAL,
                         static_cast<GL::GLint>(GL::GL_LINEAR_MIPMAP_LINEAR_VAL));
    } else {
        gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_MAX_LEVEL_VAL, 0);
        gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_MIN_FILTER_VAL,
                         static_cast<GL::GLint>(GL::GL_LINEAR_VAL));
    }

    gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_MAG_FILTER_VAL,
                     static_cast<GL::GLint>(GL::GL_LINEAR_VAL));
    const GL::GLint wrap = static_cast<GL::GLint>(isCubeAtlas ? GL::GL_CLAMP_TO_EDGE_VAL : GL::GL_REPEAT_VAL);
    gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_WRAP_S_VAL, wrap);
    gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_WRAP_T_VAL, wrap);
    gl.BindTexture(GL::GL_TEXTURE_2D_VAL, 0u);

    outTex.glTextureId = texId;
    return true;
}

void DecodeBC1BC3ToRGBA8(const uint8_t* srcCursor,
                         uint32_t width,
                         uint32_t height,
                         bool isBC3,
                         uint8_t* dstRgba) noexcept {
    const uint32_t bw = (width + 3u) >> 2u;
    const uint32_t bh = (height + 3u) >> 2u;
    const uint32_t strideBytes = width * 4u;
    const uint8_t* blkPtr = srcCursor;

    for (uint32_t by = 0u; by < bh; ++by) {
        const uint32_t y0 = by * 4u;
        for (uint32_t bx = 0u; bx < bw; ++bx) {
            const uint32_t x0 = bx * 4u;

            uint8_t aLut[8] = {255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u};
            uint64_t alphaBits = 0u;
            const uint8_t* colorBlk = blkPtr;

            if (isBC3) {
                const uint8_t a0 = blkPtr[0];
                const uint8_t a1 = blkPtr[1];
                aLut[0] = a0;
                aLut[1] = a1;
                if (a0 > a1) {
                    aLut[2] = static_cast<uint8_t>((6u * a0 + 1u * a1 + 3u) / 7u);
                    aLut[3] = static_cast<uint8_t>((5u * a0 + 2u * a1 + 3u) / 7u);
                    aLut[4] = static_cast<uint8_t>((4u * a0 + 3u * a1 + 3u) / 7u);
                    aLut[5] = static_cast<uint8_t>((3u * a0 + 4u * a1 + 3u) / 7u);
                    aLut[6] = static_cast<uint8_t>((2u * a0 + 5u * a1 + 3u) / 7u);
                    aLut[7] = static_cast<uint8_t>((1u * a0 + 6u * a1 + 3u) / 7u);
                } else {
                    aLut[2] = static_cast<uint8_t>((4u * a0 + 1u * a1 + 2u) / 5u);
                    aLut[3] = static_cast<uint8_t>((3u * a0 + 2u * a1 + 2u) / 5u);
                    aLut[4] = static_cast<uint8_t>((2u * a0 + 3u * a1 + 2u) / 5u);
                    aLut[5] = static_cast<uint8_t>((1u * a0 + 4u * a1 + 2u) / 5u);
                    aLut[6] = 0u;
                    aLut[7] = 255u;
                }
                for (uint32_t ab = 0u; ab < 6u; ++ab) {
                    alphaBits |= (static_cast<uint64_t>(blkPtr[2u + ab]) << (8u * ab));
                }
                colorBlk = blkPtr + 8u;
            }

            const uint16_t c0 = static_cast<uint16_t>(colorBlk[0]) |
                                static_cast<uint16_t>(static_cast<uint16_t>(colorBlk[1]) << 8u);
            const uint16_t c1 = static_cast<uint16_t>(colorBlk[2]) |
                                static_cast<uint16_t>(static_cast<uint16_t>(colorBlk[3]) << 8u);

            const uint32_t r0 = (((c0 >> 11u) & 0x1Fu) * 527u + 23u) >> 6u;
            const uint32_t g0 = (((c0 >>  5u) & 0x3Fu) * 259u + 33u) >> 6u;
            const uint32_t b0 = (( c0         & 0x1Fu) * 527u + 23u) >> 6u;

            const uint32_t r1 = (((c1 >> 11u) & 0x1Fu) * 527u + 23u) >> 6u;
            const uint32_t g1 = (((c1 >>  5u) & 0x3Fu) * 259u + 33u) >> 6u;
            const uint32_t b1 = (( c1         & 0x1Fu) * 527u + 23u) >> 6u;

            uint8_t pal[4][4]{};
            pal[0][0] = static_cast<uint8_t>(r0); pal[0][1] = static_cast<uint8_t>(g0);
            pal[0][2] = static_cast<uint8_t>(b0); pal[0][3] = 255u;

            pal[1][0] = static_cast<uint8_t>(r1); pal[1][1] = static_cast<uint8_t>(g1);
            pal[1][2] = static_cast<uint8_t>(b1); pal[1][3] = 255u;

            if (c0 > c1 || isBC3) {
                pal[2][0] = static_cast<uint8_t>((2u * r0 + r1 + 1u) / 3u);
                pal[2][1] = static_cast<uint8_t>((2u * g0 + g1 + 1u) / 3u);
                pal[2][2] = static_cast<uint8_t>((2u * b0 + b1 + 1u) / 3u);
                pal[2][3] = 255u;

                pal[3][0] = static_cast<uint8_t>((r0 + 2u * r1 + 1u) / 3u);
                pal[3][1] = static_cast<uint8_t>((g0 + 2u * g1 + 1u) / 3u);
                pal[3][2] = static_cast<uint8_t>((b0 + 2u * b1 + 1u) / 3u);
                pal[3][3] = 255u;
            } else {
                pal[2][0] = static_cast<uint8_t>((r0 + r1) >> 1u);
                pal[2][1] = static_cast<uint8_t>((g0 + g1) >> 1u);
                pal[2][2] = static_cast<uint8_t>((b0 + b1) >> 1u);
                pal[2][3] = 255u;

                pal[3][0] = 0u; pal[3][1] = 0u; pal[3][2] = 0u; pal[3][3] = 0u;
            }

            const uint32_t cBits = static_cast<uint32_t>(colorBlk[4]) |
                                  (static_cast<uint32_t>(colorBlk[5]) << 8u) |
                                  (static_cast<uint32_t>(colorBlk[6]) << 16u) |
                                  (static_cast<uint32_t>(colorBlk[7]) << 24u);

            for (uint32_t dy = 0u; dy < 4u; ++dy) {
                const uint32_t py = y0 + dy;
                if (py >= height) break;
                uint8_t* dstRow = dstRgba + static_cast<size_t>(py) * strideBytes;
                for (uint32_t dx = 0u; dx < 4u; ++dx) {
                    const uint32_t px = x0 + dx;
                    if (px >= width) break;
                    const uint32_t idx = dy * 4u + dx;
                    const uint32_t cCode = (cBits >> (2u * idx)) & 0x3u;
                    uint8_t* pOut = dstRow + static_cast<size_t>(px) * 4u;
                    pOut[0] = pal[cCode][0];
                    pOut[1] = pal[cCode][1];
                    pOut[2] = pal[cCode][2];
                    if (isBC3) {
                        const uint32_t aCode = static_cast<uint32_t>((alphaBits >> (3u * idx)) & 0x7u);
                        pOut[3] = aLut[aCode];
                    } else {
                        pOut[3] = pal[cCode][3];
                    }
                }
            }
            blkPtr += (isBC3 ? 16u : 8u);
        }
    }
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
        const float t = std::pow(dy, 0.55f);
        r = 0.68f * (1.0f - t) + 0.11f * t;
        g = 0.81f * (1.0f - t) + 0.35f * t;
        b = 0.92f * (1.0f - t) + 0.82f * t;

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
        const float t = std::pow(-dy, 0.45f);
        r = 0.58f * (1.0f - t) + 0.05f * t;
        g = 0.70f * (1.0f - t) + 0.16f * t;
        b = 0.80f * (1.0f - t) + 0.28f * t;
    }

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
    (void)debugLabel;
    if (!data || size < 16u) {
        return false;
    }

    const uint32_t magic = ReadBE32(data);
    const uint32_t width = ReadBE32(data + 4);
    const uint32_t height = ReadBE32(data + 8);
    const uint32_t rawMipAndFmt = ReadBE32(data + 12);
    uint32_t fmtCode = (rawMipAndFmt >> 16) & 0xFFFFu;
    if (fmtCode == 0u) fmtCode = 1u;

    if (magic != WTEX_MAGIC || width == 0u || height == 0u || width > 2048u || height > 2048u || fmtCode > 3u) {
        return false;
    }

    const bool isBC1 = (fmtCode == 2u);
    const bool isBC3 = (fmtCode == 3u);
    const bool isBlockCompressed = isBC1 || isBC3;
    const uint32_t blockBytes = isBC1 ? 8u : (isBC3 ? 16u : 4u);
    const uint32_t bw = isBlockCompressed ? ((width + 3u) >> 2u) : width;
    const uint32_t bh = isBlockCompressed ? ((height + 3u) >> 2u) : height;
    const size_t level0Bytes = static_cast<size_t>(bw) * static_cast<size_t>(bh) * static_cast<size_t>(blockBytes);
    if (size < (16ULL + level0Bytes)) {
        return false;
    }

    if (!isBlockCompressed) {
        return UploadRGBA8ToGL(outTex, data + 16ULL, width, height, true, false);
    }

    std::vector<uint8_t> decoded(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 255u);
    DecodeBC1BC3ToRGBA8(data + 16ULL, width, height, isBC3, decoded.data());
    return UploadRGBA8ToGL(outTex, decoded.data(), width, height, true, false);
}

bool LoadCookedTextureFromFile(std::string_view filepath,
                               GX2Texture& outTex,
                               const char* debugLabel) noexcept {
    std::vector<uint8_t> buffer;
    if (!AssetManager::LoadBinaryFile(std::string(filepath), buffer)) {
        return false;
    }
    return LoadCookedTextureFromMemory(buffer.data(), buffer.size(), outTex, debugLabel);
}

bool CreateSolidColorTexture(GX2Texture& outTex,
                             uint32_t rgba8Color,
                             const char* debugLabel) noexcept {
    (void)debugLabel;
    const uint8_t px[4] = {
        static_cast<uint8_t>((rgba8Color >> 24) & 0xFFu),
        static_cast<uint8_t>((rgba8Color >> 16) & 0xFFu),
        static_cast<uint8_t>((rgba8Color >> 8) & 0xFFu),
        static_cast<uint8_t>(rgba8Color & 0xFFu)
    };
    uint8_t img[4 * 4 * 4]{};
    for (uint32_t i = 0u; i < 16u; ++i) {
        std::memcpy(&img[i * 4u], px, 4u);
    }
    return UploadRGBA8ToGL(outTex, img, 4u, 4u, false, false);
}

bool CreateProceduralMipmapCheckerTexture(GX2Texture& outTex,
                                          uint32_t size,
                                          uint32_t checkerCells,
                                          bool colorCodeMips,
                                          const char* debugLabel) {
    (void)debugLabel;
    if (size < 16u || size > 1024u || (size & (size - 1u)) != 0u) {
        return false;
    }
    const uint32_t cells = (checkerCells == 0u) ? 8u : checkerCells;
    const uint32_t mipLevels = CalcMipCount(size, size);

    static constexpr uint8_t kMipPalette[8][3] = {
        {245, 245, 245},
        { 40, 230, 245},
        { 50, 225,  95},
        {250, 225,  45},
        {250, 140,  35},
        {240,  55,  70},
        {200,  70, 235},
        { 90, 120, 250}
    };

    std::vector<uint8_t> base(static_cast<size_t>(size) * static_cast<size_t>(size) * 4u, 255u);
    const uint32_t cellPixels = (size >= cells) ? (size / cells) : 1u;
    for (uint32_t y = 0u; y < size; ++y) {
        for (uint32_t x = 0u; x < size; ++x) {
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
                r = 215u; g = 165u; b = 40u;
            }
            const size_t idx = (static_cast<size_t>(y) * size + x) * 4u;
            base[idx + 0u] = r;
            base[idx + 1u] = g;
            base[idx + 2u] = b;
            base[idx + 3u] = 255u;
        }
    }

    if (!colorCodeMips || !GL::IsLoaded()) {
        return UploadRGBA8ToGL(outTex, base.data(), size, size, true, false);
    }

    // Carga explícita por nivel para visualizar bandas colorizadas por LOD en QA
    if (!UploadRGBA8ToGL(outTex, base.data(), size, size, false, false)) {
        return false;
    }

    const auto& gl = GL::GetFunctions();
    if (gl.ActiveTexture) {
        gl.ActiveTexture(GL::GL_TEXTURE0_VAL);
    }
    gl.BindTexture(GL::GL_TEXTURE_2D_VAL, outTex.glTextureId);
    gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_MAX_LEVEL_VAL,
                     static_cast<GL::GLint>(mipLevels - 1u));
    gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_MIN_FILTER_VAL,
                     static_cast<GL::GLint>(GL::GL_LINEAR_MIPMAP_LINEAR_VAL));

    uint32_t curW = size;
    uint32_t curH = size;
    for (uint32_t level = 1u; level < mipLevels; ++level) {
        curW = (curW > 1u) ? (curW >> 1u) : 1u;
        curH = (curH > 1u) ? (curH >> 1u) : 1u;
        const uint32_t palIdx = (level < 8u) ? level : 7u;
        const uint32_t subCells = (curW >= 4u) ? (curW / 4u) : 1u;
        std::vector<uint8_t> mipData(static_cast<size_t>(curW) * curH * 4u, 255u);
        for (uint32_t y = 0u; y < curH; ++y) {
            for (uint32_t x = 0u; x < curW; ++x) {
                const bool isLight = (((x / subCells) + (y / subCells)) & 1u) == 0u;
                const size_t idx = (static_cast<size_t>(y) * curW + x) * 4u;
                mipData[idx + 0u] = isLight ? kMipPalette[palIdx][0] : static_cast<uint8_t>(kMipPalette[palIdx][0] / 3u);
                mipData[idx + 1u] = isLight ? kMipPalette[palIdx][1] : static_cast<uint8_t>(kMipPalette[palIdx][1] / 3u);
                mipData[idx + 2u] = isLight ? kMipPalette[palIdx][2] : static_cast<uint8_t>(kMipPalette[palIdx][2] / 3u);
                mipData[idx + 3u] = 255u;
            }
        }
        gl.TexImage2D(GL::GL_TEXTURE_2D_VAL,
                      static_cast<GL::GLint>(level),
                      static_cast<GL::GLint>(GL::GL_RGBA8_VAL),
                      static_cast<GL::GLsizei>(curW),
                      static_cast<GL::GLsizei>(curH),
                      0,
                      GL::GL_RGBA_VAL,
                      GL::GL_UNSIGNED_BYTE_VAL,
                      mipData.data());
    }
    gl.BindTexture(GL::GL_TEXTURE_2D_VAL, 0u);
    outTex.mipLevels = mipLevels;
    return true;
}

bool CreateProceduralReliefTexture(GX2Texture& outTex,
                                   uint32_t size,
                                   const char* debugLabel) {
    (void)debugLabel;
    if (size < 16u || size > 1024u || (size & (size - 1u)) != 0u) {
        return false;
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(size) * static_cast<size_t>(size) * 4u, 255u);
    const uint32_t rows = 8u;
    const uint32_t cols = 4u;
    const float fSize = static_cast<float>(size);

    for (uint32_t y = 0u; y < size; ++y) {
        const float fy = (static_cast<float>(y) / fSize) * static_cast<float>(rows);
        const uint32_t rowIdx = static_cast<uint32_t>(fy);
        const float ly = fy - static_cast<float>(rowIdx);
        const float rowOffset = ((rowIdx & 1u) != 0u) ? 0.5f : 0.0f;

        for (uint32_t x = 0u; x < size; ++x) {
            const float fx = (static_cast<float>(x) / fSize) * static_cast<float>(cols) + rowOffset;
            const uint32_t colIdx = static_cast<uint32_t>(fx);
            const float lx = fx - static_cast<float>(colIdx);

            const float distX = (lx < 0.5f) ? lx : (1.0f - lx);
            const float distY = (ly < 0.5f) ? ly : (1.0f - ly);
            const float bevelX = (distX < 0.06f) ? 0.0f : ((distX < 0.14f) ? ((distX - 0.06f) / 0.08f) : 1.0f);
            const float bevelY = (distY < 0.08f) ? 0.0f : ((distY < 0.18f) ? ((distY - 0.08f) / 0.10f) : 1.0f);
            const float height = bevelX * bevelY;

            const uint32_t brickHash = ((rowIdx * 73856093u) ^ (colIdx * 19349663u)) & 0x1Fu;
            const float shade = 0.85f + (static_cast<float>(brickHash) / 31.0f) * 0.25f;

            const float baseR = 52.0f + height * 165.0f * shade;
            const float baseG = 48.0f + height * 118.0f * shade;
            const float baseB = 46.0f + height * 92.0f * shade;

            const size_t idx = (static_cast<size_t>(y) * size + x) * 4u;
            pixels[idx + 0u] = static_cast<uint8_t>(std::min(255.0f, baseR));
            pixels[idx + 1u] = static_cast<uint8_t>(std::min(255.0f, baseG));
            pixels[idx + 2u] = static_cast<uint8_t>(std::min(255.0f, baseB));
            pixels[idx + 3u] = 255u;
        }
    }

    return UploadRGBA8ToGL(outTex, pixels.data(), size, size, true, false);
}

bool LoadCookedCubeMapFromMemory(const uint8_t* data,
                                 size_t size,
                                 GX2Texture& outTex,
                                 const char* debugLabel) noexcept {
    return LoadCookedCubeMapAsAtlasFromMemory(data, size, outTex, debugLabel);
}

bool LoadCookedCubeMapFromFile(std::string_view filepath,
                               GX2Texture& outTex,
                               const char* debugLabel) noexcept {
    return LoadCookedCubeMapAsAtlasFromFile(filepath, outTex, debugLabel);
}

bool CreateProceduralSkyCubeMap(GX2Texture& outTex,
                                uint32_t faceSize,
                                const char* debugLabel) {
    return CreateProceduralSkyCubeAtlas(outTex, faceSize, debugLabel);
}

bool CreateProceduralSkyCubeAtlas(GX2Texture& outTex,
                                  uint32_t faceSize,
                                  const char* debugLabel) {
    (void)debugLabel;
    if (faceSize < 16u || faceSize > 512u || (faceSize & (faceSize - 1u)) != 0u) {
        return false;
    }

    const uint32_t atlasW = faceSize * 3u;
    const uint32_t atlasH = faceSize * 2u;
    std::vector<uint8_t> atlas(static_cast<size_t>(atlasW) * atlasH * 4u, 255u);

    const float sunLen = std::sqrt(0.45f * 0.45f + 0.52f * 0.52f + 0.72f * 0.72f);
    const float sunX = 0.45f / sunLen;
    const float sunY = 0.52f / sunLen;
    const float sunZ = -0.72f / sunLen;
    const float invSize = 2.0f / static_cast<float>(faceSize);

    for (uint32_t face = 0u; face < 6u; ++face) {
        const uint32_t baseX = (face % 3u) * faceSize;
        const uint32_t baseY = (face / 3u) * faceSize;

        for (uint32_t y = 0u; y < faceSize; ++y) {
            const float v = (static_cast<float>(y) + 0.5f) * invSize - 1.0f;
            uint8_t* rowPtr = atlas.data() + (static_cast<size_t>(baseY + y) * atlasW + baseX) * 4u;
            for (uint32_t x = 0u; x < faceSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) * invSize - 1.0f;
                float dx = 0.0f, dy = 0.0f, dz = 0.0f;
                switch (face) {
                    case 0u: dx =  1.0f; dy = -v;    dz = -u;    break;
                    case 1u: dx = -1.0f; dy = -v;    dz =  u;    break;
                    case 2u: dx =  u;    dy =  1.0f; dz =  v;    break;
                    case 3u: dx =  u;    dy = -1.0f; dz = -v;    break;
                    case 4u: dx =  u;    dy = -v;    dz =  1.0f; break;
                    case 5u: dx = -u;    dy = -v;    dz = -1.0f; break;
                }
                const float invLen = 1.0f / std::sqrt(dx * dx + dy * dy + dz * dz);
                EvaluateProceduralSkyPixel(dx * invLen, dy * invLen, dz * invLen,
                                           sunX, sunY, sunZ, &rowPtr[x * 4u]);
            }
        }
    }

    return UploadRGBA8ToGL(outTex, atlas.data(), atlasW, atlasH, false, true);
}

bool LoadCookedCubeMapAsAtlasFromMemory(const uint8_t* data,
                                        size_t size,
                                        GX2Texture& outTex,
                                        const char* debugLabel) noexcept {
    (void)debugLabel;
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

    const uint32_t atlasW = faceSize * 3u;
    const uint32_t atlasH = faceSize * 2u;
    std::vector<uint8_t> atlas(static_cast<size_t>(atlasW) * atlasH * 4u, 255u);
    const uint8_t* srcBase = data + 32u;

    for (uint32_t face = 0u; face < 6u; ++face) {
        const uint32_t baseX = (face % 3u) * faceSize;
        const uint32_t baseY = (face / 3u) * faceSize;
        const uint8_t* faceSrc = srcBase + static_cast<size_t>(face) * faceRawBytes;
        for (uint32_t y = 0u; y < faceSize; ++y) {
            uint8_t* rowDst = atlas.data() + (static_cast<size_t>(baseY + y) * atlasW + baseX) * 4u;
            std::memcpy(rowDst, faceSrc + static_cast<size_t>(y) * faceRowBytes, faceRowBytes);
        }
    }

    return UploadRGBA8ToGL(outTex, atlas.data(), atlasW, atlasH, false, true);
}

bool LoadCookedCubeMapAsAtlasFromFile(std::string_view filepath,
                                      GX2Texture& outTex,
                                      const char* debugLabel) noexcept {
    std::vector<uint8_t> buffer;
    if (!AssetManager::LoadBinaryFile(std::string(filepath), buffer)) {
        return false;
    }
    return LoadCookedCubeMapAsAtlasFromMemory(buffer.data(), buffer.size(), outTex, debugLabel);
}

bool CreateProceduralAlphaCutoutTexture(GX2Texture& outTex,
                                        uint32_t size,
                                        const char* debugLabel) {
    (void)debugLabel;
    if (size < 32u || size > 512u || (size & (size - 1u)) != 0u) {
        return false;
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4u, 0u);
    const float invSize = 1.0f / static_cast<float>(size);

    for (uint32_t y = 0u; y < size; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) * invSize;
        uint8_t* rowPtr = pixels.data() + static_cast<size_t>(y) * size * 4u;
        for (uint32_t x = 0u; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) * invSize;

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
                rowPtr[x * 4u + 0u] = 48u;
                rowPtr[x * 4u + 1u] = 84u;
                rowPtr[x * 4u + 2u] = 68u;
                rowPtr[x * 4u + 3u] = 255u;
            } else if (foliage) {
                const float shade = 0.72f + 0.28f * (1.0f - r / leafRadius);
                rowPtr[x * 4u + 0u] = FloatToU8(0.18f * shade);
                rowPtr[x * 4u + 1u] = FloatToU8(0.76f * shade);
                rowPtr[x * 4u + 2u] = FloatToU8(0.32f * shade);
                rowPtr[x * 4u + 3u] = 255u;
            }
        }
    }

    return UploadRGBA8ToGL(outTex, pixels.data(), size, size, false, false);
}

void DestroyTexture(GX2Texture& tex) noexcept {
    if (tex.glTextureId != 0u && GL::IsLoaded()) {
        const auto& gl = GL::GetFunctions();
        if (gl.DeleteTextures) {
            const GL::GLuint id = tex.glTextureId;
            gl.DeleteTextures(1, &id);
        }
    }
    tex = GX2Texture{};
}

} // namespace Neleo::Graphics::TextureLoaderGX2
