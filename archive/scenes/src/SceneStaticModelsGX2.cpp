#include "Scene/SceneStaticModelsGX2.hpp"
#include "Scene/SceneMenuModels.hpp"
#include "Scene/SceneOptions.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Core/StackString.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include "Graphics/GX2StateHelper.hpp"
#include "Graphics/EnvironmentShaderBinary.hpp"
#include "Core/ECS/Components/StudioTurntableComponent.hpp"
#include "Core/ECS/Systems/StudioTurntableSystem.hpp"
#include "Core/ECS/Components/SpringArmComponent.hpp"
#include "Core/ECS/Systems/SpringArmSystem.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

#if defined(__WIIU__) || defined(__wiiu__)
#include <proc_ui/procui.h>
#include <gx2/event.h>
#include <vpad/input.h>
#include <padscore/wpad.h>
#endif

namespace WiiU {

namespace {

// [OPT-312] Definición canónica de rectángulos UI con alineación estricta de píxeles
// (Jason Gregory, Game Engine Architecture 3rd Ed., Cap. 14.5: Texel-to-Pixel Half-Texel Grid Alignment)
constexpr UI::UIRect s_topBarPanel{12.0f, 8.0f, 1256.0f, 48.0f};
constexpr UI::UIRect s_btnVolver  {18.0f, 14.0f, 95.0f, 36.0f};
constexpr UI::UIRect s_btnOpciones{120.0f, 14.0f, 125.0f, 36.0f};
constexpr UI::UIRect s_btnBump    {252.0f, 14.0f, 120.0f, 36.0f};
constexpr UI::UIRect s_btnSombra  {379.0f, 14.0f, 125.0f, 36.0f};
constexpr UI::UIRect s_btnGiro    {511.0f, 14.0f, 115.0f, 36.0f};
constexpr UI::UIRect s_btnProps   {633.0f, 14.0f, 115.0f, 36.0f};
constexpr UI::UIRect s_btnLuz     {755.0f, 14.0f, 120.0f, 36.0f};
constexpr UI::UIRect s_btnEstres  {882.0f, 14.0f, 140.0f, 36.0f};

constexpr UI::UIRect s_btnZoomIn  {1180.0f, 240.0f, 80.0f, 44.0f};
constexpr UI::UIRect s_btnZoomOut {1180.0f, 292.0f, 80.0f, 44.0f};

struct StressGridPreset {
    const char* label;      // "1 CASA", "4 CASAS", "9 CASAS", "16 CASAS"
    const char* shortLabel; // "VILLA:1X", "VILLA:4X", "VILLA:9X", "VILLA:16X"
    int gridDim;            // 1, 2, 3, 4
    float spacing;          // 5.5f
    float defaultCamDist;   // 10.5f, 16.5f, 23.5f, 31.0f
};

constexpr int NUM_STRESS_PRESETS = 4;
constexpr StressGridPreset STRESS_PRESETS[NUM_STRESS_PRESETS] = {
    { "1 CASA (ESTUDIO)",      "VILLA:1X",  1, 5.5f, 10.5f },
    { "4 CASAS (2x2 CUAD)",    "VILLA:4X",  2, 5.5f, 16.5f },
    { "9 CASAS (3x3 ALDEA)",   "VILLA:9X",  3, 5.5f, 23.5f },
    { "16 CASAS (4x4 PUEBLO)", "VILLA:16X", 4, 5.5f, 31.0f }
};

// [OPT-325] Etapas del Ciclo Dinámico de Día y Noche (1 minuto por etapa, ciclo total 240s)
struct DayNightStage {
    const char* name;        // "AMANECER", "MEDIODIA", "ATARDECER", "NOCHE"
    const char* buttonLabel; // "DIA:MANANA", "DIA:MEDIO", "DIA:TARDE", "DIA:NOCHE"
    Point3D dir;             // Vector hacia la fuente luminosa
    ColorRGBA color;         // Tinte de la luz
    float intensity;         // Intensidad principal
};

constexpr int NUM_DAYNIGHT_STAGES = 4;
constexpr float DAYNIGHT_STAGE_DURATION = 60.0f; // 1 minuto por etapa
constexpr float DAYNIGHT_CYCLE_DURATION = DAYNIGHT_STAGE_DURATION * 4.0f; // 240 segundos

constexpr DayNightStage DAYNIGHT_STAGES[NUM_DAYNIGHT_STAGES] = {
    // 0. AMANECER (0m - 1m): Sol matutino dorado bajo desde el este
    { "AMANECER", "DIA:MANANA", Point3D{ 0.78f, 0.42f, -0.35f}, ColorRGBA{255, 210, 145, 255}, 1.18f },
    // 1. MEDIODIA (1m - 2m): Sol blanco puro brillante alto en el cenit
    { "MEDIODIA", "DIA:MEDIO",  Point3D{ 0.18f, 0.94f,  0.28f}, ColorRGBA{255, 252, 248, 255}, 1.28f },
    // 2. ATARDECER (2m - 3m): Sol ámbar / dorado rojizo crepuscular bajo por el oeste
    { "ATARDECER","DIA:TARDE",  Point3D{-0.85f, 0.32f,  0.42f}, ColorRGBA{255, 145,  68, 255}, 1.32f },
    // 3. NOCHE (3m - 4m): Luz de luna plateada/cian desde arriba
    { "NOCHE",    "DIA:NOCHE",  Point3D{ 0.32f, 0.78f, -0.52f}, ColorRGBA{140, 185, 255, 255}, 0.92f }
};

struct BumpPreset {
    const char* label;
    float scale;
};

constexpr int NUM_BUMP_PRESETS = 4;
constexpr BumpPreset BUMP_PRESETS[NUM_BUMP_PRESETS] = {
    { "OFF",    0.0f  },
    { "SUAVE",  0.25f },
    { "NORMAL", 0.55f },
    { "FUERTE", 0.90f }
};

struct ResolutionPreset {
    const char* label;
    const char* desc;
    uint32_t width;
    uint32_t height;
    float lodBias;
};

// Alineado con ResolutionMode de EngineConfig (0: 720p, 1: 1080p, 2: 480p, 3: 648p)
constexpr int NUM_RES_PRESETS = 4;
constexpr ResolutionPreset RES_PRESETS[NUM_RES_PRESETS] = {
    { "720P",  "720P (1280x720)",  1280,  720,  0.0f  },
    { "1080P", "1080P (1920x1080)", 1920, 1080, -0.75f },
    { "480P",  "480P (854x480)",    854,  480,  0.25f },
    { "648P",  "648P (1152x648)",  1152,  648,  0.15f }
};

struct GX2FilterPreset {
    const char* label;
    const char* desc;
#if defined(__WIIU__) || defined(__wiiu__)
    GX2TexXYFilterMode xyFilter;
    GX2TexAnisoRatio anisoRatio;
    BOOL highPrecision;
#else
    int xyFilter;
    int anisoRatio;
    bool highPrecision;
#endif
};

// Todos los modos de filtrado de texturas soportados en silicio por las TMUs de AMD Latte R700 (GX2)
constexpr int NUM_FILTER_PRESETS = 7;
#if defined(__WIIU__) || defined(__wiiu__)
constexpr GX2FilterPreset FILTER_PRESETS[NUM_FILTER_PRESETS] = {
    { "POINT",     "POINT (NEAREST)",  GX2_TEX_XY_FILTER_MODE_POINT,   GX2_TEX_ANISO_RATIO_NONE,    FALSE },
    { "BILINEAL",  "BILINEAL HW",      GX2_TEX_XY_FILTER_MODE_LINEAR,  GX2_TEX_ANISO_RATIO_NONE,    FALSE },
    { "BICUBICO",  "BICUBICO R700 HW", GX2_TEX_XY_FILTER_MODE_BICUBIC, GX2_TEX_ANISO_RATIO_NONE,    TRUE  },
    { "ANISO 2X",  "ANISO 2:1 HW",     GX2_TEX_XY_FILTER_MODE_LINEAR,  GX2_TEX_ANISO_RATIO_2_TO_1,  TRUE  },
    { "ANISO 4X",  "ANISO 4:1 HW",     GX2_TEX_XY_FILTER_MODE_LINEAR,  GX2_TEX_ANISO_RATIO_4_TO_1,  TRUE  },
    { "ANISO 8X",  "ANISO 8:1 HW",     GX2_TEX_XY_FILTER_MODE_LINEAR,  GX2_TEX_ANISO_RATIO_8_TO_1,  TRUE  },
    { "ANISO 16X", "ANISO 16:1 HW",    GX2_TEX_XY_FILTER_MODE_LINEAR,  GX2_TEX_ANISO_RATIO_16_TO_1, TRUE  }
};
#else
constexpr GX2FilterPreset FILTER_PRESETS[NUM_FILTER_PRESETS] = {
    { "POINT",     "POINT (NEAREST)",  0, 0, false },
    { "BILINEAL",  "BILINEAL HW",      1, 0, false },
    { "BICUBICO",  "BICUBICO R700 HW", 2, 0, true  },
    { "ANISO 2X",  "ANISO 2:1 HW",     1, 1, true  },
    { "ANISO 4X",  "ANISO 4:1 HW",     1, 2, true  },
    { "ANISO 8X",  "ANISO 8:1 HW",     1, 3, true  },
    { "ANISO 16X", "ANISO 16:1 HW",    1, 4, true  }
};
#endif

inline uint32_t ReadBE32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

} // namespace

SceneStaticModelsGX2::SceneStaticModelsGX2() = default;

SceneStaticModelsGX2::~SceneStaticModelsGX2() {
    DestroyGX2Pipeline();
    DestroyModelTextures();
}

bool SceneStaticModelsGX2::CreateSolidTexture(ResourceHandle<GX2Texture>& outHandle,
                                              uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                              const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    outHandle = m_texturePool.Emplace();
    GX2Texture* tex = m_texturePool.Get(outHandle);
    if (!tex) return false;

    constexpr uint32_t SOLID_SIZE = 16;
    std::memset(tex, 0, sizeof(GX2Texture));
    tex->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    tex->surface.width = SOLID_SIZE;
    tex->surface.height = SOLID_SIZE;
    tex->surface.depth = 1;
    tex->surface.mipLevels = 1;
    tex->surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    tex->surface.aa = GX2_AA_MODE1X;
    tex->surface.use = GX2_SURFACE_USE_TEXTURE;
    tex->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    GX2CalcSurfaceSizeAndAlignment(&tex->surface);
    tex->surface.image = MemoryManager::Instance().AllocAligned(
        tex->surface.alignment > 256 ? tex->surface.alignment : 256,
        tex->surface.imageSize,
        MemoryTag::Texture,
        debugLabel
    );

    if (!tex->surface.image) {
        m_texturePool.Destroy(outHandle);
        return false;
    }

    uint8_t* pImage = static_cast<uint8_t*>(tex->surface.image);
    uint32_t pitchBytes = tex->surface.pitch * 4;
    for (uint32_t y = 0; y < SOLID_SIZE; ++y) {
        uint8_t* row = pImage + y * pitchBytes;
        for (uint32_t x = 0; x < SOLID_SIZE; ++x) {
            row[x * 4 + 0] = r;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = b;
            row[x * 4 + 3] = a;
        }
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, tex->surface.image, tex->surface.imageSize);
    tex->viewFirstMip = 0;
    tex->viewNumMips = 1;
    tex->viewFirstSlice = 0;
    tex->viewNumSlices = 1;
    tex->compMap = 0x00010203;
    GX2InitTextureRegs(tex);
    return true;
#else
    (void)outHandle; (void)r; (void)g; (void)b; (void)a; (void)debugLabel;
    return true;
#endif
}

bool SceneStaticModelsGX2::CreateContactShadowTexture(ResourceHandle<GX2Texture>& outHandle, const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    outHandle = m_texturePool.Emplace();
    GX2Texture* tex = m_texturePool.Get(outHandle);
    if (!tex) return false;

    constexpr uint32_t SHADOW_TEX_SIZE = 128;
    std::memset(tex, 0, sizeof(GX2Texture));
    tex->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    tex->surface.width = SHADOW_TEX_SIZE;
    tex->surface.height = SHADOW_TEX_SIZE;
    tex->surface.depth = 1;
    tex->surface.mipLevels = 1;
    tex->surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    tex->surface.aa = GX2_AA_MODE1X;
    tex->surface.use = GX2_SURFACE_USE_TEXTURE;
    tex->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    GX2CalcSurfaceSizeAndAlignment(&tex->surface);
    tex->surface.image = MemoryManager::Instance().AllocAligned(
        tex->surface.alignment > 256 ? tex->surface.alignment : 256,
        tex->surface.imageSize,
        MemoryTag::Texture,
        debugLabel
    );

    if (!tex->surface.image) {
        m_texturePool.Destroy(outHandle);
        return false;
    }

    uint8_t* pImage = static_cast<uint8_t*>(tex->surface.image);
    uint32_t pitchBytes = tex->surface.pitch * 4;
    // Generar textura procedural con penumbra gaussiana elíptica (sombra de contacto de la casa)
    for (uint32_t y = 0; y < SHADOW_TEX_SIZE; ++y) {
        float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(SHADOW_TEX_SIZE) * 2.0f - 1.0f;
        uint8_t* row = pImage + y * pitchBytes;
        for (uint32_t x = 0; x < SHADOW_TEX_SIZE; ++x) {
            float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(SHADOW_TEX_SIZE) * 2.0f - 1.0f;
            // Forma de caja suave/elipse (la casa es alargada en X=1.2, Z=1.0)
            float dBox = std::sqrt((nx * 1.05f) * (nx * 1.05f) + (ny * 1.15f) * (ny * 1.15f));
            float alpha = 1.0f - std::clamp((dBox - 0.25f) / 0.75f, 0.0f, 1.0f);
            alpha = alpha * alpha * (3.0f - 2.0f * alpha); // Curva cúbica suave
            uint8_t aVal = static_cast<uint8_t>(std::clamp(alpha * 225.0f, 0.0f, 255.0f));

            row[x * 4 + 0] = 12; // R
            row[x * 4 + 1] = 14; // G
            row[x * 4 + 2] = 18; // B
            row[x * 4 + 3] = aVal; // Alpha
        }
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, tex->surface.image, tex->surface.imageSize);
    tex->viewFirstMip = 0;
    tex->viewNumMips = 1;
    tex->viewFirstSlice = 0;
    tex->viewNumSlices = 1;
    tex->compMap = 0x00010203;
    GX2InitTextureRegs(tex);
    return true;
#else
    (void)outHandle; (void)debugLabel;
    return true;
#endif
}

bool SceneStaticModelsGX2::LoadCookedTexture(std::string_view filepath,
                                             const std::vector<uint8_t>& stagedBuffer,
                                             ResourceHandle<GX2Texture>& outHandle,
                                             const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    std::vector<uint8_t> localBuffer;
    const uint8_t* pData = nullptr;
    size_t dataSize = 0;

    if (!stagedBuffer.empty()) {
        pData = stagedBuffer.data();
        dataSize = stagedBuffer.size();
    } else {
        if (AssetManager::LoadBinaryFile(std::string(filepath), localBuffer) && !localBuffer.empty()) {
            pData = localBuffer.data();
            dataSize = localBuffer.size();
        }
    }

    if (!pData || dataSize < 16) {
        return CreateSolidTexture(outHandle, 220, 215, 205, 255, debugLabel);
    }

    // Validar cabecera Big-Endian 'WTEX' (0x57544558)
    // Word 3 codifica: bits[0..15] = mipLevels, bits[16..31] = formatCode (0/1=RGBA8, 2=BC1/DXT1, 3=BC3/DXT5)
    uint32_t magic = ReadBE32(pData);
    uint32_t width = ReadBE32(pData + 4);
    uint32_t height = ReadBE32(pData + 8);
    uint32_t rawMipAndFmt = ReadBE32(pData + 12);
    uint32_t fmtCode = (rawMipAndFmt >> 16) & 0xFFFFu;
    uint32_t rawMipLevels = rawMipAndFmt & 0xFFFFu;
    if (fmtCode == 0) {
        fmtCode = 1; // Retrocompatibilidad con cabecera v1 (donde word 3 era únicamente mipLevels)
    }
    uint32_t mipLevels = (rawMipLevels >= 1 && rawMipLevels <= 13) ? rawMipLevels : 1;

    const bool isBC1 = (fmtCode == 2);
    const bool isBC3 = (fmtCode == 3);
    const bool isBlockCompressed = isBC1 || isBC3;
    const uint32_t blockBytes = isBC1 ? 8u : (isBC3 ? 16u : 4u);

    uint32_t blocksW0 = isBlockCompressed ? ((width + 3u) >> 2) : width;
    uint32_t blocksH0 = isBlockCompressed ? ((height + 3u) >> 2) : height;
    size_t baseBytes = static_cast<size_t>(blocksW0) * static_cast<size_t>(blocksH0) * static_cast<size_t>(blockBytes);

    if (magic != 0x57544558U || width == 0 || height == 0 || width > 2048 || height > 2048 || dataSize < (16ULL + baseBytes)) {
        return CreateSolidTexture(outHandle, 220, 215, 205, 255, debugLabel);
    }

    outHandle = m_texturePool.Emplace();
    GX2Texture* tex = m_texturePool.Get(outHandle);
    if (!tex) return false;

    std::memset(tex, 0, sizeof(GX2Texture));
    tex->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    tex->surface.width = width;
    tex->surface.height = height;
    tex->surface.depth = 1;
    tex->surface.mipLevels = 1; // En GX2_TILE_MODE_LINEAR_ALIGNED, Latte R700 opera con nivel base Mip 0
    tex->surface.format = isBC1 ? GX2_SURFACE_FORMAT_UNORM_BC1
                        : (isBC3 ? GX2_SURFACE_FORMAT_UNORM_BC3
                                 : GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8);
    tex->surface.aa = GX2_AA_MODE1X;
    tex->surface.use = GX2_SURFACE_USE_TEXTURE;
    tex->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    GX2CalcSurfaceSizeAndAlignment(&tex->surface);
    uint32_t surfAlign = tex->surface.alignment > 256 ? tex->surface.alignment : 256;
    tex->surface.image = MemoryManager::Instance().AllocAligned(
        surfAlign,
        tex->surface.imageSize,
        MemoryTag::Texture,
        debugLabel
    );

    if (!tex->surface.image) {
        m_texturePool.Destroy(outHandle);
        return CreateSolidTexture(outHandle, 220, 215, 205, 255, debugLabel);
    }

    // Copiar Nivel Base (Mip 0) respetando el pitch de hardware (en texeles para RGBA8, en bloques 4x4 para BC1/BC3)
    uint8_t* pImage = static_cast<uint8_t*>(tex->surface.image);
    const uint8_t* srcCursor = pData + 16;
    uint32_t pitchBytes0 = tex->surface.pitch * blockBytes;
    uint32_t rowBytes0 = blocksW0 * blockBytes;

    for (uint32_t y = 0; y < blocksH0; ++y) {
        std::memcpy(pImage + y * pitchBytes0, srcCursor + y * rowBytes0, rowBytes0);
    }
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, tex->surface.image, tex->surface.imageSize);

