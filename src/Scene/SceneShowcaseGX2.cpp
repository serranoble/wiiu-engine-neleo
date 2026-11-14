#include "Scene/SceneShowcaseGX2.hpp"
#include "Scene/SceneMenuModels.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/ResourceMonitor.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Core/StackString.hpp"
#include "Graphics/Animation/AnimationLoader.hpp"
#include "Core/ECS/Systems/AnimationSystem.hpp"
#include "Graphics/SkyboxShaderBinary.hpp"
#include "Graphics/AdvancedMeshShaderBinary.hpp"
#include "Graphics/AdvancedSkelShaderBinary.hpp"
#include "Core/ECS/Systems/StudioTurntableSystem.hpp"
#include "Core/ECS/Systems/SpringArmSystem.hpp"
#include "Core/ECS/Systems/TransformSystem.hpp"
#include "Core/ECS/Systems/CameraSystem.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include "Graphics/GX2StateHelper.hpp"

#if defined(__WIIU__) || defined(__wiiu__)
#include <proc_ui/procui.h>
#include <vpad/input.h>
#include <padscore/wpad.h>
#endif

namespace WiiU {

static inline uint32_t ReadBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           (static_cast<uint32_t>(p[3]));
}

static inline uint16_t ReadBE16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) |
           (static_cast<uint16_t>(p[1]));
}

// Layout interactivo de la UI
static const UI::UIRect s_topBarPanel{0.0f, 0.0f, 1280.0f, 60.0f};
static const UI::UIRect s_btnVolverTop{20.0f, 12.0f, 110.0f, 36.0f};

static const UI::UIRect s_panelInfo{20.0f, 68.0f, 330.0f, 175.0f};
static const UI::UIRect s_panelTelem{930.0f, 68.0f, 330.0f, 175.0f};

static const UI::UIRect s_btnZoomIn{1180.0f, 255.0f, 80.0f, 44.0f};
static const UI::UIRect s_btnZoomOut{1180.0f, 308.0f, 80.0f, 44.0f};

static const UI::UIRect s_bottomBarPanel{0.0f, 580.0f, 1280.0f, 52.0f};
static const UI::UIRect s_btnNormalMap{15.0f, 586.0f, 155.0f, 40.0f};
static const UI::UIRect s_btnReflection{178.0f, 586.0f, 140.0f, 40.0f};
static const UI::UIRect s_btnClipSalto{326.0f, 586.0f, 105.0f, 40.0f};
static const UI::UIRect s_btnClipPiar{439.0f, 586.0f, 105.0f, 40.0f};
static const UI::UIRect s_btnClipMirar{552.0f, 586.0f, 105.0f, 40.0f};
static const UI::UIRect s_btnClipCompleta{665.0f, 586.0f, 105.0f, 40.0f};
static const UI::UIRect s_btnPause{778.0f, 586.0f, 105.0f, 40.0f};
static const UI::UIRect s_btnClay{891.0f, 586.0f, 105.0f, 40.0f};
static const UI::UIRect s_btnResetCam{1004.0f, 586.0f, 125.0f, 40.0f};
static const UI::UIRect s_btnVolverBot{1137.0f, 586.0f, 125.0f, 40.0f};

SceneShowcaseGX2::SceneShowcaseGX2() = default;

SceneShowcaseGX2::~SceneShowcaseGX2() {
    DestroyGX2Pipeline();
}

void SceneShowcaseGX2::LoadAsync() {
    // 1. Carga asíncrona de mallas y texturas en Core 2
    AssetManager::LoadBinaryFile("models/skydome.wmesh", m_stagedSkydomeMesh);
    AssetManager::LoadBinaryFile("models/skybox_day.wcube", m_stagedCubeMap);
    AssetManager::LoadBinaryFile("models/dutch_house.wmesh", m_stagedHouseMesh);
    AssetManager::LoadBinaryFile("models/dutch_house_props.wmesh", m_stagedPropsMesh);
    AssetManager::LoadBinaryFile("models/ground_plane.wmesh", m_stagedGroundMesh);
    AssetManager::LoadBinaryFile("models/bird_orange.wmesh", m_stagedBirdMesh);
    AssetManager::LoadBinaryFile("models/bird_orange.wskel", m_stagedBirdSkel);
    AssetManager::LoadBinaryFile("models/bird_orange.wanim", m_stagedBirdAnim);

    // 2. Parsear esqueleto y animación del pájaro
    AnimationLoader::LoadSkeleton("models/bird_orange.wskel", m_skeleton);
    AnimationLoader::LoadClips("models/bird_orange.wanim", m_animationClips);

    // 3. Crear texturas de forma asíncrona en Core 2
    CreateTextures();
}

bool SceneShowcaseGX2::CreateTextures() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_texturesCreated) return true;

    bool okCube  = LoadCookedCubeMap("models/skybox_day.wcube");
    bool okSky   = LoadCookedTexture("models/skydome.wtex", m_hSkydomeTexture, "Tex_Skydome");
    bool okHouse = LoadCookedTexture("models/dutch_house.wtex", m_hHouseTexture, "Tex_DutchHouse");
    bool okProps = LoadCookedTexture("models/dutch_house_props.wtex", m_hPropsTexture, "Tex_DutchProps");
    bool okBird  = LoadCookedTexture("models/bird_orange.wtex", m_hBirdTexture, "Tex_BirdOrange");
    bool okGrass = LoadCookedTexture("models/ground_grass.wtex", m_hGrassTexture, "Tex_GroundGrass");

    if (okCube && okSky && okHouse && okProps && okBird && okGrass) {
        m_texturesCreated = true;
        return true;
    }
    return false;
#else
    return true;
#endif
}

bool SceneShowcaseGX2::LoadCookedCubeMap(std::string_view filepath) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_skyboxTextureCreated) return true;

    std::vector<uint8_t> buffer;
    if (!m_stagedCubeMap.empty()) {
        buffer = std::move(m_stagedCubeMap);
    } else {
        if (!AssetManager::LoadBinaryFile(std::string(filepath), buffer) || buffer.size() < 32) {
            return false;
        }
    }

    uint32_t magic = ReadBE32(buffer.data());
    uint16_t faceSize = ReadBE16(buffer.data() + 6);
    uint32_t totalDataSize = ReadBE32(buffer.data() + 8);

    if (magic != 0x57435542U || faceSize == 0 || totalDataSize == 0 || buffer.size() < (32 + totalDataSize)) {
        return false;
    }

    std::memset(&m_skyboxTexture, 0, sizeof(GX2Texture));
    m_skyboxTexture.surface.dim = GX2_SURFACE_DIM_TEXTURE_CUBE;
    m_skyboxTexture.surface.width = faceSize;
    m_skyboxTexture.surface.height = faceSize;
    m_skyboxTexture.surface.depth = 6; // En GX2, un CubeMap posee 6 caras (depth = 6)
    m_skyboxTexture.surface.mipLevels = 1;
    m_skyboxTexture.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    m_skyboxTexture.surface.aa = GX2_AA_MODE1X;
    m_skyboxTexture.surface.use = GX2_SURFACE_USE_TEXTURE;
    m_skyboxTexture.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    GX2CalcSurfaceSizeAndAlignment(&m_skyboxTexture.surface);

    // Salvaguarda robusta: garantizar memoria contigua para las 6 caras completas
    uint32_t facePitchBytes = m_skyboxTexture.surface.pitch * 4;
    uint32_t faceSliceBytes = facePitchBytes * faceSize;
    uint32_t totalRequiredBytes = faceSliceBytes * 6;
    if (m_skyboxTexture.surface.imageSize < totalRequiredBytes) {
        m_skyboxTexture.surface.imageSize = totalRequiredBytes;
    }

    uint32_t align = m_skyboxTexture.surface.alignment > 256 ? m_skyboxTexture.surface.alignment : 256;
    m_skyboxTexture.surface.image = MemoryManager::Instance().AllocAligned(
        align,
        m_skyboxTexture.surface.imageSize,
        MemoryTag::Texture,
        "Tex_SkyboxCube"
    );

    if (!m_skyboxTexture.surface.image) return false;

    const uint8_t* pSrc = buffer.data() + 32;
    uint8_t* pDst = static_cast<uint8_t*>(m_skyboxTexture.surface.image);
    uint32_t faceRawBytes = faceSize * faceSize * 4;

    for (uint32_t face = 0; face < 6; ++face) {
        uint8_t* faceDst = pDst + face * faceSliceBytes;
        const uint8_t* faceSrc = pSrc + face * faceRawBytes;
        for (uint32_t y = 0; y < faceSize; ++y) {
            std::memcpy(faceDst + y * facePitchBytes, faceSrc + y * (faceSize * 4), faceSize * 4);
        }
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, m_skyboxTexture.surface.image, m_skyboxTexture.surface.imageSize);

    m_skyboxTexture.viewFirstMip = 0;
    m_skyboxTexture.viewNumMips = 1;
    m_skyboxTexture.viewFirstSlice = 0;
    m_skyboxTexture.viewNumSlices = 6;
    m_skyboxTexture.compMap = 0x00010203;
    GX2InitTextureRegs(&m_skyboxTexture);

    m_skyboxTextureCreated = true;
    return true;