    tex->surface.mipmaps = nullptr;
    tex->surface.mipmapSize = 0;
    tex->viewFirstMip = 0;
    tex->viewNumMips = 1;
    tex->viewFirstSlice = 0;
    tex->viewNumSlices = 1;
    tex->compMap = 0x00010203;
    GX2InitTextureRegs(tex);
    return true;
#else
    (void)filepath; (void)stagedBuffer; (void)outHandle; (void)debugLabel;
    return true;
#endif
}

void SceneStaticModelsGX2::UpdateSamplerFilter() {
#if defined(__WIIU__) || defined(__wiiu__)
    const auto& filterPreset = FILTER_PRESETS[m_filterPresetIndex];
    const auto& resPreset = RES_PRESETS[m_resPresetIndex];

    GX2InitSampler(&m_sampler, GX2_TEX_CLAMP_MODE_WRAP, filterPreset.xyFilter);
    GX2InitSamplerXYFilter(&m_sampler, filterPreset.xyFilter, filterPreset.xyFilter, filterPreset.anisoRatio);
    GX2InitSamplerZMFilter(&m_sampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_NONE);
    GX2InitSamplerFilterAdjust(&m_sampler, filterPreset.highPrecision,
                               GX2_TEX_MIP_PERF_MODE_DISABLE, GX2_TEX_Z_PERF_MODE_DISABLED);
    GX2InitSamplerLOD(&m_sampler, 0.0f, 0.0f, resPreset.lodBias);
#endif
}