#else
    (void)filepath;
    return true;
#endif
}

bool SceneShowcaseGX2::LoadCookedTexture(std::string_view filepath,
                                         ResourceHandle<GX2Texture>& outHandle,
                                         const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    std::vector<uint8_t> buffer;
    if (!AssetManager::LoadBinaryFile(std::string(filepath), buffer) || buffer.size() < 16) {
        return false;
    }

    // Validar cabecera Big-Endian 'WTEX' (0x57544558)
    uint32_t magic = ReadBE32(buffer.data());
    uint32_t width = ReadBE32(buffer.data() + 4);
    uint32_t height = ReadBE32(buffer.data() + 8);
    uint32_t rawMipAndFmt = ReadBE32(buffer.data() + 12);
    uint32_t fmtCode = (rawMipAndFmt >> 16) & 0xFFFFu;
    uint32_t rawMipLevels = rawMipAndFmt & 0xFFFFu;
    if (fmtCode == 0) fmtCode = 1;
    uint32_t mipLevels = (rawMipLevels >= 1 && rawMipLevels <= 13) ? rawMipLevels : 1;

    const bool isBC1 = (fmtCode == 2);
    const bool isBC3 = (fmtCode == 3);
    const bool isBlockCompressed = isBC1 || isBC3;
    const uint32_t blockBytes = isBC1 ? 8u : (isBC3 ? 16u : 4u);

    uint32_t blocksW0 = isBlockCompressed ? ((width + 3u) >> 2) : width;
    uint32_t blocksH0 = isBlockCompressed ? ((height + 3u) >> 2) : height;
    size_t baseBytes = static_cast<size_t>(blocksW0) * static_cast<size_t>(blocksH0) * static_cast<size_t>(blockBytes);

    if (magic != 0x57544558U || width == 0 || height == 0 || width > 2048 || height > 2048 || buffer.size() < (16ULL + baseBytes)) {
        return false;
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
        return false;
    }

    // Copiar Nivel Base (Mip 0) respetando el pitch de hardware
    uint8_t* pImage = static_cast<uint8_t*>(tex->surface.image);
    const uint8_t* srcCursor = buffer.data() + 16;
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
    (void)filepath; (void)outHandle; (void)debugLabel;
    return true;
#endif
}

bool SceneShowcaseGX2::InitGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_skyShaderLoaded && m_meshShaderLoaded && m_skelShaderLoaded &&
        m_birdBuffersCreated && m_depthBufferCreated && m_nativeContextState) {
        return true;
    }

    // 0. Cargar Shader de Skydome Hemisférico (doom_sky11)
    if (!m_skyShaderLoaded) {
        std::vector<uint8_t> shaderData;
        bool loaded = AssetManager::LoadBinaryFile("shaders/skydome_gx2.gsh", shaderData);
        if (!loaded || shaderData.empty()) {
            shaderData.assign(SKYBOX_GX2_GSH_DATA, SKYBOX_GX2_GSH_DATA + SKYBOX_GX2_GSH_SIZE);
            loaded = true;
        }

        if (loaded && !shaderData.empty()) {
            uint32_t vsHdrSize = GFDGetVertexShaderHeaderSize(0, shaderData.data());
            uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
            uint32_t psHdrSize = GFDGetPixelShaderHeaderSize(0, shaderData.data());
            uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());

            m_skyVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "Sky_VS_Hdr"));
            m_skyVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "Sky_VS_Prog");
            m_skyPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHdrSize, MemoryTag::ShaderProgram, "Sky_PS_Hdr"));
            m_skyPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "Sky_PS_Prog");

            if (m_skyVs && m_skyVsProgram && m_skyPs && m_skyPsProgram) {
                GFDGetVertexShader(m_skyVs, m_skyVsProgram, 0, shaderData.data());
                GFDGetPixelShader(m_skyPs, m_skyPsProgram, 0, shaderData.data());
                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_skyVsProgram, vsProgSize);
                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_skyPsProgram, psProgSize);

                GX2AttribStream attrs[4];
                attrs[0].location = 0; attrs[0].buffer = 0; attrs[0].offset = 0;
                attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32; attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[0].aluDivisor = 0; attrs[0].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[1].location = 1; attrs[1].buffer = 0; attrs[1].offset = 12;
                attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32; attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[1].aluDivisor = 0; attrs[1].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[2].location = 2; attrs[2].buffer = 0; attrs[2].offset = 24;
                attrs[2].format = GX2_ATTRIB_FORMAT_FLOAT_32_32; attrs[2].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[2].aluDivisor = 0; attrs[2].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                attrs[2].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[3].location = 3; attrs[3].buffer = 0; attrs[3].offset = 32;
                attrs[3].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32; attrs[3].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[3].aluDivisor = 0; attrs[3].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                attrs[3].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                uint32_t fsSize = GX2CalcFetchShaderSizeEx(4, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                m_skyFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "Sky_FS_Prog");
                if (m_skyFsProgram) {
                    GX2InitFetchShaderEx(&m_skyFs, static_cast<uint8_t*>(m_skyFsProgram), 4, attrs,
                                         GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_skyFsProgram, fsSize);
                    m_skyShaderLoaded = true;
                }
            }
        }
    }

    // 1. Cargar Shader de Mallas Avanzadas (Casa con Normal Mapping y Reflexión)
    if (!m_meshShaderLoaded) {
        std::vector<uint8_t> shaderData;
        bool loaded = AssetManager::LoadBinaryFile("shaders/advanced_mesh_gx2.gsh", shaderData);
        if (!loaded || shaderData.empty()) {
            shaderData.assign(ADVANCED_MESH_GX2_GSH_DATA, ADVANCED_MESH_GX2_GSH_DATA + ADVANCED_MESH_GX2_GSH_SIZE);
            loaded = true;
        }

        if (loaded && !shaderData.empty()) {
            uint32_t vsHdrSize = GFDGetVertexShaderHeaderSize(0, shaderData.data());
            uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
            uint32_t psHdrSize = GFDGetPixelShaderHeaderSize(0, shaderData.data());
            uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());

            m_meshVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "Mesh_VS_Hdr"));
            m_meshVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "Mesh_VS_Prog");
            m_meshPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHdrSize, MemoryTag::ShaderProgram, "Mesh_PS_Hdr"));
            m_meshPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "Mesh_PS_Prog");

            if (m_meshVs && m_meshVsProgram && m_meshPs && m_meshPsProgram) {
                GFDGetVertexShader(m_meshVs, m_meshVsProgram, 0, shaderData.data());
                GFDGetPixelShader(m_meshPs, m_meshPsProgram, 0, shaderData.data());
                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_meshVsProgram, vsProgSize);
                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_meshPsProgram, psProgSize);

                GX2AttribStream attrs[4];
                attrs[0].location = 0; attrs[0].buffer = 0; attrs[0].offset = 0;
                attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32; attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[0].aluDivisor = 0; attrs[0].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[1].location = 1; attrs[1].buffer = 0; attrs[1].offset = 12;
                attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32; attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[1].aluDivisor = 0; attrs[1].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[2].location = 2; attrs[2].buffer = 0; attrs[2].offset = 24;
                attrs[2].format = GX2_ATTRIB_FORMAT_FLOAT_32_32; attrs[2].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[2].aluDivisor = 0; attrs[2].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                attrs[2].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[3].location = 3; attrs[3].buffer = 0; attrs[3].offset = 32;
                attrs[3].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32; attrs[3].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[3].aluDivisor = 0; attrs[3].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                attrs[3].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                uint32_t fsSize = GX2CalcFetchShaderSizeEx(4, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                m_meshFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "Mesh_FS_Prog");
                if (m_meshFsProgram) {
                    GX2InitFetchShaderEx(&m_meshFs, static_cast<uint8_t*>(m_meshFsProgram), 4, attrs,
                                         GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_meshFsProgram, fsSize);
                    m_meshShaderLoaded = true;
                }
            }
        }
    }

    // 3. Cargar Shader de Mallas Esqueléticas (Pájaro Animado con Reflexión)
    if (!m_skelShaderLoaded) {
        std::vector<uint8_t> shaderData;
        bool loaded = AssetManager::LoadBinaryFile("shaders/advanced_skel_gx2.gsh", shaderData);
        if (!loaded || shaderData.empty()) {
            shaderData.assign(ADVANCED_SKEL_GX2_GSH_DATA, ADVANCED_SKEL_GX2_GSH_DATA + ADVANCED_SKEL_GX2_GSH_SIZE);
            loaded = true;
        }

        if (loaded && !shaderData.empty()) {
            uint32_t vsHdrSize = GFDGetVertexShaderHeaderSize(0, shaderData.data());
            uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
            uint32_t psHdrSize = GFDGetPixelShaderHeaderSize(0, shaderData.data());
            uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());

            m_skelVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "Skel_VS_Hdr"));
            m_skelVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "Skel_VS_Prog");
            m_skelPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHdrSize, MemoryTag::ShaderProgram, "Skel_PS_Hdr"));
            m_skelPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "Skel_PS_Prog");

            if (m_skelVs && m_skelVsProgram && m_skelPs && m_skelPsProgram) {
                GFDGetVertexShader(m_skelVs, m_skelVsProgram, 0, shaderData.data());
                GFDGetPixelShader(m_skelPs, m_skelPsProgram, 0, shaderData.data());
                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_skelVsProgram, vsProgSize);
                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_skelPsProgram, psProgSize);

                GX2AttribStream attrs[6];
                attrs[0].location = 0; attrs[0].buffer = 0; attrs[0].offset = offsetof(SkinnedMeshVertex, pos);
                attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32; attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[0].aluDivisor = 0; attrs[0].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[1].location = 1; attrs[1].buffer = 0; attrs[1].offset = offsetof(SkinnedMeshVertex, normal);
                attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32; attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[1].aluDivisor = 0; attrs[1].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[2].location = 2; attrs[2].buffer = 0; attrs[2].offset = offsetof(SkinnedMeshVertex, texCoord);
                attrs[2].format = GX2_ATTRIB_FORMAT_FLOAT_32_32; attrs[2].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[2].aluDivisor = 0; attrs[2].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                attrs[2].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[3].location = 3; attrs[3].buffer = 0; attrs[3].offset = offsetof(SkinnedMeshVertex, color);
                attrs[3].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32; attrs[3].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[3].aluDivisor = 0; attrs[3].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                attrs[3].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[4].location = 4; attrs[4].buffer = 0; attrs[4].offset = offsetof(SkinnedMeshVertex, boneIndices);
                attrs[4].format = GX2_ATTRIB_FORMAT_UINT_8_8_8_8; attrs[4].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[4].aluDivisor = 0; attrs[4].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                attrs[4].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[5].location = 5; attrs[5].buffer = 0; attrs[5].offset = offsetof(SkinnedMeshVertex, boneWeights);
                attrs[5].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32; attrs[5].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[5].aluDivisor = 0; attrs[5].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                attrs[5].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                uint32_t fsSize = GX2CalcFetchShaderSizeEx(6, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                m_skelFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "Skel_FS_Prog");
                if (m_skelFsProgram) {
                    GX2InitFetchShaderEx(&m_skelFs, static_cast<uint8_t*>(m_skelFsProgram), 6, attrs,
                                         GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_skelFsProgram, fsSize);
                    m_skelShaderLoaded = true;
                }
            }
        }
    }

    // 4. Subir buffers VBO/IBO del pájaro
    if (!m_birdBuffersCreated && !m_stagedBirdMesh.empty() && m_stagedBirdMesh.size() >= 96) {
        const auto* hdr = reinterpret_cast<const MeshHeader*>(m_stagedBirdMesh.data());
        m_birdVertexCount = hdr->vertexCount;
        m_birdIndexCount = hdr->indexCount;

        m_birdVBO.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ
        );
        m_birdVBO.elemSize = hdr->vertexStride;
        m_birdVBO.elemCount = hdr->vertexCount;
        if (GX2RCreateBuffer(&m_birdVBO)) {
            void* vboPtr = GX2RLockBufferEx(&m_birdVBO, GX2R_RESOURCE_BIND_NONE);
            if (vboPtr) {
                std::memcpy(vboPtr, m_stagedBirdMesh.data() + hdr->vboOffset, hdr->vboSize);
                GX2RUnlockBufferEx(&m_birdVBO, GX2R_RESOURCE_BIND_NONE);
            }
        }

        m_birdIBO.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_INDEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ
        );
        m_birdIBO.elemSize = sizeof(uint16_t);
        m_birdIBO.elemCount = hdr->indexCount;
        if (GX2RCreateBuffer(&m_birdIBO)) {
            void* iboPtr = GX2RLockBufferEx(&m_birdIBO, GX2R_RESOURCE_BIND_NONE);
            if (iboPtr) {
                std::memcpy(iboPtr, m_stagedBirdMesh.data() + hdr->iboOffset, hdr->iboSize);
                GX2RUnlockBufferEx(&m_birdIBO, GX2R_RESOURCE_BIND_NONE);
            }
        }

        m_birdBuffersCreated = true;
    }

    // 5. Inicializar samplers: Skydome (Wrap U, Clamp V) y Mallas (Wrap, Aniso 4x)
    GX2InitSampler(&m_skydomeSampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerClamping(&m_skydomeSampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_CLAMP_MODE_WRAP);
    GX2InitSamplerXYFilter(&m_skydomeSampler, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_ANISO_RATIO_NONE);
    GX2InitSamplerZMFilter(&m_skydomeSampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_LINEAR);

    // Sampler de CubeMap para reflexión ambiental
    GX2InitSampler(&m_skyboxSampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerXYFilter(&m_skyboxSampler, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_ANISO_RATIO_NONE);
    GX2InitSamplerZMFilter(&m_skyboxSampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_LINEAR);

    GX2InitSampler(&m_meshSampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerXYFilter(&m_meshSampler, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_ANISO_RATIO_4_TO_1);
    GX2InitSamplerZMFilter(&m_meshSampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_LINEAR);
    GX2InitSamplerLOD(&m_meshSampler, 0.0f, 10.0f, 0.0f);

    // 6. Crear Depth Buffer en 1280x720
    if (!m_depthBufferCreated) {
        std::memset(&m_depthBuffer, 0, sizeof(GX2DepthBuffer));
        m_depthBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        m_depthBuffer.surface.width = 1280;
        m_depthBuffer.surface.height = 720;
        m_depthBuffer.surface.depth = 1;
        m_depthBuffer.surface.mipLevels = 1;
        m_depthBuffer.surface.format = GX2_SURFACE_FORMAT_FLOAT_D24_S8;
        m_depthBuffer.surface.use = GX2_SURFACE_USE_DEPTH_BUFFER;
        m_depthBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
        m_depthBuffer.viewNumSlices = 1;
        m_depthBuffer.depthClear = 1.0f;
        GX2CalcSurfaceSizeAndAlignment(&m_depthBuffer.surface);

        if (GX2RCreateSurface(&m_depthBuffer.surface, static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_DEPTH_BUFFER | GX2R_RESOURCE_USAGE_GPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ
        ))) {
            GX2InitDepthBufferRegs(&m_depthBuffer);
            GX2InitDepthBufferHiZEnable(&m_depthBuffer, FALSE);
            m_depthBufferCreated = true;
        }
    }

    // 7. Context State
    if (!m_nativeContextState) {
        m_nativeContextState = static_cast<GX2ContextState*>(
            MemoryManager::Instance().AllocAligned(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState),
                                                  MemoryTag::ContextState, "Showcase_ContextState")
        );
        if (m_nativeContextState) {
            GX2SetupContextStateEx(m_nativeContextState, TRUE);
        }
    }

    return m_skyShaderLoaded && m_meshShaderLoaded && m_skelShaderLoaded &&
           m_birdBuffersCreated && m_depthBufferCreated && (m_nativeContextState != nullptr);
#else
    return true;
#endif
}