bool SceneStaticModelsGX2::CreateModelTextures() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_texturesCreated) return true;

    bool ok1 = LoadCookedTexture("models/dutch_house.wtex", m_stagedHouseTex, m_hHouseTexture, "Tex_DutchHouse");
    bool ok2 = LoadCookedTexture("models/dutch_house_props.wtex", m_stagedPropsTex, m_hPropsTexture, "Tex_DutchProps");
    bool ok3 = LoadCookedTexture("models/dutch_house_alpha.wtex", m_stagedAlphaTex, m_hAlphaTexture, "Tex_DutchAlpha");
    bool ok4 = CreateSolidTexture(m_hClayTexture, 230, 228, 224, 255, "Tex_ClayStudio");
    bool ok5 = CreateSolidTexture(m_hPedestalTexture, 36, 42, 54, 255, "Tex_StudioPedestal");
    bool ok6 = CreateContactShadowTexture(m_hShadowTexture, "Tex_ContactShadow");

    if (ok1 && ok2 && ok3 && ok4 && ok5 && ok6) {
        UpdateSamplerFilter();
        m_texturesCreated = true;
        return true;
    }
    return false;
#else
    return true;
#endif
}

bool SceneStaticModelsGX2::CreateStudioPedestalMeshes() {
    if (m_pedestalCreated) return true;

    // 1. Crear Malla del Pedestal Cilíndrico de Estudio (Radio 7.5m, Altura 0.35m, 32 lados)
    constexpr int SLICES = 32;
    constexpr float RADIUS = 7.5f;
    constexpr float TOP_Y = 0.0f;
    constexpr float BOT_Y = -0.35f;

    std::vector<uint8_t> pedData;
    size_t pedHeaderSize = 96;
    size_t pedVboOffset = 256; // Alineado a 256B

    // Vértices: Centro superior (1) + Borde superior (SLICES+1) + Centro inferior (1) + Borde inferior (SLICES+1) + Lados (2*(SLICES+1))
    std::vector<MeshVertex> pedVerts;
    std::vector<uint16_t> pedIndices;

    // Vértice central cara superior
    pedVerts.push_back(MeshVertex{{0.0f, TOP_Y, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}, {0.85f, 0.88f, 0.95f, 1.0f}});
    for (int i = 0; i <= SLICES; ++i) {
        float angle = static_cast<float>(i) * (6.2831853f / static_cast<float>(SLICES));
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        float u = 0.5f + 0.5f * cosA;
        float v = 0.5f + 0.5f * sinA;
        pedVerts.push_back(MeshVertex{{cosA * RADIUS, TOP_Y, sinA * RADIUS}, {0.0f, 1.0f, 0.0f}, {u, v}, {0.75f, 0.78f, 0.85f, 1.0f}});
    }
    for (int i = 0; i < SLICES; ++i) {
        pedIndices.push_back(0);
        pedIndices.push_back(static_cast<uint16_t>(i + 1));
        pedIndices.push_back(static_cast<uint16_t>(i + 2));
    }

    // Borde biselado lateral
    uint16_t sideBase = static_cast<uint16_t>(pedVerts.size());
    for (int i = 0; i <= SLICES; ++i) {
        float angle = static_cast<float>(i) * (6.2831853f / static_cast<float>(SLICES));
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        float u = static_cast<float>(i) / static_cast<float>(SLICES) * 8.0f;
        pedVerts.push_back(MeshVertex{{cosA * RADIUS, TOP_Y, sinA * RADIUS}, {cosA, 0.0f, sinA}, {u, 0.0f}, {0.55f, 0.58f, 0.65f, 1.0f}});
        pedVerts.push_back(MeshVertex{{cosA * RADIUS, BOT_Y, sinA * RADIUS}, {cosA, 0.0f, sinA}, {u, 1.0f}, {0.35f, 0.38f, 0.45f, 1.0f}});
    }
    for (int i = 0; i < SLICES; ++i) {
        uint16_t i0 = sideBase + i * 2;
        uint16_t i1 = i0 + 1;
        uint16_t i2 = i0 + 2;
        uint16_t i3 = i0 + 3;
        pedIndices.push_back(i0);
        pedIndices.push_back(i1);
        pedIndices.push_back(i2);
        pedIndices.push_back(i2);
        pedIndices.push_back(i1);
        pedIndices.push_back(i3);
    }

    size_t pedVboBytes = pedVerts.size() * sizeof(MeshVertex);
    size_t pedIboOffset = (pedVboOffset + pedVboBytes + 31) & ~31;
    size_t pedIboBytes = pedIndices.size() * sizeof(uint16_t);
    pedData.resize(pedIboOffset + pedIboBytes, 0);

    MeshHeader pedHdr{};
    pedHdr.magic = WMESH_MAGIC;
    pedHdr.version = WMESH_VERSION_CURRENT;
    pedHdr.flags = MESH_FLAG_STATIC | MESH_FLAG_HAS_COLOR | MESH_FLAG_HAS_TEXCOORD | MESH_FLAG_HAS_NORMAL;
    pedHdr.vertexCount = static_cast<uint32_t>(pedVerts.size());
    pedHdr.indexCount = static_cast<uint32_t>(pedIndices.size());
    pedHdr.vertexStride = sizeof(MeshVertex);
    pedHdr.indexFormat = 0; // U16
    pedHdr.vboOffset = static_cast<uint32_t>(pedVboOffset);
    pedHdr.vboSize = static_cast<uint32_t>(pedVboBytes);
    pedHdr.iboOffset = static_cast<uint32_t>(pedIboOffset);
    pedHdr.iboSize = static_cast<uint32_t>(pedIboBytes);
    pedHdr.boundingSphere[0] = 0.0f; pedHdr.boundingSphere[1] = -0.175f; pedHdr.boundingSphere[2] = 0.0f; pedHdr.boundingSphere[3] = RADIUS;
    pedHdr.aabbMin[0] = -RADIUS; pedHdr.aabbMin[1] = BOT_Y; pedHdr.aabbMin[2] = -RADIUS;
    pedHdr.aabbMax[0] =  RADIUS; pedHdr.aabbMax[1] = TOP_Y; pedHdr.aabbMax[2] =  RADIUS;
    pedHdr.submeshCount = 1;

    std::memcpy(pedData.data(), &pedHdr, sizeof(MeshHeader));
    std::memcpy(pedData.data() + pedVboOffset, pedVerts.data(), pedVboBytes);
    std::memcpy(pedData.data() + pedIboOffset, pedIndices.data(), pedIboBytes);
    m_pedestalMesh.LoadFromMemory(pedData.data(), pedData.size());

    // 2. Crear Malla del Plano de Sombra de Contacto (Quad horizontal a Y = 0.005f sobre el pedestal)
    constexpr float SHADOW_W = 4.8f;
    constexpr float SHADOW_D = 4.4f;
    std::vector<MeshVertex> shVerts = {
        MeshVertex{{-SHADOW_W, 0.005f, -SHADOW_D}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
        MeshVertex{{ SHADOW_W, 0.005f, -SHADOW_D}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
        MeshVertex{{ SHADOW_W, 0.005f,  SHADOW_D}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
        MeshVertex{{-SHADOW_W, 0.005f,  SHADOW_D}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    };
    std::vector<uint16_t> shIndices = { 0, 1, 2, 0, 2, 3 };

    std::vector<uint8_t> shData;
    size_t shVboOffset = 256;
    size_t shVboBytes = shVerts.size() * sizeof(MeshVertex);
    size_t shIboOffset = (shVboOffset + shVboBytes + 31) & ~31;
    size_t shIboBytes = shIndices.size() * sizeof(uint16_t);
    shData.resize(shIboOffset + shIboBytes, 0);

    MeshHeader shHdr{};
    shHdr.magic = WMESH_MAGIC;
    shHdr.version = WMESH_VERSION_CURRENT;
    shHdr.flags = MESH_FLAG_STATIC | MESH_FLAG_HAS_COLOR | MESH_FLAG_HAS_TEXCOORD | MESH_FLAG_HAS_NORMAL;
    shHdr.vertexCount = 4;
    shHdr.indexCount = 6;
    shHdr.vertexStride = sizeof(MeshVertex);
    shHdr.indexFormat = 0;
    shHdr.vboOffset = static_cast<uint32_t>(shVboOffset);
    shHdr.vboSize = static_cast<uint32_t>(shVboBytes);
    shHdr.iboOffset = static_cast<uint32_t>(shIboOffset);
    shHdr.iboSize = static_cast<uint32_t>(shIboBytes);
    shHdr.boundingSphere[0] = 0.0f; shHdr.boundingSphere[1] = 0.005f; shHdr.boundingSphere[2] = 0.0f; shHdr.boundingSphere[3] = 4.0f;
    shHdr.aabbMin[0] = -SHADOW_W; shHdr.aabbMin[1] = 0.0f; shHdr.aabbMin[2] = -SHADOW_D;
    shHdr.aabbMax[0] =  SHADOW_W; shHdr.aabbMax[1] = 0.01f; shHdr.aabbMax[2] =  SHADOW_D;
    shHdr.submeshCount = 1;

    std::memcpy(shData.data(), &shHdr, sizeof(MeshHeader));
    std::memcpy(shData.data() + shVboOffset, shVerts.data(), shVboBytes);
    std::memcpy(shData.data() + shIboOffset, shIndices.data(), shIboBytes);
    m_shadowMesh.LoadFromMemory(shData.data(), shData.size());

    m_pedestalCreated = true;
    return true;
}

void SceneStaticModelsGX2::DestroyModelTextures() {
#if defined(__WIIU__) || defined(__wiiu__)
    auto freeTexHandle = [this](ResourceHandle<GX2Texture>& handle) {
        if (GX2Texture* tex = m_texturePool.Get(handle)) {
            if (tex->surface.mipmaps) {
                MemoryManager::Instance().FreeAligned(tex->surface.mipmaps);
                tex->surface.mipmaps = nullptr;
            }
            if (tex->surface.image) {
                MemoryManager::Instance().FreeAligned(tex->surface.image);
                tex->surface.image = nullptr;
            }
            m_texturePool.Destroy(handle);
        }
    };

    freeTexHandle(m_hHouseTexture);
    freeTexHandle(m_hPropsTexture);
    freeTexHandle(m_hAlphaTexture);
    freeTexHandle(m_hClayTexture);
    freeTexHandle(m_hPedestalTexture);
    freeTexHandle(m_hShadowTexture);
    m_texturesCreated = false;
#endif
}

bool SceneStaticModelsGX2::InitGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_shaderLoaded && m_texturesCreated && m_depthBufferCreated && m_nativeContextState) {
        return true;
    }

    if (!CreateModelTextures()) {
        return false;
    }

    if (!m_shaderLoaded) {
        std::vector<uint8_t> shaderData;
        bool loaded = AssetManager::LoadBinaryFile("shaders/environment_gx2.gsh", shaderData);
        if (!loaded || shaderData.empty()) {
            shaderData.assign(ENVIRONMENT_GX2_GSH_DATA, ENVIRONMENT_GX2_GSH_DATA + ENVIRONMENT_GX2_GSH_SIZE);
            loaded = true;
        }

        if (loaded && !shaderData.empty()) {
            uint32_t vsHeaderSize = GFDGetVertexShaderHeaderSize(0, shaderData.data());
            uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
            uint32_t psHeaderSize = GFDGetPixelShaderHeaderSize(0, shaderData.data());
            uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());

            if (vsHeaderSize > 0 && vsProgSize > 0 && psHeaderSize > 0 && psProgSize > 0) {
                m_directVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHeaderSize, MemoryTag::ShaderProgram, "StaticMod_VS_Hdr"));
                m_directVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "StaticMod_VS_Prog");
                m_directPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHeaderSize, MemoryTag::ShaderProgram, "StaticMod_PS_Hdr"));
                m_directPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "StaticMod_PS_Prog");

                if (m_directVs && m_directVsProgram && m_directPs && m_directPsProgram) {
                    BOOL vsOk = GFDGetVertexShader(m_directVs, m_directVsProgram, 0, shaderData.data());
                    BOOL psOk = GFDGetPixelShader(m_directPs, m_directPsProgram, 0, shaderData.data());
                    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directVsProgram, vsProgSize);
                    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directPsProgram, psProgSize);

                    if (vsOk && psOk) {
                        GX2AttribStream attrs[4];
                        std::memset(attrs, 0, sizeof(attrs));
                        attrs[0].location = 0;
                        attrs[0].buffer = 0;
                        attrs[0].offset = offsetof(MeshVertex, pos);
                        attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                        attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[0].aluDivisor = 0;
                        attrs[0].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                        attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                        attrs[1].location = 1;
                        attrs[1].buffer = 0;
                        attrs[1].offset = offsetof(MeshVertex, normal);
                        attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                        attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[1].aluDivisor = 0;
                        attrs[1].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                        attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                        attrs[2].location = 2;
                        attrs[2].buffer = 0;
                        attrs[2].offset = offsetof(MeshVertex, texCoord);
                        attrs[2].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
                        attrs[2].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[2].aluDivisor = 0;
                        attrs[2].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                        attrs[2].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                        attrs[3].location = 3;
                        attrs[3].buffer = 0;
                        attrs[3].offset = offsetof(MeshVertex, color);
                        attrs[3].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
                        attrs[3].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[3].aluDivisor = 0;
                        attrs[3].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                        attrs[3].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                        uint32_t fsSize = GX2CalcFetchShaderSizeEx(4, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                        m_directFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "StaticMod_FS_Prog");
                        if (m_directFsProgram) {
                            GX2InitFetchShaderEx(&m_directFs, static_cast<uint8_t*>(m_directFsProgram), 4, attrs,
                                                 GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directFsProgram, fsSize);
                            m_shaderLoaded = true;
                            m_useDirectGFD = true;
                        }
                    }
                }
            }
        }
    }

    if (!m_depthBufferCreated) {
        std::memset(&m_depthBuffer, 0, sizeof(GX2DepthBuffer));
        m_depthBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        m_depthBuffer.surface.width = 1280;
        m_depthBuffer.surface.height = 720;
        m_depthBuffer.surface.depth = 1;
        m_depthBuffer.surface.mipLevels = 1;
        m_depthBuffer.surface.format = GX2_SURFACE_FORMAT_FLOAT_D24_S8;
        m_depthBuffer.surface.aa = GX2_AA_MODE1X;
        m_depthBuffer.surface.use = GX2_SURFACE_USE_DEPTH_BUFFER;
        m_depthBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
        m_depthBuffer.viewMip = 0;
        m_depthBuffer.viewFirstSlice = 0;
        m_depthBuffer.viewNumSlices = 1;
        m_depthBuffer.depthClear = 1.0f;
        m_depthBuffer.stencilClear = 0;

        GX2CalcSurfaceSizeAndAlignment(&m_depthBuffer.surface);

        if (GX2RCreateSurface(&m_depthBuffer.surface,
                              static_cast<GX2RResourceFlags>(GX2R_RESOURCE_BIND_DEPTH_BUFFER |
                                                             GX2R_RESOURCE_USAGE_GPU_READ |
                                                             GX2R_RESOURCE_USAGE_GPU_WRITE))) {
            GX2InitDepthBufferRegs(&m_depthBuffer);
            GX2InitDepthBufferHiZEnable(&m_depthBuffer, FALSE);
            m_depthBufferCreated = true;
        }
    }

    if (!m_nativeContextState) {
        m_nativeContextState = static_cast<GX2ContextState*>(
            MemoryManager::Instance().AllocAligned(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState), MemoryTag::ContextState, "StaticModCtxState")
        );
        if (m_nativeContextState) {
            GX2SetupContextStateEx(m_nativeContextState, TRUE);
        }
    }

    return m_shaderLoaded && m_texturesCreated && m_depthBufferCreated && (m_nativeContextState != nullptr);