void SceneShowcaseGX2::DestroyGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIIsRunning() && ProcUIInForeground()) {
        GX2DrawDone();
    }

    m_skydomeMesh.Destroy();
    m_houseMesh.Destroy();
    m_propsMesh.Destroy();

    if (m_skyboxTextureCreated && m_skyboxTexture.surface.image) {
        MemoryManager::Instance().FreeAligned(m_skyboxTexture.surface.image);
        m_skyboxTexture.surface.image = nullptr;
        m_skyboxTextureCreated = false;
    }
    m_groundMesh.Destroy();

    if (m_birdBuffersCreated) {
        GX2RDestroyBufferEx(&m_birdVBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_birdIBO, GX2R_RESOURCE_BIND_NONE);
        m_birdBuffersCreated = false;
    }


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
    freeTexHandle(m_hSkydomeTexture);
    freeTexHandle(m_hHouseTexture);
    freeTexHandle(m_hPropsTexture);
    freeTexHandle(m_hBirdTexture);
    freeTexHandle(m_hGrassTexture);
    m_texturesCreated = false;

    if (m_depthBufferCreated) {
        GX2RDestroySurfaceEx(&m_depthBuffer.surface, GX2R_RESOURCE_BIND_NONE);
        m_depthBufferCreated = false;
    }

    if (m_nativeContextState) {
        void* rawCtx = static_cast<void*>(m_nativeContextState);
        MemoryManager::Instance().FreeAligned(rawCtx);
        m_nativeContextState = nullptr;
    }

    // Liberar shaders
    if (m_skyVs) { void* raw = static_cast<void*>(m_skyVs); MemoryManager::Instance().Free(raw); m_skyVs = nullptr; }
    if (m_skyVsProgram) { MemoryManager::Instance().FreeAligned(m_skyVsProgram); m_skyVsProgram = nullptr; }
    if (m_skyPs) { void* raw = static_cast<void*>(m_skyPs); MemoryManager::Instance().Free(raw); m_skyPs = nullptr; }
    if (m_skyPsProgram) { MemoryManager::Instance().FreeAligned(m_skyPsProgram); m_skyPsProgram = nullptr; }
    if (m_skyFsProgram) { MemoryManager::Instance().FreeAligned(m_skyFsProgram); m_skyFsProgram = nullptr; }
    m_skyShaderLoaded = false;
if (m_meshVs) { void* raw = static_cast<void*>(m_meshVs); MemoryManager::Instance().Free(raw); m_meshVs = nullptr; }
    if (m_meshVsProgram) { MemoryManager::Instance().FreeAligned(m_meshVsProgram); m_meshVsProgram = nullptr; }
    if (m_meshPs) { void* raw = static_cast<void*>(m_meshPs); MemoryManager::Instance().Free(raw); m_meshPs = nullptr; }
    if (m_meshPsProgram) { MemoryManager::Instance().FreeAligned(m_meshPsProgram); m_meshPsProgram = nullptr; }
    if (m_meshFsProgram) { MemoryManager::Instance().FreeAligned(m_meshFsProgram); m_meshFsProgram = nullptr; }
    m_meshShaderLoaded = false;

    if (m_skelVs) { void* raw = static_cast<void*>(m_skelVs); MemoryManager::Instance().Free(raw); m_skelVs = nullptr; }
    if (m_skelVsProgram) { MemoryManager::Instance().FreeAligned(m_skelVsProgram); m_skelVsProgram = nullptr; }
    if (m_skelPs) { void* raw = static_cast<void*>(m_skelPs); MemoryManager::Instance().Free(raw); m_skelPs = nullptr; }
    if (m_skelPsProgram) { MemoryManager::Instance().FreeAligned(m_skelPsProgram); m_skelPsProgram = nullptr; }
    if (m_skelFsProgram) { MemoryManager::Instance().FreeAligned(m_skelFsProgram); m_skelFsProgram = nullptr; }
    m_skelShaderLoaded = false;
#endif
}

void SceneShowcaseGX2::OnEnter() {
    AudioManager::Instance().PlayBGM();
    m_clayMode = (EngineConfig::Get().graphics.renderMode == ShadingRenderMode::Clay);

    // 1. Cargar mallas y animaciones si LoadAsync no se ejecutó
    if (m_skeleton.jointCount == 0) {
        AnimationLoader::LoadSkeleton("models/bird_orange.wskel", m_skeleton);
        AnimationLoader::LoadClips("models/bird_orange.wanim", m_animationClips);
    }
    if (m_stagedBirdMesh.empty()) {
        AssetManager::LoadBinaryFile("models/bird_orange.wmesh", m_stagedBirdMesh);
    }

    // 2. Cargar Mallas Estáticas
    if (!m_stagedSkydomeMesh.empty()) {
        m_skydomeMesh.LoadFromMemory(m_stagedSkydomeMesh.data(), m_stagedSkydomeMesh.size());
    } else {
        m_skydomeMesh.LoadFromFile("models/skydome.wmesh");
    }

    if (!m_stagedHouseMesh.empty()) {
        m_houseMesh.LoadFromMemory(m_stagedHouseMesh.data(), m_stagedHouseMesh.size());
    } else {
        m_houseMesh.LoadFromFile("models/dutch_house.wmesh");
    }

    if (!m_stagedPropsMesh.empty()) {
        m_propsMesh.LoadFromMemory(m_stagedPropsMesh.data(), m_stagedPropsMesh.size());
    } else {
        m_propsMesh.LoadFromFile("models/dutch_house_props.wmesh");
    }

    if (!m_stagedGroundMesh.empty()) {
        m_groundMesh.LoadFromMemory(m_stagedGroundMesh.data(), m_stagedGroundMesh.size());
    } else {
        m_groundMesh.LoadFromFile("models/ground_plane.wmesh");
    }

    // 3. Crear texturas (si LoadAsync no las creó previamente)
    CreateTextures();

    // 4. Inicializar Pipeline GX2
    InitGX2Pipeline();

    // 5. Configurar Entidades en ECSWorld
    m_ecsWorld.Clear();

    // 5.1 Entidad Plano de Terreno Continuo (césped leafy grass)
    m_groundEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_groundEntity, Point3D{0.0f, 0.0f, 0.0f});
    auto& groundMeshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_groundEntity);
    m_groundMesh.PopulateMeshComponent(groundMeshComp);
    auto& groundBoundComp = m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_groundEntity);
    m_groundMesh.PopulateBoundingComponent(groundBoundComp);

    // 5.3 Entidad Casa Holandesa y Tejado/Props (posicionada a la derecha en plano medio-fondo)
    m_houseEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_houseEntity, Point3D{2.2f, 0.0f, -0.8f});
    auto& houseMeshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_houseEntity);
    m_houseMesh.PopulateMeshComponent(houseMeshComp);
    auto& houseBoundComp = m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_houseEntity);
    m_houseMesh.PopulateBoundingComponent(houseBoundComp);

    m_propsEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_propsEntity, Point3D{2.2f, 0.0f, -0.8f});
    auto& propsMeshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_propsEntity);
    m_propsMesh.PopulateMeshComponent(propsMeshComp);
    auto& propsBoundComp = m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_propsEntity);
    m_propsMesh.PopulateBoundingComponent(propsBoundComp);

    // 5.4 Entidad Pájaro Naranja (escala 1.0x, primer plano a la izquierda separado de la casa)
    m_birdEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(
        m_birdEntity,
        Point3D{-4.2f, 0.0f, 1.8f},
        Point3D{0.0f, 0.35f, 0.0f},
        Point3D{1.0f, 1.0f, 1.0f}
    );
    auto& birdMeshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_birdEntity);
#if defined(__WIIU__) || defined(__wiiu__)
    birdMeshComp.vbo = &m_birdVBO;
    birdMeshComp.ibo = &m_birdIBO;
    birdMeshComp.indexCount = m_birdIndexCount;
    birdMeshComp.vertexCount = m_birdVertexCount;
    birdMeshComp.vertexStride = SKINNED_VERTEX_STRIDE;
    birdMeshComp.primitiveMode = GX2_PRIMITIVE_MODE_TRIANGLES;
#endif
    m_ecsWorld.EmplaceComponent<SkeletalMeshComponent>(m_birdEntity, &m_skeleton);
    const AnimationClip* initClip = (!m_animationClips.empty()) ? &m_animationClips[0] : nullptr;
    m_ecsWorld.EmplaceComponent<AnimatorComponent>(m_birdEntity, initClip, true, 1.0f);
    m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_birdEntity, Point3D{0.0f, 1.4f, 0.0f}, 2.4f);

    // 5.5 Cámara orbital con Spring-Arm (centro baricéntrico entre casa y pájaro)
    m_camTarget = Point3D{-1.0f, 1.5f, 0.5f};
    m_targetCamTarget = m_camTarget;
    m_camYaw = 0.55f;
    m_targetCamYaw = 0.55f;
    m_camPitch = 0.20f;
    m_targetCamPitch = 0.20f;
    m_camDistance = 11.5f;
    m_targetCamDistance = 11.5f;

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
    m_ecsWorld.EmplaceComponent<CameraComponent>(m_cameraEntity, 55.0f, 1280.0f / 720.0f, 0.20f, 1000.0f, m_camTarget, DisplayTarget::TV);
    m_ecsWorld.EmplaceComponent<StudioTurntableComponent>(m_cameraEntity, m_camYaw, m_camPitch, m_camDistance, m_camTarget, false);
    m_ecsWorld.EmplaceComponent<SpringArmComponent>(m_cameraEntity, m_camDistance, 0.35f, 3.0f, 35.0f, 0.25f);

    // 5.6 Paso inicial de sistemas ECS para pre-calcular matrices antes del primer fotograma
    StudioTurntableSystem::Update(m_ecsWorld, 0.001f);
    SpringArmSystem::Update(m_ecsWorld, 0.001f);
    TransformSystem::Update(m_ecsWorld);
    CameraSystem::Update(m_ecsWorld);

    // 5.0 Entidad Skydome Hemisférico
    m_skydomeEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_skydomeEntity, Point3D{0.0f, 0.0f, 0.0f});
    auto& skyMeshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_skydomeEntity);
    m_skydomeMesh.PopulateMeshComponent(skyMeshComp);

    // Liberar búferes temporales de staging
    m_stagedSkydomeMesh.clear(); m_stagedSkydomeMesh.shrink_to_fit();
    m_stagedCubeMap.clear(); m_stagedCubeMap.shrink_to_fit();
    m_stagedHouseMesh.clear(); m_stagedHouseMesh.shrink_to_fit();
    m_stagedPropsMesh.clear(); m_stagedPropsMesh.shrink_to_fit();
    m_stagedGroundMesh.clear(); m_stagedGroundMesh.shrink_to_fit();
    m_stagedBirdMesh.clear(); m_stagedBirdMesh.shrink_to_fit();
    m_stagedBirdSkel.clear(); m_stagedBirdSkel.shrink_to_fit();
    m_stagedBirdAnim.clear(); m_stagedBirdAnim.shrink_to_fit();
}

void SceneShowcaseGX2::OnExit() {
    m_ecsWorld.Clear();
    m_skydomeMesh.Destroy();
    m_houseMesh.Destroy();
    m_propsMesh.Destroy();
    m_propsMesh.Destroy();

    if (m_skyboxTextureCreated && m_skyboxTexture.surface.image) {
        MemoryManager::Instance().FreeAligned(m_skyboxTexture.surface.image);
        m_skyboxTexture.surface.image = nullptr;
        m_skyboxTextureCreated = false;
    }
    m_groundMesh.Destroy();
}

void SceneShowcaseGX2::SelectClip(int index) {
    if (m_animationClips.empty()) return;
    int maxClips = static_cast<int>(m_animationClips.size());
    m_activeClipIndex = ((index % maxClips) + maxClips) % maxClips;
    if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_birdEntity)) {
        anim->Play(&m_animationClips[m_activeClipIndex], true);
        if (m_isPaused) {
            anim->Resume();
            m_isPaused = false;
        }
    }
}

void SceneShowcaseGX2::Update(float dt) {
    // 1. Telemetría de FPS
    m_fpsTimer += dt;
    ++m_frameCount;
    if (m_fpsTimer >= 0.5f) {
        m_fps = static_cast<float>(m_frameCount) / m_fpsTimer;
        m_frameTimeMs = (m_fps > 0.0f) ? (1000.0f / m_fps) : 16.6f;
        m_frameCount = 0;
        m_fpsTimer = 0.0f;
    }

    auto& input = InputManager::Instance();
    const auto& touch = input.GetTouchState();

    // 2. Control Físico con GamePad
#if defined(__WIIU__) || defined(__wiiu__)
    if (input.IsButtonTriggered(VPAD_BUTTON_A) || input.IsButtonTriggered(WPAD_PRO_BUTTON_A)) {
        SelectClip(0); // 1: Salto
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_X) || input.IsButtonTriggered(WPAD_PRO_BUTTON_X)) {
        SelectClip(1); // 2: Piar
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_Y) || input.IsButtonTriggered(WPAD_PRO_BUTTON_Y)) {
        SelectClip(2); // 3: Mirar
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_B) || input.IsButtonTriggered(WPAD_PRO_BUTTON_B)) {
        SelectClip(3); // 4: Completa
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_L) || input.IsButtonTriggered(WPAD_PRO_BUTTON_L)) {
        m_normalMapEnabled = !m_normalMapEnabled;
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_R) || input.IsButtonTriggered(WPAD_PRO_BUTTON_R)) {
        m_reflectionModeIndex = (m_reflectionModeIndex + 1) % 4;
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_PLUS) || input.IsButtonTriggered(WPAD_PRO_BUTTON_PLUS)) {
        m_clayMode = !m_clayMode;
        EngineConfig::Instance().graphics.renderMode = m_clayMode ? ShadingRenderMode::Clay : ShadingRenderMode::Textured;
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_MINUS) || input.IsButtonTriggered(WPAD_PRO_BUTTON_MINUS)) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
        return;
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_STICK_R)) {
        m_targetCamTarget = Point3D{-1.0f, 1.5f, 0.5f};
        m_targetCamYaw = 0.55f;
        m_targetCamPitch = 0.20f;
        m_targetCamDistance = 11.5f;
        if (auto* arm = m_ecsWorld.GetComponent<SpringArmComponent>(m_cameraEntity)) {
            arm->targetArmLength = 11.5f;
        }
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_STICK_L)) {
        m_isPaused = !m_isPaused;
        if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_birdEntity)) {
            if (m_isPaused) anim->Pause(); else anim->Resume();
        }
        AudioManager::Instance().PlayClick();
    }
    // Control de Zoom continuo con ZL / ZR y D-Pad Arriba / Abajo
    if (input.IsButtonHeld(VPAD_BUTTON_ZL) || input.IsButtonHeld(WPAD_PRO_BUTTON_ZL)) {
        m_targetCamDistance = std::clamp(m_targetCamDistance - 8.5f * dt, 2.0f, 35.0f);
    }
    if (input.IsButtonHeld(VPAD_BUTTON_ZR) || input.IsButtonHeld(WPAD_PRO_BUTTON_ZR)) {
        m_targetCamDistance = std::clamp(m_targetCamDistance + 8.5f * dt, 2.0f, 35.0f);
    }
    if (input.IsButtonHeld(VPAD_BUTTON_UP) || input.IsButtonHeld(WPAD_PRO_BUTTON_UP)) {
        m_targetCamDistance = std::clamp(m_targetCamDistance - 8.5f * dt, 2.0f, 35.0f);
    }
    if (input.IsButtonHeld(VPAD_BUTTON_DOWN) || input.IsButtonHeld(WPAD_PRO_BUTTON_DOWN)) {
        m_targetCamDistance = std::clamp(m_targetCamDistance + 8.5f * dt, 2.0f, 35.0f);
    }