#else
    return true;
#endif
}

void SceneStaticModelsGX2::DestroyGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown()) {
        GX2DrawDone();
    }

    if (m_depthBufferCreated) {
        GX2RDestroySurfaceEx(&m_depthBuffer.surface, GX2R_RESOURCE_BIND_NONE);
        m_depthBufferCreated = false;
    }

    if (m_nativeContextState) {
        void* rawCtx = static_cast<void*>(m_nativeContextState);
        MemoryManager::Instance().FreeAligned(rawCtx);
        m_nativeContextState = nullptr;
    }

    if (m_shaderLoaded) {
        if (!m_useDirectGFD) {
            WHBGfxFreeShaderGroup(&m_shaderGroup);
        } else {
            void* rawVs = static_cast<void*>(m_directVs);
            MemoryManager::Instance().Free(rawVs);
            m_directVs = nullptr;
            MemoryManager::Instance().FreeAligned(m_directVsProgram);
            m_directVsProgram = nullptr;
            void* rawPs = static_cast<void*>(m_directPs);
            MemoryManager::Instance().Free(rawPs);
            m_directPs = nullptr;
            MemoryManager::Instance().FreeAligned(m_directPsProgram);
            m_directPsProgram = nullptr;
            MemoryManager::Instance().FreeAligned(m_directFsProgram);
            m_directFsProgram = nullptr;
        }
        m_shaderLoaded = false;
        m_useDirectGFD = false;
    }

    m_pColorBuffer = nullptr;
#endif
}

void SceneStaticModelsGX2::BindColorBuffer(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    m_pColorBuffer = GetSDL2ColorBuffer(renderer);
#else
    (void)renderer;
#endif
}