#endif

    // 3. Control Táctil
    if (touch.active) {
        float tx = touch.x;
        float ty = touch.y;

        if (ty >= 65.0f && ty <= 570.0f) {
            if (touch.justPressed) {
                m_isDraggingCamera = true;
                m_dragStartX = tx;
                m_dragStartY = ty;
                m_initialYaw = m_targetCamYaw;
                m_initialPitch = m_targetCamPitch;
            } else if (m_isDraggingCamera) {
                float dx = tx - m_dragStartX;
                float dy = ty - m_dragStartY;
                m_targetCamYaw = m_initialYaw - dx * 0.006f;
                m_targetCamPitch = std::clamp(m_initialPitch + dy * 0.006f, -0.25f, 1.25f);
            }
        }

        if (touch.justPressed) {
            if (s_btnVolverTop.Contains(tx, ty) || s_btnVolverBot.Contains(tx, ty)) {
                AudioManager::Instance().PlayClick();
                SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
                return;
            }
            if (s_btnNormalMap.Contains(tx, ty)) {
                m_normalMapEnabled = !m_normalMapEnabled;
                AudioManager::Instance().PlayClick();
            }
            if (s_btnReflection.Contains(tx, ty)) {
                m_reflectionModeIndex = (m_reflectionModeIndex + 1) % 4;
                AudioManager::Instance().PlayClick();
            }
            if (s_btnClipSalto.Contains(tx, ty)) {
                SelectClip(0);
                AudioManager::Instance().PlayClick();
            }
            if (s_btnClipPiar.Contains(tx, ty)) {
                SelectClip(1);
                AudioManager::Instance().PlayClick();
            }
            if (s_btnClipMirar.Contains(tx, ty)) {
                SelectClip(2);
                AudioManager::Instance().PlayClick();
            }
            if (s_btnClipCompleta.Contains(tx, ty)) {
                SelectClip(3);
                AudioManager::Instance().PlayClick();
            }
            if (s_btnPause.Contains(tx, ty)) {
                m_isPaused = !m_isPaused;
                if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_birdEntity)) {
                    if (m_isPaused) anim->Pause(); else anim->Resume();
                }
                AudioManager::Instance().PlayClick();
            }
            if (s_btnClay.Contains(tx, ty)) {
                m_clayMode = !m_clayMode;
                EngineConfig::Instance().graphics.renderMode = m_clayMode ? ShadingRenderMode::Clay : ShadingRenderMode::Textured;
                AudioManager::Instance().PlayClick();
            }
            if (s_btnZoomIn.Contains(tx, ty)) {
                m_targetCamDistance = std::clamp(m_targetCamDistance - 1.5f, 2.0f, 35.0f);
                if (auto* arm = m_ecsWorld.GetComponent<SpringArmComponent>(m_cameraEntity)) {
                    arm->targetArmLength = m_targetCamDistance;
                }
                AudioManager::Instance().PlayClick();
            }
            if (s_btnZoomOut.Contains(tx, ty)) {
                m_targetCamDistance = std::clamp(m_targetCamDistance + 1.5f, 2.0f, 35.0f);
                if (auto* arm = m_ecsWorld.GetComponent<SpringArmComponent>(m_cameraEntity)) {
                    arm->targetArmLength = m_targetCamDistance;
                }
                AudioManager::Instance().PlayClick();
            }
            if (s_btnResetCam.Contains(tx, ty)) {
                m_targetCamTarget = Point3D{-1.0f, 1.5f, 0.5f};
                m_targetCamYaw = 0.55f;
                m_targetCamPitch = 0.20f;
                m_targetCamDistance = 11.5f;
                if (auto* arm = m_ecsWorld.GetComponent<SpringArmComponent>(m_cameraEntity)) {
                    arm->targetArmLength = 11.5f;
                }
                AudioManager::Instance().PlayClick();
            }
        }
    } else {
        m_isDraggingCamera = false;
    }

    // 4. Control orbital con sticks analógicos
    const auto stickL = input.GetLeftStick();
    const auto stickR = input.GetRightStick();
    if (std::abs(stickR.x) > 0.05f) m_targetCamYaw += stickR.x * dt * 2.2f;
    if (std::abs(stickR.y) > 0.05f) m_targetCamPitch = std::clamp(m_targetCamPitch + stickR.y * dt * 1.8f, -0.25f, 1.25f);
    if (std::abs(stickL.y) > 0.05f) {
        m_targetCamDistance = std::clamp(m_targetCamDistance - stickL.y * dt * 8.5f, 2.0f, 35.0f);
    }

    // 5. Actualizar sistemas ECS
    AnimationSystem::Update(m_ecsWorld, dt);

    if (auto* turntable = m_ecsWorld.GetComponent<StudioTurntableComponent>(m_cameraEntity)) {
        turntable->autoRotate = m_autoRotate;
        turntable->isDragging = m_isDraggingCamera;
        turntable->targetYaw = m_targetCamYaw;
        turntable->targetPitch = m_targetCamPitch;
        turntable->targetDistance = m_targetCamDistance;
        turntable->targetTarget = m_targetCamTarget;
    }

    StudioTurntableSystem::Update(m_ecsWorld, dt);
    SpringArmSystem::Update(m_ecsWorld, dt);

    if (const auto* turntable = m_ecsWorld.GetComponent<StudioTurntableComponent>(m_cameraEntity)) {
        m_camYaw = turntable->yaw;
        m_camPitch = turntable->pitch;
        m_camDistance = turntable->distance;
        m_camTarget = turntable->target;
        m_targetCamYaw = turntable->targetYaw;
    }

    TransformSystem::Update(m_ecsWorld);
    CameraSystem::Update(m_ecsWorld);
}

void SceneShowcaseGX2::BindColorBuffer(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    m_pColorBuffer = GetSDL2ColorBuffer(renderer);
#else
    (void)renderer;
#endif
}