void SceneStaticModelsGX2::LoadAsync() {
    if (m_asyncStaged) return;

    // Pilar 2 (Jason Gregory Cap. 7): El hilo trabajador en Core 2 realiza la precarga
    // asíncrona de archivos binarios (.wtex y .wmesh) hacia vectores en memoria RAM.
    // [REGLA DE ORO SILICIO]: El Core 2 NUNCA debe invocar APIs de GX2 (GX2InitTextureRegs,
    // GX2CalcSurfaceSizeAndAlignment, GX2Invalidate, GX2InitSampler) porque el subsistema
    // GX2 no es reentrante y el Core 1 está usando activamente la GPU para renderizar la pantalla de carga.
    AssetManager::LoadBinaryFile("models/dutch_house.wtex", m_stagedHouseTex);
    AssetManager::LoadBinaryFile("models/dutch_house_props.wtex", m_stagedPropsTex);
    AssetManager::LoadBinaryFile("models/dutch_house_alpha.wtex", m_stagedAlphaTex);
    AssetManager::LoadBinaryFile("models/dutch_house.wmesh", m_stagedHouseMesh);
    AssetManager::LoadBinaryFile("models/dutch_house_props.wmesh", m_stagedPropsMesh);
    AssetManager::LoadBinaryFile("models/dutch_house_alpha.wmesh", m_stagedAlphaMesh);
    m_asyncStaged = true;
}

void SceneStaticModelsGX2::OnEnter() {
    m_animTime = 0.0f;
    m_dayNightTime = 60.0f; // Inicia en Mediodía (1m - 2m, luz óptima y brillante)
    m_lastFrameTicks = SDL_GetTicks();
    m_smoothedFps = 60.0f;
    AudioManager::Instance().StopBGM();

    // [Hito 124 - Jason Gregory Cap. 5.3] Sincronizar parámetros globales del motor
    const auto& gcfg = EngineConfig::Get().graphics;
    m_filterPresetIndex = static_cast<int>(gcfg.textureFilter);
    if (m_filterPresetIndex < 0 || m_filterPresetIndex >= NUM_FILTER_PRESETS) {
        m_filterPresetIndex = 4; // Aniso 4X
    }
    m_resPresetIndex = static_cast<int>(gcfg.resolution);
    if (m_resPresetIndex < 0 || m_resPresetIndex >= NUM_RES_PRESETS) {
        m_resPresetIndex = 0; // 720p
    }
    m_clayMode = (gcfg.renderMode == ShadingRenderMode::Clay);
    m_showPedestal = gcfg.showPedestal;

    // Inicialización del pipeline GX2 de forma segura en Core 1 (hilo principal de render)
    InitGX2Pipeline();
    UpdateSamplerFilter();

    // Liberar inmediatamente los búferes de staging de texturas en RAM tras subirlos a MEM2
    m_stagedHouseTex.clear();
    m_stagedHouseTex.shrink_to_fit();
    m_stagedPropsTex.clear();
    m_stagedPropsTex.shrink_to_fit();
    m_stagedAlphaTex.clear();
    m_stagedAlphaTex.shrink_to_fit();

    // 1. Cargar mallas cocinadas .wmesh de la Casa Holandesa (usando staging de Core 2 si está disponible)
    bool hOk = !m_stagedHouseMesh.empty()
        ? m_houseMesh.LoadFromMemory(m_stagedHouseMesh.data(), m_stagedHouseMesh.size())
        : m_houseMesh.LoadFromFile("models/dutch_house.wmesh");
    bool pOk = !m_stagedPropsMesh.empty()
        ? m_propsMesh.LoadFromMemory(m_stagedPropsMesh.data(), m_stagedPropsMesh.size())
        : m_propsMesh.LoadFromFile("models/dutch_house_props.wmesh");
    bool aOk = !m_stagedAlphaMesh.empty()
        ? m_alphaMesh.LoadFromMemory(m_stagedAlphaMesh.data(), m_stagedAlphaMesh.size())
        : m_alphaMesh.LoadFromFile("models/dutch_house_alpha.wmesh");
    m_modelsLoaded = hOk && pOk && aOk;

    // Liberar inmediatamente los búferes de staging de mallas en RAM tras subir a GX2R VBO/IBO
    m_stagedHouseMesh.clear();
    m_stagedHouseMesh.shrink_to_fit();
    m_stagedPropsMesh.clear();
    m_stagedPropsMesh.shrink_to_fit();
    m_stagedAlphaMesh.clear();
    m_stagedAlphaMesh.shrink_to_fit();

    // 2. Registrar entidades en ECSWorld centradas en el origen (0, 0, 0)
    m_ecsWorld.Clear();
    Point3D centerOffset{0.63f, 0.0f, -0.16f};

    // 2.1 Entidad Principal: Fachada y estructura de la Casa Holandesa
    m_houseEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_houseEntity, centerOffset);
    auto& houseMeshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_houseEntity);
    m_houseMesh.PopulateMeshComponent(houseMeshComp);
    auto& houseBoundComp = m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_houseEntity);
    m_houseMesh.PopulateBoundingComponent(houseBoundComp);
    auto& houseMat = m_ecsWorld.EmplaceComponent<MaterialComponent>(m_houseEntity,
        1.0f, 1.0f, 0.55f, -0.35f, BlendMode::Opaque, true, true);
#if defined(__WIIU__) || defined(__wiiu__)
    houseMat.textureHandle = m_hHouseTexture;
#endif

    // 2.2 Entidad Props: Farolas, barriles, chimeneas, vallas y detalles exteriores
    m_propsEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_propsEntity, centerOffset);
    auto& propsMeshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_propsEntity);
    m_propsMesh.PopulateMeshComponent(propsMeshComp);
    auto& propsBoundComp = m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_propsEntity);
    m_propsMesh.PopulateBoundingComponent(propsBoundComp);
    auto& propsMat = m_ecsWorld.EmplaceComponent<MaterialComponent>(m_propsEntity,
        1.0f, 1.0f, 0.45f, -0.35f, BlendMode::Opaque, true, true);
#if defined(__WIIU__) || defined(__wiiu__)
    propsMat.textureHandle = m_hPropsTexture;
#endif

    // 2.3 Entidad Alpha: Cristales translúcidos de ventanas y follaje
    m_alphaEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_alphaEntity, centerOffset);
    auto& alphaMeshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_alphaEntity);
    m_alphaMesh.PopulateMeshComponent(alphaMeshComp);
    auto& alphaBoundComp = m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_alphaEntity);
    m_alphaMesh.PopulateBoundingComponent(alphaBoundComp);
    auto& alphaMat = m_ecsWorld.EmplaceComponent<MaterialComponent>(m_alphaEntity,
        1.0f, 1.0f, 0.25f, -0.35f, BlendMode::AlphaCutout, true, true);
#if defined(__WIIU__) || defined(__wiiu__)
    alphaMat.textureHandle = m_hAlphaTexture;
#endif

    // 2.4 Entidades del Pedestal de Estudio y Sombra de Contacto
    CreateStudioPedestalMeshes();

    // Pedestal cilíndrico de estudio en (0, 0, 0)
    m_pedestalEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_pedestalEntity, Point3D{0.0f, 0.0f, 0.0f});
    auto& pedMeshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_pedestalEntity);
    m_pedestalMesh.PopulateMeshComponent(pedMeshComp);
    auto& pedBoundComp = m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_pedestalEntity);
    m_pedestalMesh.PopulateBoundingComponent(pedBoundComp);
    auto& pedMat = m_ecsWorld.EmplaceComponent<MaterialComponent>(m_pedestalEntity,
        1.0f, 1.0f, 0.20f, -0.35f, BlendMode::Opaque, true, true);
#if defined(__WIIU__) || defined(__wiiu__)
    pedMat.textureHandle = m_hPedestalTexture;
#endif

    // Plano de Sombra de Contacto Suave (AlphaBlend) a ras de suelo
    m_shadowEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_shadowEntity, Point3D{0.0f, 0.0f, 0.0f});
    auto& shMeshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_shadowEntity);
    m_shadowMesh.PopulateMeshComponent(shMeshComp);
    auto& shBoundComp = m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_shadowEntity);
    m_shadowMesh.PopulateBoundingComponent(shBoundComp);
    auto& shMat = m_ecsWorld.EmplaceComponent<MaterialComponent>(m_shadowEntity,
        1.0f, 1.0f, 0.0f, -0.35f, BlendMode::AlphaBlend, true, false);
#if defined(__WIIU__) || defined(__wiiu__)
    shMat.textureHandle = m_hShadowTexture;
#endif

    // 2.5 Entidad Cámara Orbital de Estudio
    m_camTarget = Point3D{0.0f, 2.55f, 0.0f};
    m_targetCamTarget = m_camTarget;
    m_camYaw = 0.65f;
    m_targetCamYaw = 0.65f;
    m_camPitch = 0.28f;
    m_targetCamPitch = 0.28f;
    m_camDistance = 10.5f;
    m_targetCamDistance = 10.5f;

    float cosP = std::cos(m_camPitch);
    float sinP = std::sin(m_camPitch);
    float cosY = std::cos(m_camYaw);
    float sinY = std::sin(m_camYaw);
    Point3D initialCamPos{
        m_camTarget.x + m_camDistance * sinY * cosP,
        m_camTarget.y + m_camDistance * sinP,
        m_camTarget.z - m_camDistance * cosY * cosP
    };

    m_cameraEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_cameraEntity, initialCamPos);
    m_ecsWorld.EmplaceComponent<CameraComponent>(m_cameraEntity,
        55.0f, 1280.0f / 720.0f, 0.20f, 100.0f, m_camTarget, DisplayTarget::TV);
    m_ecsWorld.EmplaceComponent<StudioTurntableComponent>(m_cameraEntity,
        m_camYaw, m_camPitch, m_camDistance, m_camTarget, m_autoRotate);
    // [OPT-321] Elastic Spring-Arm de cámara contra colisión de suelo y obstáculos
    m_ecsWorld.EmplaceComponent<SpringArmComponent>(m_cameraEntity,
        m_camDistance, 0.35f, 1.8f, 40.0f, 0.25f);

    // 3. Paso inicial de sistemas ECS
    StudioTurntableSystem::Update(m_ecsWorld, 0.001f);
    SpringArmSystem::Update(m_ecsWorld, 0.001f);
    TransformSystem::Update(m_ecsWorld);
    CameraSystem::Update(m_ecsWorld);
    if (const auto* tvCam = m_ecsWorld.GetComponent<CameraComponent>(m_cameraEntity)) {
        CullingSystem::Update(m_ecsWorld, *tvCam, 1280.0f, 720.0f);
    }
}

void SceneStaticModelsGX2::OnExit() {
    m_ecsWorld.Clear();

    m_stagedHouseMesh.clear();
    m_stagedHouseMesh.shrink_to_fit();
    m_stagedPropsMesh.clear();
    m_stagedPropsMesh.shrink_to_fit();
    m_stagedAlphaMesh.clear();
    m_stagedAlphaMesh.shrink_to_fit();
    m_asyncStaged = false;

    m_houseMesh.Destroy();
    m_propsMesh.Destroy();
    m_alphaMesh.Destroy();
    m_pedestalMesh.Destroy();
    m_shadowMesh.Destroy();
    m_modelsLoaded = false;
    m_pedestalCreated = false;

    DestroyGX2Pipeline();
    DestroyModelTextures();
}

void SceneStaticModelsGX2::Update(float dt) {
    m_animTime += dt;
    if (m_dayNightAuto) {
        m_dayNightTime += dt;
        if (m_dayNightTime >= DAYNIGHT_CYCLE_DURATION) {
            m_dayNightTime -= DAYNIGHT_CYCLE_DURATION;
        }
    }

    // 1. Telemetría de FPS
    uint32_t currentTicks = SDL_GetTicks();
    uint32_t deltaTicks = currentTicks - m_lastFrameTicks;
    m_lastFrameTicks = currentTicks;
    if (deltaTicks > 0) {
        float instantFps = 1000.0f / static_cast<float>(deltaTicks);
        m_smoothedFps = m_smoothedFps * 0.92f + instantFps * 0.08f;
    }

    // 2. Ergonomía táctil GamePad
    auto& input = InputManager::Instance();
    const auto& touch = input.GetTouchState();

    if (touch.active) {
        float tx = static_cast<float>(touch.x);
        float ty = static_cast<float>(touch.y);

        // Control orbital en el área central del Viewport 3D (Y in [65, 625])
        if (ty >= 65.0f && ty <= 625.0f) {
            if (touch.justPressed) {
                m_isDraggingCamera = true;
                m_dragStartX = tx;
                m_dragStartY = ty;
                m_initialYaw = m_targetCamYaw;
                m_initialPitch = m_targetCamPitch;
            } else if (m_isDraggingCamera) {
                float dx = tx - m_dragStartX;
                float dy = ty - m_dragStartY;
                m_targetCamYaw = m_initialYaw + dx * 0.0055f;
                m_targetCamPitch = std::clamp(m_initialPitch + dy * 0.0045f, -0.35f, 1.35f);
            }
        }

        if (touch.justPressed) {
            // Botón 1: < VOLVER
            if (s_btnVolver.Contains(tx, ty)) {
                AudioManager::Instance().PlayClick();
                SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
                return;
            }
            // Botón 2: OPCIONES GLOBALES (Jason Gregory Cap. 5.3)
            if (s_btnOpciones.Contains(tx, ty)) {
                AudioManager::Instance().PlayClick();
                SceneManager::Instance().ChangeScene(std::make_unique<SceneOptions>([]() {
                    SceneManager::Instance().ChangeScene(std::make_unique<SceneStaticModelsGX2>());
                }));
                return;
            }
            // Botón 3: RELIEVE/BUMP
            if (s_btnBump.Contains(tx, ty)) {
                m_bumpPresetIndex = (m_bumpPresetIndex + 1) % NUM_BUMP_PRESETS;
                AudioManager::Instance().PlayClick();
            }
            // Botón 4: SUELO/SOMBRA
            if (s_btnSombra.Contains(tx, ty)) {
                m_showPedestal = !m_showPedestal;
                EngineConfig::Instance().graphics.showPedestal = m_showPedestal;
                AudioManager::Instance().PlayClick();
            }
            // Botón 5: GIRO AUTO
            if (s_btnGiro.Contains(tx, ty)) {
                m_autoRotate = !m_autoRotate;
                AudioManager::Instance().PlayClick();
            }
            // Botón 6: PROPS
            if (s_btnProps.Contains(tx, ty)) {
                m_showProps = !m_showProps;
                AudioManager::Instance().PlayClick();
            }
            // Botón 7: CICLO DÍA Y NOCHE (1 min por etapa: Amanecer -> Mediodía -> Atardecer -> Noche)
            if (s_btnLuz.Contains(tx, ty)) {
                int curStage = static_cast<int>(m_dayNightTime / DAYNIGHT_STAGE_DURATION) % NUM_DAYNIGHT_STAGES;
                int nextStage = (curStage + 1) % NUM_DAYNIGHT_STAGES;
                m_dayNightTime = static_cast<float>(nextStage) * DAYNIGHT_STAGE_DURATION;
                AudioManager::Instance().PlayClick();
            }
            // Botón 8: PRUEBA DE ESTRÉS (VILLA / CLONES)
            if (s_btnEstres.Contains(tx, ty)) {
                m_stressPresetIndex = (m_stressPresetIndex + 1) % NUM_STRESS_PRESETS;
                m_targetCamDistance = STRESS_PRESETS[m_stressPresetIndex].defaultCamDist;
                AudioManager::Instance().PlayClick();
            }
            if (s_btnZoomIn.Contains(tx, ty)) {
                m_targetCamDistance = std::clamp(m_targetCamDistance - 1.5f, 2.5f, 40.0f);
                AudioManager::Instance().PlayClick();
            }
            if (s_btnZoomOut.Contains(tx, ty)) {
                m_targetCamDistance = std::clamp(m_targetCamDistance + 1.5f, 2.5f, 40.0f);
                AudioManager::Instance().PlayClick();
            }
        }
    } else {
        m_isDraggingCamera = false;
    }

    // 4. Controles físicos con mando (Sticks, Gatillos y Botones)
    const auto stickL = input.GetLeftStick();
    const auto stickR = input.GetRightStick();

    if (std::abs(stickR.x) > 0.05f || std::abs(stickR.y) > 0.05f) {
        m_targetCamYaw += stickR.x * (1.8f * dt);
        m_targetCamPitch = std::clamp(m_targetCamPitch + stickR.y * (1.4f * dt), -0.35f, 1.35f);
    }
    if (std::abs(stickL.x) > 0.05f) {
        m_targetCamYaw += stickL.x * (1.5f * dt);
    }
    if (std::abs(stickL.y) > 0.05f) {
        m_targetCamDistance = std::clamp(m_targetCamDistance - stickL.y * (8.5f * dt), 2.5f, 40.0f);
    }

#if defined(__WIIU__) || defined(__wiiu__)
    if (input.IsButtonHeld(VPAD_BUTTON_UP) || input.IsButtonHeld(WPAD_PRO_BUTTON_UP)) {
        m_targetCamTarget.y = std::clamp(m_targetCamTarget.y + 3.5f * dt, 0.4f, 6.0f);
    }
    if (input.IsButtonHeld(VPAD_BUTTON_DOWN) || input.IsButtonHeld(WPAD_PRO_BUTTON_DOWN)) {
        m_targetCamTarget.y = std::clamp(m_targetCamTarget.y - 3.5f * dt, 0.4f, 6.0f);
    }
    if (input.IsButtonHeld(VPAD_BUTTON_ZL) || input.IsButtonHeld(WPAD_PRO_BUTTON_ZL)) {
        m_targetCamDistance = std::clamp(m_targetCamDistance - 8.5f * dt, 2.5f, 40.0f);
    }
    if (input.IsButtonHeld(VPAD_BUTTON_ZR) || input.IsButtonHeld(WPAD_PRO_BUTTON_ZR)) {
        m_targetCamDistance = std::clamp(m_targetCamDistance + 8.5f * dt, 2.5f, 40.0f);
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_STICK_R) || input.IsButtonTriggered(WPAD_PRO_BUTTON_STICK_R)) {
        m_stressPresetIndex = (m_stressPresetIndex + 1) % NUM_STRESS_PRESETS;
        m_targetCamDistance = STRESS_PRESETS[m_stressPresetIndex].defaultCamDist;
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_MINUS) || input.IsButtonTriggered(WPAD_PRO_BUTTON_MINUS)) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneOptions>([]() {
            SceneManager::Instance().ChangeScene(std::make_unique<SceneStaticModelsGX2>());
        }));
        return;
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_R) || input.IsButtonTriggered(WPAD_PRO_BUTTON_R)) {
        m_resPresetIndex = (m_resPresetIndex + 1) % NUM_RES_PRESETS;
        EngineConfig::Instance().graphics.resolution = static_cast<ResolutionMode>(m_resPresetIndex);
        UpdateSamplerFilter();
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_L) || input.IsButtonTriggered(WPAD_PRO_BUTTON_L)) {
        m_resPresetIndex = (m_resPresetIndex + NUM_RES_PRESETS - 1) % NUM_RES_PRESETS;
        EngineConfig::Instance().graphics.resolution = static_cast<ResolutionMode>(m_resPresetIndex);
        UpdateSamplerFilter();
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_RIGHT) || input.IsButtonTriggered(WPAD_PRO_BUTTON_RIGHT)) {
        m_filterPresetIndex = (m_filterPresetIndex + 1) % NUM_FILTER_PRESETS;
        EngineConfig::Instance().graphics.textureFilter = static_cast<TextureFilterMode>(m_filterPresetIndex);
        UpdateSamplerFilter();
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_LEFT) || input.IsButtonTriggered(WPAD_PRO_BUTTON_LEFT)) {
        m_filterPresetIndex = (m_filterPresetIndex + NUM_FILTER_PRESETS - 1) % NUM_FILTER_PRESETS;
        EngineConfig::Instance().graphics.textureFilter = static_cast<TextureFilterMode>(m_filterPresetIndex);
        UpdateSamplerFilter();
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_UP) || input.IsButtonTriggered(WPAD_PRO_BUTTON_UP)) {
        m_bumpPresetIndex = (m_bumpPresetIndex + 1) % NUM_BUMP_PRESETS;
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_DOWN) || input.IsButtonTriggered(WPAD_PRO_BUTTON_DOWN)) {
        m_showPedestal = !m_showPedestal;
        EngineConfig::Instance().graphics.showPedestal = m_showPedestal;
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_X) || input.IsButtonTriggered(WPAD_PRO_BUTTON_X)) {
        m_clayMode = !m_clayMode;
        EngineConfig::Instance().graphics.renderMode = m_clayMode ? ShadingRenderMode::Clay : ShadingRenderMode::Textured;
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_Y) || input.IsButtonTriggered(WPAD_PRO_BUTTON_Y)) {
        m_targetCamTarget = Point3D{0.0f, 2.55f, 0.0f};
        m_targetCamYaw = 0.65f;
        m_targetCamPitch = 0.28f;
        m_targetCamDistance = 10.5f;
        if (auto* arm = m_ecsWorld.GetComponent<SpringArmComponent>(m_cameraEntity)) {
            arm->targetArmLength = 10.5f;
        }
        AudioManager::Instance().PlayClick();
    }