void SceneShowcaseGX2::RenderScene3D_GX2() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_nativeContextState || !m_pColorBuffer || !m_skyShaderLoaded || !m_meshShaderLoaded || !m_skelShaderLoaded) {
        return;
    }

    GX2SetContextState(m_nativeContextState);
    GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    if (m_depthBufferCreated) {
        GX2SetDepthBuffer(&m_depthBuffer);
    }
    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);

    GX2ClearColor(m_pColorBuffer, 0.52f, 0.75f, 0.91f, 1.0f); // Azul cielo diáfano suave diorama
    if (m_depthBufferCreated) {
        GX2ClearDepthStencilEx(&m_depthBuffer, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
    }

    m_drawCallsCount = 0;
    m_trianglesCount = 0;

    // 1. Obtener matrices de vista y proyección desde CameraComponent
    const auto* camComp = m_ecsWorld.GetComponent<CameraComponent>(m_cameraEntity);
    const auto* camTrans = m_ecsWorld.GetComponent<TransformComponent>(m_cameraEntity);
    Matrix4x4 viewMatrix = camComp ? camComp->viewMatrix : Matrix4x4::Identity();
    Matrix4x4 projMatrix = camComp ? camComp->projMatrix : Matrix4x4::Identity();
    Matrix4x4 vpMatrix = projMatrix * viewMatrix;
    Point3D camPos = camTrans ? camTrans->position : Point3D{0.0f, 2.5f, 8.8f};

    // Parámetros comunes de iluminación
    alignas(256) float psParams[16];
    psParams[0] = camPos.x; psParams[1] = camPos.y; psParams[2] = camPos.z; psParams[3] = 0.0f; // uCamPosTime
    psParams[4] = 0.60f;    psParams[5] = 0.55f;    psParams[6] = 0.58f;    psParams[7] = 1.0f; // uLightDir (Luz solar cálida)
    psParams[8] = 1.0f;     psParams[9] = 0.96f;    psParams[10] = 0.88f;  psParams[11] = 32.0f; // uLightColor + shininess
        static const float s_reflectionLevels[4] = {0.0f, 0.25f, 0.50f, 1.0f};
    float currentRefl = s_reflectionLevels[m_reflectionModeIndex & 3];
    psParams[12] = currentRefl; // uMaterialParams.x = reflectionIntensity modulado por Alpha BC3
    psParams[13] = m_normalMapEnabled ? 1.0f : 0.0f; // uMaterialParams.y = normalMapEnabled
    psParams[14] = 2.0f;    // uMaterialParams.z = bumpScale
    psParams[15] = 0.38f;   // uMaterialParams.w = ambientIntensity

    // =========================================================================
    // PASO 0: SKYDOME HEMISFÉRICO (DOOM_SKY11 A PROFUNDIDAD MÁXIMA Z=W=1.0)
    // =========================================================================
    if (m_skydomeMesh.IsLoaded()) {
        GX2SetFetchShader(&m_skyFs);
        GX2SetVertexShader(m_skyVs);
        GX2SetPixelShader(m_skyPs);

        // Matriz de vista desprovista de traslación para anclaje infinito
        Matrix4x4 skyView = viewMatrix;
        skyView.m[12] = 0.0f;
        skyView.m[13] = 0.0f;
        skyView.m[14] = 0.0f;
        Matrix4x4 skyMVP = projMatrix * skyView;

        alignas(256) float skyUniforms[16];
        std::memcpy(skyUniforms, skyMVP.m, 16 * sizeof(float));
        GX2SetVertexUniformReg(0, 16, skyUniforms);

        if (GX2Texture* skyTex = m_texturePool.Get(m_hSkydomeTexture)) {
            GX2SetPixelTexture(skyTex, 0);
            GX2SetPixelSampler(&m_skydomeSampler, 0);
        }

        // Depth test LEQUAL, depth write inactivo para que el domo actúe de fondo
        RHIPipelineCache::Instance().BindState(RHIPipelineState(true, false, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false));

        const auto* skyMeshComp = m_ecsWorld.GetComponent<MeshComponent>(m_skydomeEntity);
        if (skyMeshComp && skyMeshComp->vbo && skyMeshComp->ibo) {
            GX2RSetAttributeBuffer(static_cast<GX2RBuffer*>(skyMeshComp->vbo), 0, skyMeshComp->vertexStride, 0);
            GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, static_cast<GX2RBuffer*>(skyMeshComp->ibo), GX2_INDEX_TYPE_U16, skyMeshComp->indexCount, 0, 0, 1);
            m_drawCallsCount += 1;
            m_trianglesCount += skyMeshComp->indexCount / 3;
        }
    }

    // =========================================================================
    // PASO 1: PLANO DE TERRENO CONTINUO (CÉSPED LEAFY GRASS CON TILING 40x)
    // =========================================================================
    if (m_groundMesh.IsLoaded()) {
        GX2SetFetchShader(&m_meshFs);
        GX2SetVertexShader(m_meshVs);
        GX2SetPixelShader(m_meshPs);

        const auto* transform = m_ecsWorld.GetComponent<TransformComponent>(m_groundEntity);
        Matrix4x4 model = transform ? transform->worldMatrix : Matrix4x4::Identity();
        Matrix4x4 mvp = vpMatrix * model;

        alignas(256) float vsUniforms[32];
        std::memcpy(&vsUniforms[0], mvp.m, 16 * sizeof(float));
        std::memcpy(&vsUniforms[16], model.m, 16 * sizeof(float));
        GX2SetVertexUniformReg(0, 32, vsUniforms);

        alignas(256) float groundPsParams[16];
        std::memcpy(groundPsParams, psParams, 16 * sizeof(float));
        groundPsParams[12] = 0.0f; // reflectionIntensity = 0.0 para césped
        groundPsParams[14] = 0.8f; // micro-relieve suave
        GX2SetPixelUniformReg(0, 16, groundPsParams);

        if (GX2Texture* grassTex = m_texturePool.Get(m_hGrassTexture)) {
            GX2SetPixelTexture(grassTex, 0);
            GX2SetPixelSampler(&m_meshSampler, 0);
        }

        RHIPipelineCache::Instance().BindState(RHIPipelineState(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false));

        const auto* groundMeshComp = m_ecsWorld.GetComponent<MeshComponent>(m_groundEntity);
        if (groundMeshComp && groundMeshComp->vbo && groundMeshComp->ibo) {
            GX2RSetAttributeBuffer(static_cast<GX2RBuffer*>(groundMeshComp->vbo), 0, groundMeshComp->vertexStride, 0);
            GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, static_cast<GX2RBuffer*>(groundMeshComp->ibo), GX2_INDEX_TYPE_U16, groundMeshComp->indexCount, 0, 0, 1);
            m_drawCallsCount += 1;
            m_trianglesCount += groundMeshComp->indexCount / 3;
        }
    }

    // =========================================================================
    // PASO 2: CASA HOLANDESA ESTÁTICA (.WMESH + NORMAL MAP)
    // =========================================================================
    if (m_houseMesh.IsLoaded()) {
        GX2SetFetchShader(&m_meshFs);
        GX2SetVertexShader(m_meshVs);
        GX2SetPixelShader(m_meshPs);

        const auto* transform = m_ecsWorld.GetComponent<TransformComponent>(m_houseEntity);
        Matrix4x4 model = transform ? transform->worldMatrix : Matrix4x4::Identity();
        Matrix4x4 mvp = vpMatrix * model;

        alignas(256) float vsUniforms[32];
        std::memcpy(&vsUniforms[0], mvp.m, 16 * sizeof(float));
        std::memcpy(&vsUniforms[16], model.m, 16 * sizeof(float));
        GX2SetVertexUniformReg(0, 32, vsUniforms);

        GX2SetPixelUniformReg(0, 16, psParams);

        if (GX2Texture* houseTex = m_texturePool.Get(m_hHouseTexture)) {
            GX2SetPixelTexture(houseTex, 0);
            GX2SetPixelSampler(&m_meshSampler, 0);
        }
        if (m_skyboxTextureCreated) {
            GX2SetPixelTexture(&m_skyboxTexture, 1);
            GX2SetPixelSampler(&m_skyboxSampler, 1);
        }

        RHIPipelineCache::Instance().BindState(RHIPipelineState(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false));

        const auto* houseMeshComp = m_ecsWorld.GetComponent<MeshComponent>(m_houseEntity);
        if (houseMeshComp && houseMeshComp->vbo && houseMeshComp->ibo) {
            GX2RSetAttributeBuffer(static_cast<GX2RBuffer*>(houseMeshComp->vbo), 0, houseMeshComp->vertexStride, 0);
            GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, static_cast<GX2RBuffer*>(houseMeshComp->ibo), GX2_INDEX_TYPE_U16, houseMeshComp->indexCount, 0, 0, 1);
            m_drawCallsCount += 1;
            m_trianglesCount += houseMeshComp->indexCount / 3;
        }

        // Submalla Tejado, Chimeneas y Props Exteriores con su atlas dedicado (dutch_house_props.wtex)
        if (m_propsMesh.IsLoaded()) {
            alignas(256) float propsPsParams[16];
            std::memcpy(propsPsParams, psParams, 16 * sizeof(float));
            propsPsParams[12] = 0.0f; // Tejado y accesorios mate (sin reflexión de cristal)
            GX2SetPixelUniformReg(0, 16, propsPsParams);

            if (GX2Texture* propsTex = m_texturePool.Get(m_hPropsTexture)) {
                GX2SetPixelTexture(propsTex, 0);
                GX2SetPixelSampler(&m_meshSampler, 0);
            }

            const auto* propsMeshComp = m_ecsWorld.GetComponent<MeshComponent>(m_propsEntity);
            if (propsMeshComp && propsMeshComp->vbo && propsMeshComp->ibo) {
                GX2RSetAttributeBuffer(static_cast<GX2RBuffer*>(propsMeshComp->vbo), 0, propsMeshComp->vertexStride, 0);
                GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, static_cast<GX2RBuffer*>(propsMeshComp->ibo), GX2_INDEX_TYPE_U16, propsMeshComp->indexCount, 0, 0, 1);
                m_drawCallsCount += 1;
                m_trianglesCount += propsMeshComp->indexCount / 3;
            }
        }
    }

    // =========================================================================
    // PASO 3: PÁJARO DINÁMICO (.WMESH + LBS 21 HUESOS)
    // =========================================================================
    if (m_birdBuffersCreated) {
        GX2SetFetchShader(&m_skelFs);
        GX2SetVertexShader(m_skelVs);
        GX2SetPixelShader(m_skelPs);

        const auto* transform = m_ecsWorld.GetComponent<TransformComponent>(m_birdEntity);
        Matrix4x4 model = transform ? transform->worldMatrix : Matrix4x4::Identity();
        Matrix4x4 mvp = vpMatrix * model;

        constexpr uint32_t TOTAL_VS_UNIFORMS = 32 + 24 * 16;
        alignas(256) float skelVsUniforms[TOTAL_VS_UNIFORMS];
        std::memcpy(&skelVsUniforms[0], mvp.m, 16 * sizeof(float));
        std::memcpy(&skelVsUniforms[16], model.m, 16 * sizeof(float));

        if (const auto* skelMesh = m_ecsWorld.GetComponent<SkeletalMeshComponent>(m_birdEntity)) {
            if (skelMesh->IsValid()) {
                uint32_t count = std::min(skelMesh->jointCount, 24u);
                for (uint32_t j = 0; j < count; ++j) {
                    std::memcpy(&skelVsUniforms[32 + j * 16], skelMesh->skinningMatrices[j].m, 16 * sizeof(float));
                }
                for (uint32_t j = count; j < 24u; ++j) {
                    std::memcpy(&skelVsUniforms[32 + j * 16], Matrix4x4::Identity().m, 16 * sizeof(float));
                }
            } else {
                for (uint32_t j = 0; j < 24u; ++j) {
                    std::memcpy(&skelVsUniforms[32 + j * 16], Matrix4x4::Identity().m, 16 * sizeof(float));
                }
            }
        }

        GX2SetVertexUniformReg(0, TOTAL_VS_UNIFORMS, skelVsUniforms);

        alignas(256) float birdPsParams[16];
        std::memcpy(birdPsParams, psParams, 16 * sizeof(float));
        birdPsParams[13] = m_clayMode ? 1.0f : 0.0f; // Modo Arcilla para el pájaro
        GX2SetPixelUniformReg(0, 16, birdPsParams);

        if (GX2Texture* birdTex = m_texturePool.Get(m_hBirdTexture)) {
            GX2SetPixelTexture(birdTex, 0);
            GX2SetPixelSampler(&m_meshSampler, 0);
        }
        if (m_skyboxTextureCreated) {
            GX2SetPixelTexture(&m_skyboxTexture, 1);
            GX2SetPixelSampler(&m_skyboxSampler, 1);
        }

        RHIPipelineCache::Instance().BindState(RHIPipelineState(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false));

        GX2RSetAttributeBuffer(&m_birdVBO, 0, SKINNED_VERTEX_STRIDE, 0);
        GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_birdIBO, GX2_INDEX_TYPE_U16, m_birdIndexCount, 0, 0, 1);
        m_drawCallsCount += 1;
        m_trianglesCount += m_birdIndexCount / 3;
    }