#endif

    // 5. Actualización desacoplada vía Sistema ECS StudioTurntable (OPT-302 / OPT-303)
    if (auto* turntable = m_ecsWorld.GetComponent<StudioTurntableComponent>(m_cameraEntity)) {
        turntable->autoRotate = m_autoRotate;
        turntable->isDragging = m_isDraggingCamera;
        turntable->targetYaw = m_targetCamYaw;
        turntable->targetPitch = m_targetCamPitch;
        turntable->targetDistance = m_targetCamDistance;
        turntable->targetTarget = m_targetCamTarget;
    }

    StudioTurntableSystem::Update(m_ecsWorld, dt);
    // [OPT-321] Barrido de colisión esférico y amortiguamiento elástico de cámara
    SpringArmSystem::Update(m_ecsWorld, dt);

    if (const auto* turntable = m_ecsWorld.GetComponent<StudioTurntableComponent>(m_cameraEntity)) {
        m_camYaw = turntable->yaw;
        m_camPitch = turntable->pitch;
        m_camDistance = turntable->distance;
        m_camTarget = turntable->target;
        m_targetCamYaw = turntable->targetYaw;
    }

    const auto& resPreset = RES_PRESETS[m_resPresetIndex];
    float vpW = static_cast<float>(resPreset.width);
    float vpH = static_cast<float>(resPreset.height);

    if (auto* camComp = m_ecsWorld.GetComponent<CameraComponent>(m_cameraEntity)) {
        camComp->aspectRatio = vpW / vpH;
        camComp->MarkDirty();
    }

    // 6. Actualizar sistemas ECS (Transform, Camera y Culling secuencial en Core 1)
    TransformSystem::Update(m_ecsWorld);
    CameraSystem::Update(m_ecsWorld);
    if (const auto* tvCam = m_ecsWorld.GetComponent<CameraComponent>(m_cameraEntity)) {
        CullingSystem::Update(m_ecsWorld, *tvCam, vpW, vpH);
    }

    // 7. Retorno con botón físico B
    if (input.IsBackRequested()) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
    }
}

void SceneStaticModelsGX2::Render(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_shaderLoaded || !m_texturesCreated || !m_depthBufferCreated) {
        if (!InitGX2Pipeline()) return;
    }

    BindColorBuffer(renderer);
    RenderScene3D_GX2();
    RenderHUD_GX2();
    RestoreSDL2GX2State(renderer);
#else
    (void)renderer;
#endif
}

void SceneStaticModelsGX2::RenderScene3D_GX2() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_nativeContextState || !m_pColorBuffer) return;

    GX2SetContextState(m_nativeContextState);
    GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    if (m_depthBufferCreated) {
        GX2SetDepthBuffer(&m_depthBuffer);
    }

    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);

    // Fondo 100% Negro Puro en todo el Color Buffer 1280x720
    GX2ClearColor(m_pColorBuffer, 0.0f, 0.0f, 0.0f, 1.0f);
    if (m_depthBufferCreated) {
        GX2ClearDepthStencilEx(&m_depthBuffer, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
    }

    const auto& resPreset = RES_PRESETS[m_resPresetIndex];
    // [OPT-429] Siempre renderizar a pantalla completa 1280x720 nativo (sin letterboxing ni encogimiento)
    // aplicando escalado continuo de hardware de Wii U y adaptando el bias LOD de texturas según el preset.
    GX2SetContextState(m_nativeContextState);
    GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    if (m_depthBufferCreated) {
        GX2SetDepthBuffer(&m_depthBuffer);
    }
    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);

    if (!m_useDirectGFD) {
        GX2SetFetchShader(&m_shaderGroup.fetchShader);
        GX2SetVertexShader(m_shaderGroup.vertexShader);
        GX2SetPixelShader(m_shaderGroup.pixelShader);
    } else {
        GX2SetFetchShader(&m_directFs);
        GX2SetVertexShader(m_directVs);
        GX2SetPixelShader(m_directPs);
    }

    GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, FALSE,
                         GX2_POLYGON_MODE_TRIANGLE, GX2_POLYGON_MODE_TRIANGLE,
                         TRUE, TRUE, FALSE);

    RHIPipelineCache::Instance().Invalidate();

    const auto* tvCam = m_ecsWorld.GetComponent<CameraComponent>(m_cameraEntity);
    const auto* tvTrans = m_ecsWorld.GetComponent<TransformComponent>(m_cameraEntity);
    if (!tvCam || !tvTrans || !m_modelsLoaded) {
        GX2Flush();
        return;
    }

    Point3D camPos = tvTrans->position;
    Matrix4x4 vpMatrix = tvCam->viewProjMatrix;
    // [OPT-325] Evaluación del Ciclo de Día y Noche (1 minuto por etapa, transición suave de 12s)
    float cycleTime = m_dayNightTime;
    while (cycleTime < 0.0f) cycleTime += DAYNIGHT_CYCLE_DURATION;
    while (cycleTime >= DAYNIGHT_CYCLE_DURATION) cycleTime -= DAYNIGHT_CYCLE_DURATION;

    int curStageIdx = static_cast<int>(cycleTime / DAYNIGHT_STAGE_DURATION) % NUM_DAYNIGHT_STAGES;
    int nextStageIdx = (curStageIdx + 1) % NUM_DAYNIGHT_STAGES;
    float timeInStage = std::fmod(cycleTime, DAYNIGHT_STAGE_DURATION);

    // Transición suave (Hermite) en los últimos 12 segundos del minuto
    float t = 0.0f;
    if (timeInStage >= 48.0f) {
        float blend = (timeInStage - 48.0f) / 12.0f;
        t = blend * blend * (3.0f - 2.0f * blend);
    }

    const auto& sCur = DAYNIGHT_STAGES[curStageIdx];
    const auto& sNext = DAYNIGHT_STAGES[nextStageIdx];

    Point3D lightDir = (sCur.dir.Normalized() * (1.0f - t) + sNext.dir.Normalized() * t).Normalized();
    float lightR = (static_cast<float>(sCur.color.r) * (1.0f - t) + static_cast<float>(sNext.color.r) * t) / 255.0f;
    float lightG = (static_cast<float>(sCur.color.g) * (1.0f - t) + static_cast<float>(sNext.color.g) * t) / 255.0f;
    float lightB = (static_cast<float>(sCur.color.b) * (1.0f - t) + static_cast<float>(sNext.color.b) * t) / 255.0f;
    float intensity = sCur.intensity * (1.0f - t) + sNext.intensity * t;

    alignas(256) float vsUniforms[32];
    alignas(256) float psUniforms[20];

    psUniforms[0] = camPos.x; psUniforms[1] = camPos.y; psUniforms[2] = camPos.z; psUniforms[3] = m_animTime;
    psUniforms[4] = lightDir.x; psUniforms[5] = lightDir.y; psUniforms[6] = lightDir.z; psUniforms[7] = 1.0f;
    psUniforms[8] = lightR * intensity;
    psUniforms[9] = lightG * intensity;
    psUniforms[10] = lightB * intensity;
    psUniforms[11] = 1.0f;
    // uWindParams.z = 0.0f desactiva sombras de nubes para iluminación limpia de estudio
    psUniforms[12] = 0.0f; psUniforms[13] = 0.0f; psUniforms[14] = 0.0f; psUniforms[15] = 0.1f;

    GX2SetPixelSampler(&m_sampler, 0);

    const auto& stressPreset = STRESS_PRESETS[m_stressPresetIndex];
    int gridDim = stressPreset.gridDim;
    float spacing = stressPreset.spacing;
    float halfExt = (static_cast<float>(gridDim) - 1.0f) * 0.5f;

    auto drawModelEntityAt = [&](Entity ent, bool forceDisableCull, float offX, float offZ) {
        if (!m_ecsWorld.IsAlive(ent)) return;
        const auto* mesh = m_ecsWorld.GetComponent<MeshComponent>(ent);
        if (!mesh || !mesh->vbo || !mesh->ibo || !mesh->isVisible) return;

        const auto* mat = m_ecsWorld.GetComponent<MaterialComponent>(ent);
        if (mat) {
            RHIPipelineState state = RHIPipelineState::FromMaterial(*mat);
            if (forceDisableCull) {
                state.cullEnable = false;
            }
            RHIPipelineCache::Instance().BindState(state);
            psUniforms[16] = mat->materialMode;
            psUniforms[17] = mat->uvTiling;
            // customParam: en modelos de estudio (< 0.0), el valor absoluto es el factor de bump analítico.
            // Para mallas alpha o sombra de contacto, se desactiva el bump mapping para evitar perturbaciones.
            float bumpStrength = (ent == m_alphaEntity || ent == m_shadowEntity) ? 0.0f : BUMP_PRESETS[m_bumpPresetIndex].scale;
            psUniforms[18] = -bumpStrength;
            psUniforms[19] = m_clayMode ? 0.40f : mat->roughness;
        } else {
            RHIPipelineCache::Instance().BindState(RHIPipelineState(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, !forceDisableCull));
            psUniforms[16] = 1.0f;
            psUniforms[17] = 1.0f;
            float bumpStrength = (ent == m_alphaEntity || ent == m_shadowEntity) ? 0.0f : BUMP_PRESETS[m_bumpPresetIndex].scale;
            psUniforms[18] = -bumpStrength;
            psUniforms[19] = 0.50f;
        }
        GX2SetPixelUniformReg(0, 20, psUniforms);

        ResourceHandle<GX2Texture> texHandle = m_clayMode
            ? m_hClayTexture
            : ((mat && mat->textureHandle.IsValid()) ? mat->textureHandle : m_hClayTexture);
        if (const GX2Texture* tex = m_texturePool.Get(texHandle)) {
            GX2SetPixelTexture(tex, 0);
        }

        const auto* transform = m_ecsWorld.GetComponent<TransformComponent>(ent);
        Matrix4x4 baseModel = transform ? transform->worldMatrix : Matrix4x4::Identity();
        Matrix4x4 model = (offX == 0.0f && offZ == 0.0f)
            ? baseModel
            : (Matrix4x4::Translation(offX, 0.0f, offZ) * baseModel);

        Matrix4x4 mvp = vpMatrix * model;
        std::memcpy(&vsUniforms[0], mvp.m, 16 * sizeof(float));
        std::memcpy(&vsUniforms[16], model.m, 16 * sizeof(float));
        GX2SetVertexUniformReg(0, 32, vsUniforms);

        GX2RSetAttributeBuffer(static_cast<GX2RBuffer*>(mesh->vbo), 0, sizeof(MeshVertex), 0);
        GX2RDrawIndexed(static_cast<GX2PrimitiveMode>(mesh->primitiveMode),
                        static_cast<GX2RBuffer*>(mesh->ibo),
                        (mesh->indexType == 0) ? GX2_INDEX_TYPE_U16 : GX2_INDEX_TYPE_U32,
                        mesh->indexCount, 0, 0, 1);
    };

    // Dibujar cuadrícula de instancias (Prueba de Estrés GPU Latte GX2)
    for (int gz = 0; gz < gridDim; ++gz) {
        float offZ = (static_cast<float>(gz) - halfExt) * spacing;
        for (int gx = 0; gx < gridDim; ++gx) {
            float offX = (static_cast<float>(gx) - halfExt) * spacing;

            // 1. Submalla Principal: Casa Holandesa (Opaca)
            drawModelEntityAt(m_houseEntity, true, offX, offZ);

            // 2. Submallas Secundarias: Props y Cristales/Follaje Alpha
            if (m_showProps) {
                drawModelEntityAt(m_propsEntity, true, offX, offZ);
                drawModelEntityAt(m_alphaEntity, true, offX, offZ);
            }

            // 3. Pedestal Cilíndrico de Estudio y Sombra de Contacto Suave (OPT-309)
            if (m_showPedestal && m_pedestalCreated) {
                drawModelEntityAt(m_pedestalEntity, false, offX, offZ);
                drawModelEntityAt(m_shadowEntity, true, offX, offZ);
            }
        }
    }

    // Restaurar viewport/scissor completo 1280x720 antes del HUD 2D
    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);
    GX2Flush();
#endif
}

void SceneStaticModelsGX2::RenderHUD_GX2() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_pColorBuffer) return;

    // [OPT-312] Aislamiento de estado de silicio GPU mediante ScopedUIPass
    // (Jason Gregory, Game Engine Architecture 3rd Ed., Cap. 14.5.3: Render State Hygiene)
    UI::ScopedUIPass uiPass(m_nativeContextState, m_pColorBuffer);

    const auto& resPreset = RES_PRESETS[m_resPresetIndex];
    const auto& filterPreset = FILTER_PRESETS[m_filterPresetIndex];

    // =========================================================================
    // BARRA SUPERIOR DE CONTROLES (Y in [8, 56], H = 48)
    // =========================================================================
    UI::UIToolkit::DrawPanel(s_topBarPanel);

    // Botón 1: < VOLVER
    UI::UIToolkit::DrawButton(s_btnVolver, "< VOLVER", false, UI::TextScale::Normal);

    // Botón 2: OPCIONES (Jason Gregory Cap. 5.3)
    UI::UIToolkit::DrawButton(s_btnOpciones, "OPCIONES", false, UI::TextScale::Normal);

    // Botón 3: RELIEVE/BUMP
    const char* compactBumpLabel = "BUMP:OFF";
    switch (m_bumpPresetIndex) {
        case 0: compactBumpLabel = "BUMP:OFF"; break;
        case 1: compactBumpLabel = "BUMP:SUAVE"; break;
        case 2: compactBumpLabel = "BUMP:NORM"; break;
        case 3: compactBumpLabel = "BUMP:FUERT"; break;
        default: break;
    }
    UI::UIToolkit::DrawButton(s_btnBump, compactBumpLabel, false, UI::TextScale::Normal);

    // Botón 4: SUELO/SOMBRA
    UI::UIToolkit::DrawToggleButton(s_btnSombra, m_showPedestal ? "SOMBRA:SI" : "SOMBRA:NO",
                                    m_showPedestal, false, UI::TextScale::Normal);

    // Botón 5: GIRO AUTO
    UI::UIToolkit::DrawToggleButton(s_btnGiro, m_autoRotate ? "GIRO:SI" : "GIRO:NO",
                                    m_autoRotate, false, UI::TextScale::Normal);

    // Botón 6: PROPS
    UI::UIToolkit::DrawToggleButton(s_btnProps, m_showProps ? "PROP:SI" : "PROP:NO",
                                    m_showProps, false, UI::TextScale::Normal);

    // Botón 7: CICLO DÍA Y NOCHE
    int curStage = static_cast<int>(m_dayNightTime / DAYNIGHT_STAGE_DURATION) % NUM_DAYNIGHT_STAGES;
    const char* compactLightLabel = DAYNIGHT_STAGES[curStage].buttonLabel;
    UI::UIToolkit::DrawButton(s_btnLuz, compactLightLabel, false, UI::TextScale::Normal);

    // Botón 8: PRUEBA DE ESTRÉS (VILLA / CLONES)
    UI::UIToolkit::DrawButton(s_btnEstres, STRESS_PRESETS[m_stressPresetIndex].shortLabel, false, UI::TextScale::Normal);

    // Botones Flotantes de Zoom Rápido en Margen Derecho
    UI::UIToolkit::DrawButton(s_btnZoomIn, "ZOOM+", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnZoomOut, "ZOOM-", false, UI::TextScale::Normal);

    // =========================================================================
    // BARRA DE GUÍA DE CONTROLES (Y in [636, 666], H = 30)
    // =========================================================================
    UI::UIToolkit::DrawGuideBar(
        "TOUCH/STICKS: ORBITAR | ZL/ZR: ZOOM | R3: CLONES | D-PAD: BUMP | X: ARCILLA | -: OPCIONES | B: VOLVER",
        636.0f, UI::TextScale::Normal
    );

    // =========================================================================
    // BARRA INFERIOR DE TELEMETRÍA (Y in [672, 708], H = 36)
    // =========================================================================
    const auto& stressPreset = STRESS_PRESETS[m_stressPresetIndex];
    uint32_t numInstances = static_cast<uint32_t>(stressPreset.gridDim * stressPreset.gridDim);
    uint32_t singleVerts = m_houseMesh.GetVertexCount() +
        (m_showProps ? (m_propsMesh.GetVertexCount() + m_alphaMesh.GetVertexCount()) : 0);
    uint32_t singleTris = (m_houseMesh.GetIndexCount() +
        (m_showProps ? (m_propsMesh.GetIndexCount() + m_alphaMesh.GetIndexCount()) : 0)) / 3;

    uint32_t totalVerts = singleVerts * numInstances;
    uint32_t totalTris = singleTris * numInstances;
    uint32_t totalDraws = (1 + (m_showProps ? 2 : 0) + (m_showPedestal ? 2 : 0)) * numInstances;

    int stageSecs = static_cast<int>(std::fmod(m_dayNightTime, DAYNIGHT_STAGE_DURATION));
    StackString<256> telemetria;
    telemetria.Format("FPS: %.1f | DIST: %.1fM | %s (%ds/60s) | CASAS: %u | TRIS: %u | DRAWS: %u | RES: %s | %s",
                      m_smoothedFps, m_camDistance, DAYNIGHT_STAGES[curStage].name, stageSecs, numInstances, totalTris, totalDraws,
                      resPreset.label, filterPreset.desc);
    UI::UIToolkit::DrawTelemetryBar(telemetria.c_str(), 672.0f, UI::TextScale::Normal);
#endif
}

} // namespace WiiU