#endif
}

void SceneShowcaseGX2::RenderHUD_GX2() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_pColorBuffer) return;

    UI::ScopedUIPass uiPass(m_nativeContextState, m_pColorBuffer);

    // 1. Barra Superior con Título y Volver
    UI::UIToolkit::DrawPanel(s_topBarPanel);
    UI::UIToolkit::DrawButton(s_btnVolverTop, "< VOLVER", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawLabel(140.0f, 22.0f, "SHADING AVANZADO: DIORAMA ESTUDIO + NORMAL MAP + REFLEXION", UI::TextScale::Normal, ColorRGBA::Cyan());

    // 2. Panel Izquierdo: Estado de Materiales y Shading
    UI::UIToolkit::DrawPanel(s_panelInfo);
    UI::UIToolkit::DrawLabel(30.0f, 74.0f, "TECNICAS GORDON & CLEVENGER", UI::TextScale::Normal, ColorRGBA::Yellow());

    StackString<64> normLabel;
    normLabel.Format("CAP. 10 NORMAL MAP: %s", m_normalMapEnabled ? "ACTIVO (TBN dFdx)" : "DESACTIVADO");
    UI::UIToolkit::DrawLabel(30.0f, 104.0f, normLabel.c_str(), UI::TextScale::Normal, m_normalMapEnabled ? ColorRGBA::Green() : ColorRGBA::Orange());

    UI::UIToolkit::DrawLabel(30.0f, 134.0f, "FONDO: SKYDOME: DOOM_SKY11 LIMPIO", UI::TextScale::Normal, ColorRGBA::Cyan());

    const char* clipName = (!m_animationClips.empty() && m_activeClipIndex < static_cast<int>(m_animationClips.size()))
        ? m_animationClips[m_activeClipIndex].name : "SALTO";
    StackString<64> animLabel;
    animLabel.Format("PAJARO: %s | %s", clipName, m_isPaused ? "PAUSA" : "PLAY");
    UI::UIToolkit::DrawLabel(30.0f, 164.0f, animLabel.c_str(), UI::TextScale::Normal, ColorRGBA{255, 200, 100, 255});

    UI::UIToolkit::DrawLabel(30.0f, 194.0f, "FRESNEL SCHLICK (F0=0.04) EN SILICIO", UI::TextScale::Small, ColorRGBA::White());

    // 3. Panel Derecho: Telemetría Hardware
    UI::UIToolkit::DrawPanel(s_panelTelem);
    UI::UIToolkit::DrawLabel(944.0f, 74.0f, "TELEMETRIA SILICIO LATTE", UI::TextScale::Normal, ColorRGBA::Yellow());

    StackString<64> fpsLabel;
    fpsLabel.Format("FPS: %.1f (%.2f ms)", m_fps, m_frameTimeMs);
    UI::UIToolkit::DrawLabel(944.0f, 104.0f, fpsLabel.c_str(), UI::TextScale::Normal, (m_fps >= 58.0f) ? ColorRGBA::Green() : ColorRGBA::Red());

    StackString<64> drawLabel;
    drawLabel.Format("DRAWS: %u | TRIANGULOS: %u", m_drawCallsCount, m_trianglesCount);
    UI::UIToolkit::DrawLabel(944.0f, 134.0f, drawLabel.c_str(), UI::TextScale::Normal, ColorRGBA::White());

    const auto& metrics = ResourceMonitor::Instance().GetMetrics();
    float mem1MB = static_cast<float>(metrics.mem1Bytes) / (1024.0f * 1024.0f);
    float mem2MB = static_cast<float>(metrics.mem2Bytes) / (1024.0f * 1024.0f);
    StackString<64> memLabel;
    memLabel.Format("MEM1: %.1fMB | MEM2: %.1fMB", mem1MB, mem2MB);
    UI::UIToolkit::DrawLabel(944.0f, 164.0f, memLabel.c_str(), UI::TextScale::Normal, ColorRGBA::Cyan());

    UI::UIToolkit::DrawLabel(944.0f, 194.0f, "FONDO: SKYDOME: DOOM_SKY11 GX2", UI::TextScale::Small, ColorRGBA{180, 220, 255, 255});

    StackString<32> distStr;
    distStr.Format("DIST: %.1fM", m_camDistance);
    UI::UIToolkit::DrawLabel(230.0f, 194.0f, distStr.c_str(), UI::TextScale::Small, ColorRGBA::Yellow());

    // Botones Flotantes de Zoom Rápido en Margen Derecho
    UI::UIToolkit::DrawButton(s_btnZoomIn, "ZOOM+", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnZoomOut, "ZOOM-", false, UI::TextScale::Normal);

    // 4. Barra Inferior de Botones Interactivos
    UI::UIToolkit::DrawPanel(s_bottomBarPanel);

    UI::UIToolkit::DrawToggleButton(s_btnNormalMap, m_normalMapEnabled ? "NORMAL:SI" : "NORMAL:NO", m_normalMapEnabled, false, UI::TextScale::Normal);

    static const char* const s_reflLabels[4] = {"REFL: 0%", "REFL: 25%", "REFL: 50%", "REFL:100%"};
    UI::UIToolkit::DrawButton(s_btnReflection, s_reflLabels[m_reflectionModeIndex & 3], m_reflectionModeIndex > 0, UI::TextScale::Normal);

    UI::UIToolkit::DrawButton(s_btnClipSalto, "1:SALTO", m_activeClipIndex == 0, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnClipPiar, "2:PIAR", m_activeClipIndex == 1, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnClipMirar, "3:MIRAR", m_activeClipIndex == 2, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnClipCompleta, "4:TODO", m_activeClipIndex == 3, UI::TextScale::Normal);

    UI::UIToolkit::DrawToggleButton(s_btnPause, m_isPaused ? "PAUSA:SI" : "PAUSA:NO", m_isPaused, false, UI::TextScale::Normal);
    UI::UIToolkit::DrawToggleButton(s_btnClay, m_clayMode ? "CLAY:SI" : "CLAY:NO", m_clayMode, false, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnResetCam, "RESET (R3)", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnVolverBot, "MENU (-)", false, UI::TextScale::Normal);

    // 5. Barra Guía y Telemetría
    UI::UIToolkit::DrawGuideBar("ZL/ZR o D-PAD: ZOOM | STICK L: ZOOM | STICK R: ORBITA | L: NORMAL | A/X/Y/B: CLIPS", 636.0f, UI::TextScale::Normal);

    StackString<256> telemFooter;
    telemFooter.Format("FPS: %.1f | SKYDOME: DOOM_SKY11 | DERIVADAS TBN: ACTIVO | LBS: 21 HUESOS | TOTAL TRIS: %u",
                       m_fps, m_trianglesCount);
    UI::UIToolkit::DrawTelemetryBar(telemFooter.c_str(), 672.0f, UI::TextScale::Normal);
#endif
}

void SceneShowcaseGX2::Render(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_skyShaderLoaded || !m_meshShaderLoaded || !m_skelShaderLoaded ||
        !m_birdBuffersCreated || !m_depthBufferCreated || !m_texturesCreated) {
        if (!InitGX2Pipeline()) return;
    }

    BindColorBuffer(renderer);
    RenderScene3D_GX2();
    RenderHUD_GX2();
#else
    SDL_SetRenderDrawColor(renderer, 25, 30, 45, 255);
    SDL_RenderClear(renderer);
    // Renderizado informativo en PC
#endif
}

} // namespace WiiU
