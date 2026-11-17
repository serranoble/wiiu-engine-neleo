#include "Scene/SceneSkeletalAnimationGX2.hpp"
#include "Scene/SceneMenuModels.hpp"
#include "Scene/SceneManager.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/ResourceMonitor.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/StackString.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/DebugDraw3D.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include "Graphics/GX2StateHelper.hpp"
#include "Graphics/SkinningShaderBinary.hpp"
#include "Graphics/Animation/AnimationLoader.hpp"
#include "Core/ECS/Systems/AnimationSystem.hpp"
#include "Core/ECS/Components/IKRigComponent.hpp"
#include "Core/ECS/Systems/StudioTurntableSystem.hpp"
#include "Core/ECS/Systems/SpringArmSystem.hpp"
#include "Core/ECS/Systems/TransformSystem.hpp"
#include "Core/ECS/Systems/CameraSystem.hpp"
#include "Core/ECS/Systems/CullingSystem.hpp"
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
constexpr UI::UIRect s_topBarPanel{12.0f, 8.0f, 1256.0f, 48.0f};
constexpr UI::UIRect s_btnVolverTop{18.0f, 14.0f, 95.0f, 36.0f};

constexpr UI::UIRect s_panelInfo  {18.0f, 64.0f, 340.0f, 160.0f};
constexpr UI::UIRect s_panelTelem {922.0f, 64.0f, 340.0f, 160.0f};

constexpr UI::UIRect s_btnLayer   {18.0f, 232.0f, 166.0f, 40.0f};
constexpr UI::UIRect s_btnIKMode  {192.0f, 232.0f, 166.0f, 40.0f};

constexpr UI::UIRect s_btnZoomIn  {1180.0f, 240.0f, 80.0f, 44.0f};
constexpr UI::UIRect s_btnZoomOut {1180.0f, 292.0f, 80.0f, 44.0f};

constexpr UI::UIRect s_bottomBarPanel{12.0f, 576.0f, 1256.0f, 52.0f};
constexpr UI::UIRect s_btnIdle    {18.0f, 582.0f, 135.0f, 40.0f};
constexpr UI::UIRect s_btnWalk    {161.0f, 582.0f, 135.0f, 40.0f};
constexpr UI::UIRect s_btnWave    {304.0f, 582.0f, 145.0f, 40.0f};
constexpr UI::UIRect s_btnPause   {457.0f, 582.0f, 140.0f, 40.0f};
constexpr UI::UIRect s_btnSpeed   {605.0f, 582.0f, 130.0f, 40.0f};
constexpr UI::UIRect s_btnClay    {743.0f, 582.0f, 135.0f, 40.0f};
constexpr UI::UIRect s_btnResetCam{886.0f, 582.0f, 145.0f, 40.0f};
constexpr UI::UIRect s_btnVolverBot{1039.0f, 582.0f, 219.0f, 40.0f};
}

SceneSkeletalAnimationGX2::SceneSkeletalAnimationGX2() = default;

SceneSkeletalAnimationGX2::~SceneSkeletalAnimationGX2() {
    DestroyGX2Pipeline();
}

void SceneSkeletalAnimationGX2::LoadAsync() {
    // 1. Carga asíncrona de archivos binarios en Core 2
    AssetManager::LoadBinaryFile("models/character.wmesh", m_stagedMesh);
    AssetManager::LoadBinaryFile("models/character.wskel", m_stagedSkel);
    AssetManager::LoadBinaryFile("models/character.wanim", m_stagedAnim);

    // 2. Parsear esqueleto y animaciones
    AnimationLoader::LoadSkeleton("models/character.wskel", m_skeleton);
    AnimationLoader::LoadClips("models/character.wanim", m_animationClips);
}

void SceneSkeletalAnimationGX2::CreateStudioPedestalMeshes() {
    if (m_pedestalCreated) return;

    // 1. Pedestal Cilíndrico de Estudio (Radio 1.8m, Altura 0.25m)
    constexpr int PEDESTAL_SEGS = 48;
    constexpr float PEDESTAL_RADIUS = 1.8f;
    constexpr float PEDESTAL_HEIGHT = 0.25f;

    std::vector<MeshVertex> pedVerts;
    std::vector<uint32_t> pedIdxs;

    // Centro superior
    uint32_t topCenterIdx = 0;
    pedVerts.push_back(MeshVertex{
        {0.0f, PEDESTAL_HEIGHT, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.5f, 0.5f},
        {0.88f, 0.88f, 0.90f, 1.0f}
    });

    for (int s = 0; s < PEDESTAL_SEGS; ++s) {
        float angle = (static_cast<float>(s) / static_cast<float>(PEDESTAL_SEGS)) * 6.2831853f;
        float nx = std::cos(angle);
        float nz = std::sin(angle);
        float px = nx * PEDESTAL_RADIUS;
        float pz = nz * PEDESTAL_RADIUS;
        float u = nx * 0.5f + 0.5f;
        float v = nz * 0.5f + 0.5f;

        pedVerts.push_back(MeshVertex{
            {px, PEDESTAL_HEIGHT, pz},
            {0.0f, 1.0f, 0.0f},
            {u, v},
            {0.85f, 0.85f, 0.87f, 1.0f}
        });
    }

    for (int s = 0; s < PEDESTAL_SEGS; ++s) {
        uint32_t nextS = (s + 1) % PEDESTAL_SEGS;
        pedIdxs.push_back(topCenterIdx);
        pedIdxs.push_back(static_cast<uint32_t>(1 + s));
        pedIdxs.push_back(static_cast<uint32_t>(1 + nextS));
    }

    uint32_t rimBaseIdx = static_cast<uint32_t>(pedVerts.size());
    for (int s = 0; s < PEDESTAL_SEGS; ++s) {
        float angle = (static_cast<float>(s) / static_cast<float>(PEDESTAL_SEGS)) * 6.2831853f;
        float nx = std::cos(angle);
        float nz = std::sin(angle);
        float px = nx * PEDESTAL_RADIUS;
        float pz = nz * PEDESTAL_RADIUS;
        float u = static_cast<float>(s) / static_cast<float>(PEDESTAL_SEGS);

        pedVerts.push_back(MeshVertex{
            {px, PEDESTAL_HEIGHT, pz},
            {nx, 0.0f, nz},
            {u, 0.0f},
            {0.75f, 0.75f, 0.78f, 1.0f}
        });
        pedVerts.push_back(MeshVertex{
            {px, 0.0f, pz},
            {nx, 0.0f, nz},
            {u, 1.0f},
            {0.45f, 0.45f, 0.48f, 1.0f}
        });
    }

    for (int s = 0; s < PEDESTAL_SEGS; ++s) {
        int nextS = (s + 1) % PEDESTAL_SEGS;
        uint32_t i0 = static_cast<uint32_t>(rimBaseIdx + s * 2);
        uint32_t i1 = static_cast<uint32_t>(rimBaseIdx + s * 2 + 1);
        uint32_t i2 = static_cast<uint32_t>(rimBaseIdx + nextS * 2);
        uint32_t i3 = static_cast<uint32_t>(rimBaseIdx + nextS * 2 + 1);

        pedIdxs.push_back(i0);
        pedIdxs.push_back(i1);
        pedIdxs.push_back(i2);

        pedIdxs.push_back(i2);
        pedIdxs.push_back(i1);
        pedIdxs.push_back(i3);
    }

    m_pedestalMesh.CreateFromVertices(pedVerts, pedIdxs);

    // 2. Plano de Sombra de Contacto Suave (AlphaBlend) a ras del suelo
    constexpr float SHADOW_EXTENT = 1.65f;
    std::vector<MeshVertex> shVerts = {
        {{-SHADOW_EXTENT, 0.005f, -SHADOW_EXTENT}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.65f}},
        {{ SHADOW_EXTENT, 0.005f, -SHADOW_EXTENT}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.65f}},
        {{ SHADOW_EXTENT, 0.005f,  SHADOW_EXTENT}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.65f}},
        {{-SHADOW_EXTENT, 0.005f,  SHADOW_EXTENT}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.65f}}
    };
    std::vector<uint32_t> shIdxs = {0, 1, 2, 0, 2, 3};
    m_shadowMesh.CreateFromVertices(shVerts, shIdxs);

    m_pedestalCreated = true;
}

bool SceneSkeletalAnimationGX2::InitGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_shaderLoaded && m_characterBuffersCreated && m_depthBufferCreated && m_nativeContextState) {
        return true;
    }

    if (!m_shaderLoaded) {
        std::vector<uint8_t> shaderData;
        bool loaded = AssetManager::LoadBinaryFile("shaders/skinning_gx2.gsh", shaderData);
        if (!loaded || shaderData.empty()) {
            shaderData.assign(SKINNING_GX2_GSH_DATA, SKINNING_GX2_GSH_DATA + SKINNING_GX2_GSH_SIZE);
            loaded = true;
        }

        if (loaded && !shaderData.empty()) {
            uint32_t vsHeaderSize = GFDGetVertexShaderHeaderSize(0, shaderData.data());
            uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
            uint32_t psHeaderSize = GFDGetPixelShaderHeaderSize(0, shaderData.data());
            uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());

            m_directVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHeaderSize, MemoryTag::ShaderProgram, "SkelAnim_VS_Hdr"));
            m_directVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "SkelAnim_VS_Prog");
            m_directPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHeaderSize, MemoryTag::ShaderProgram, "SkelAnim_PS_Hdr"));
            m_directPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "SkelAnim_PS_Prog");

            if (m_directVs && m_directVsProgram && m_directPs && m_directPsProgram) {
                BOOL vsOk = GFDGetVertexShader(m_directVs, m_directVsProgram, 0, shaderData.data());
                BOOL psOk = GFDGetPixelShader(m_directPs, m_directPsProgram, 0, shaderData.data());

                if (vsOk && psOk) {
                    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directVsProgram, vsProgSize);
                    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directPsProgram, psProgSize);

                    // Fetch Shader con 6 atributos para malla esquelética (stride 80B)
                    GX2AttribStream attrs[6];
                    attrs[0].location = 0;
                    attrs[0].buffer = 0;
                    attrs[0].offset = offsetof(SkinnedMeshVertex, pos);
                    attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                    attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[0].aluDivisor = 0;
                    attrs[0].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                    attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                    attrs[1].location = 1;
                    attrs[1].buffer = 0;
                    attrs[1].offset = offsetof(SkinnedMeshVertex, normal);
                    attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                    attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[1].aluDivisor = 0;
                    attrs[1].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                    attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                    attrs[2].location = 2;
                    attrs[2].buffer = 0;
                    attrs[2].offset = offsetof(SkinnedMeshVertex, texCoord);
                    attrs[2].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
                    attrs[2].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[2].aluDivisor = 0;
                    attrs[2].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                    attrs[2].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                    attrs[3].location = 3;
                    attrs[3].buffer = 0;
                    attrs[3].offset = offsetof(SkinnedMeshVertex, color);
                    attrs[3].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
                    attrs[3].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[3].aluDivisor = 0;
                    attrs[3].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                    attrs[3].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                    attrs[4].location = 4;
                    attrs[4].buffer = 0;
                    attrs[4].offset = offsetof(SkinnedMeshVertex, boneIndices);
                    attrs[4].format = GX2_ATTRIB_FORMAT_UINT_8_8_8_8;
                    attrs[4].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[4].aluDivisor = 0;
                    attrs[4].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                    attrs[4].endianSwap = GX2_ENDIAN_SWAP_NONE;

                    attrs[5].location = 5;
                    attrs[5].buffer = 0;
                    attrs[5].offset = offsetof(SkinnedMeshVertex, boneWeights);
                    attrs[5].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
                    attrs[5].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[5].aluDivisor = 0;
                    attrs[5].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                    attrs[5].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                    uint32_t fsSize = GX2CalcFetchShaderSizeEx(6, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                    m_directFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "SkelAnim_FS_Prog");
                    if (m_directFsProgram) {
                        GX2InitFetchShaderEx(&m_directFs, static_cast<uint8_t*>(m_directFsProgram), 6, attrs,
                                             GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directFsProgram, fsSize);
                        m_shaderLoaded = true;
                    }
                }
            }
        }
    }

    // 2. Subir buffers VBO/IBO del personaje a GPU
    if (!m_characterBuffersCreated && !m_stagedMesh.empty() && m_stagedMesh.size() >= 96) {
        const auto* hdr = reinterpret_cast<const MeshHeader*>(m_stagedMesh.data());
        m_characterVertexCount = hdr->vertexCount;
        m_characterIndexCount = hdr->indexCount;

        m_characterVBO.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ
        );
        m_characterVBO.elemSize = hdr->vertexStride;
        m_characterVBO.elemCount = hdr->vertexCount;
        if (GX2RCreateBuffer(&m_characterVBO)) {
            void* vboPtr = GX2RLockBufferEx(&m_characterVBO, GX2R_RESOURCE_BIND_NONE);
            if (vboPtr) {
                std::memcpy(vboPtr, m_stagedMesh.data() + hdr->vboOffset, hdr->vboSize);
                GX2RUnlockBufferEx(&m_characterVBO, GX2R_RESOURCE_BIND_NONE);
            }
        }

        m_characterIBO.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_INDEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ
        );
        m_characterIBO.elemSize = sizeof(uint16_t);
        m_characterIBO.elemCount = hdr->indexCount;
        if (GX2RCreateBuffer(&m_characterIBO)) {
            void* iboPtr = GX2RLockBufferEx(&m_characterIBO, GX2R_RESOURCE_BIND_NONE);
            if (iboPtr) {
                std::memcpy(iboPtr, m_stagedMesh.data() + hdr->iboOffset, hdr->iboSize);
                GX2RUnlockBufferEx(&m_characterIBO, GX2R_RESOURCE_BIND_NONE);
            }
        }

        m_characterBuffersCreated = true;
    }

    // 3. Crear Depth Buffer en 1280x720
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
            MemoryManager::Instance().AllocAligned(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState),
                                                  MemoryTag::ContextState, "SkelAnim_ContextState")
        );
        if (m_nativeContextState) {
            GX2SetupContextStateEx(m_nativeContextState, TRUE);
        }
    }

    return m_shaderLoaded && m_characterBuffersCreated && m_depthBufferCreated && (m_nativeContextState != nullptr);
#else
    return true;
#endif
}

void SceneSkeletalAnimationGX2::DestroyGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIIsRunning() && ProcUIInForeground()) {
        GX2DrawDone();
    }

    if (m_characterBuffersCreated) {
        GX2RDestroyBufferEx(&m_characterVBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_characterIBO, GX2R_RESOURCE_BIND_NONE);
        m_characterBuffersCreated = false;
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

    if (m_directVs) { void* raw = static_cast<void*>(m_directVs); MemoryManager::Instance().Free(raw); m_directVs = nullptr; }
    if (m_directVsProgram) { MemoryManager::Instance().FreeAligned(m_directVsProgram); m_directVsProgram = nullptr; }
    if (m_directPs) { void* raw = static_cast<void*>(m_directPs); MemoryManager::Instance().Free(raw); m_directPs = nullptr; }
    if (m_directPsProgram) { MemoryManager::Instance().FreeAligned(m_directPsProgram); m_directPsProgram = nullptr; }
    if (m_directFsProgram) { MemoryManager::Instance().FreeAligned(m_directFsProgram); m_directFsProgram = nullptr; }

    m_shaderLoaded = false;
#endif
}

void SceneSkeletalAnimationGX2::OnEnter() {
    AudioManager::Instance().PlayBGM();
    m_clayMode = (EngineConfig::Get().graphics.renderMode == ShadingRenderMode::Clay);

    // 1. Si no se ejecutó LoadAsync, cargar síncronamente
    if (m_skeleton.jointCount == 0) {
        AnimationLoader::LoadSkeleton("models/character.wskel", m_skeleton);
        AnimationLoader::LoadClips("models/character.wanim", m_animationClips);
    }
    if (m_stagedMesh.empty()) {
        AssetManager::LoadBinaryFile("models/character.wmesh", m_stagedMesh);
    }

    // 2. Inicializar pipeline de hardware y geometrías de estudio
    InitGX2Pipeline();
    CreateStudioPedestalMeshes();

    // 3. Crear entidad del personaje esquelético
    m_characterEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_characterEntity, Point3D{0.0f, 0.25f, 0.0f});
    auto& meshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_characterEntity);
#if defined(__WIIU__) || defined(__wiiu__)
    meshComp.vbo = &m_characterVBO;
    meshComp.ibo = &m_characterIBO;
    meshComp.indexCount = m_characterIndexCount;
    meshComp.vertexCount = m_characterVertexCount;
    meshComp.vertexStride = SKINNED_VERTEX_STRIDE;
    meshComp.primitiveMode = GX2_PRIMITIVE_MODE_TRIANGLES;
#endif
    m_ecsWorld.EmplaceComponent<MaterialComponent>(m_characterEntity, 1.0f, 1.0f, 0.35f, 0.0f, BlendMode::Opaque, true, true);
    m_ecsWorld.EmplaceComponent<SkeletalMeshComponent>(m_characterEntity, &m_skeleton);

    const AnimationClip* initClip = (!m_animationClips.empty()) ? &m_animationClips[0] : nullptr;
    m_ecsWorld.EmplaceComponent<AnimatorComponent>(m_characterEntity, initClip, true, 1.0f);
    m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_characterEntity, Point3D{0.0f, 1.5f, 0.0f}, 1.6f);

    // Configurar IKRigComponent (Two-Bone Foot IK + Look-At IK + FABRIK Arm Chain)
    auto& ikRig = m_ecsWorld.EmplaceComponent<IKRigComponent>(m_characterEntity);
    const int16_t jChest = static_cast<int16_t>(m_skeleton.FindJointIndex("Chest"));
    const int16_t jHead = static_cast<int16_t>(m_skeleton.FindJointIndex("Neck_Head"));
    const int16_t jLHip = static_cast<int16_t>(m_skeleton.FindJointIndex("L_Hip"));
    const int16_t jLKnee = static_cast<int16_t>(m_skeleton.FindJointIndex("L_Knee"));
    const int16_t jLFoot = static_cast<int16_t>(m_skeleton.FindJointIndex("L_Foot"));
    const int16_t jRHip = static_cast<int16_t>(m_skeleton.FindJointIndex("R_Hip"));
    const int16_t jRKnee = static_cast<int16_t>(m_skeleton.FindJointIndex("R_Knee"));
    const int16_t jRFoot = static_cast<int16_t>(m_skeleton.FindJointIndex("R_Foot"));
    const int16_t jLShoulder = static_cast<int16_t>(m_skeleton.FindJointIndex("L_Shoulder"));
    const int16_t jLElbow = static_cast<int16_t>(m_skeleton.FindJointIndex("L_Elbow"));
    const int16_t jLHand = static_cast<int16_t>(m_skeleton.FindJointIndex("L_Hand"));

    ikRig.AddTwoBoneChain(jLHip, jLKnee, jLFoot, Point3D{0.0f, 0.0f, 1.0f}, 0.95f, true);
    ikRig.AddTwoBoneChain(jRHip, jRKnee, jRFoot, Point3D{0.0f, 0.0f, 1.0f}, 0.90f, true);
    ikRig.ConfigureLookAt(jChest, jHead, Point3D{0.0f, 0.0f, 1.0f}, 0.30f, 0.85f, 1.15f, true);
    const int16_t armChain[4] = {jChest, jLShoulder, jLElbow, jLHand};
    ikRig.ConfigureFABRIK(armChain, 4, 6, 0.002f, false);

    // 4. Crear pedestal y sombra
    m_pedestalEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_pedestalEntity, Point3D{0.0f, 0.0f, 0.0f});
    auto& pedMeshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_pedestalEntity);
    m_pedestalMesh.PopulateMeshComponent(pedMeshComp);
    m_ecsWorld.EmplaceComponent<MaterialComponent>(m_pedestalEntity, 1.0f, 1.0f, 0.20f, 0.0f, BlendMode::Opaque, true, true);
    auto& pedBound = m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_pedestalEntity);
    m_pedestalMesh.PopulateBoundingComponent(pedBound);

    m_shadowEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_shadowEntity, Point3D{0.0f, 0.0f, 0.0f});
    auto& shMeshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_shadowEntity);
    m_shadowMesh.PopulateMeshComponent(shMeshComp);
    m_ecsWorld.EmplaceComponent<MaterialComponent>(m_shadowEntity, 1.0f, 1.0f, 0.0f, 0.0f, BlendMode::AlphaBlend, true, false);

    // 5. Cámara orbital con Spring-Arm
    m_camTarget = Point3D{0.0f, 1.8f, 0.0f};
    m_targetCamTarget = m_camTarget;
    m_camYaw = 0.45f;
    m_targetCamYaw = 0.45f;
    m_camPitch = 0.20f;
    m_targetCamPitch = 0.20f;
    m_camDistance = 5.5f;
    m_targetCamDistance = 5.5f;

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
    m_ecsWorld.EmplaceComponent<CameraComponent>(m_cameraEntity, 55.0f, 1280.0f / 720.0f, 0.20f, 100.0f, m_camTarget, DisplayTarget::TV);
    m_ecsWorld.EmplaceComponent<StudioTurntableComponent>(m_cameraEntity, m_camYaw, m_camPitch, m_camDistance, m_camTarget, m_autoRotate);
    m_ecsWorld.EmplaceComponent<SpringArmComponent>(m_cameraEntity, m_camDistance, 0.35f, 1.5f, 25.0f, 0.25f);

    // Paso inicial de sistemas
    TransformSystem::Update(m_ecsWorld);
    UpdateIKDemoTargets();
    AnimationSystem::Update(m_ecsWorld, 0.001f);
    StudioTurntableSystem::Update(m_ecsWorld, 0.001f);
    SpringArmSystem::Update(m_ecsWorld, 0.001f);
    CameraSystem::Update(m_ecsWorld);

    m_stagedMesh.clear();
    m_stagedMesh.shrink_to_fit();
}

void SceneSkeletalAnimationGX2::OnExit() {
    m_ecsWorld.Clear();
}

void SceneSkeletalAnimationGX2::TriggerClipCrossFade(int clipIdx) {
    if (clipIdx < 0 || clipIdx >= static_cast<int>(m_animationClips.size())) return;
    m_activeClipIndex = clipIdx;
    if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_characterEntity)) {
        anim->CrossFadeTo(&m_animationClips[clipIdx], 0.32f, true);
    }
    AudioManager::Instance().PlayClick();
}

void SceneSkeletalAnimationGX2::ToggleOverlayLayer() {
    if (m_animationClips.size() <= 2) return;
    m_layerBlendActive = !m_layerBlendActive;
    if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_characterEntity)) {
        if (m_layerBlendActive) {
            m_activeClipIndex = 1; // Walk en tren inferior
            anim->CrossFadeTo(&m_animationClips[1], 0.25f, true);
            // Máscara binaria bits 1..9 (Spine, Chest, Neck_Head, hombros/codos/manos)
            anim->SetOverlayLayer(&m_animationClips[2], 0x03FEu, 1.0f, true);
        } else {
            anim->ClearOverlayLayer();
        }
    }
    AudioManager::Instance().PlayClick();
}

void SceneSkeletalAnimationGX2::CycleIKDemoMode() {
    m_ikDemoMode = (m_ikDemoMode + 1) % 3;
    UpdateIKDemoTargets();
    AudioManager::Instance().PlayClick();
}

void SceneSkeletalAnimationGX2::ResetDemoCamera() {
    m_targetCamTarget = Point3D{0.0f, 1.8f, 0.0f};
    m_targetCamYaw = 0.45f;
    m_targetCamPitch = 0.20f;
    m_targetCamDistance = 5.5f;
    if (auto* arm = m_ecsWorld.GetComponent<SpringArmComponent>(m_cameraEntity)) {
        arm->targetArmLength = 5.5f;
    }
    AudioManager::Instance().PlayClick();
}

void SceneSkeletalAnimationGX2::UpdateIKDemoTargets() {
    m_lookAtTargetPos = Point3D{
        std::sin(m_demoOrbitTime * 1.15f) * 1.85f,
        2.25f + std::cos(m_demoOrbitTime * 1.65f) * 0.45f,
        2.40f
    };
    m_leftFootTargetPos = Point3D{0.35f, 0.72f, 0.18f};
    m_rightFootTargetPos = Point3D{-0.35f, 0.40f, 0.12f};
    m_fabrikTargetPos = Point3D{
        1.10f + std::sin(m_demoOrbitTime * 1.8f) * 0.28f,
        2.35f + std::cos(m_demoOrbitTime * 2.2f) * 0.42f,
        0.70f + std::sin(m_demoOrbitTime * 1.1f) * 0.35f
    };

    auto* ikRig = m_ecsWorld.GetComponent<IKRigComponent>(m_characterEntity);
    if (!ikRig) return;

    ikRig->ikEnabled = (m_ikDemoMode > 0);
    ikRig->pelvisOffsetY = (m_ikDemoMode > 0) ? -0.05f : 0.0f;

    if (ikRig->twoBoneChainCount >= 2) {
        const float rightWeight = (m_activeClipIndex == 1 || m_layerBlendActive) ? 0.35f : 0.90f;
        ikRig->twoBoneChains[0].targetWorldPos = m_leftFootTargetPos;
        ikRig->twoBoneChains[0].enabled = (m_ikDemoMode > 0);
        ikRig->twoBoneChains[1].targetWorldPos = m_rightFootTargetPos;
        ikRig->twoBoneChains[1].weight = rightWeight;
        ikRig->twoBoneChains[1].enabled = (m_ikDemoMode > 0);
    }

    ikRig->lookAt.targetWorldPos = m_lookAtTargetPos;
    ikRig->lookAt.enabled = (m_ikDemoMode > 0);

    ikRig->fabrikChain.targetWorldPos = m_fabrikTargetPos;
    ikRig->fabrikChain.enabled = (m_ikDemoMode == 2);
}

void SceneSkeletalAnimationGX2::DrawIKDebugOverlay() const {
    if (m_ikDemoMode <= 0) return;

    const auto* camComp = m_ecsWorld.GetComponent<CameraComponent>(m_cameraEntity);
    const auto* charTf = m_ecsWorld.GetComponent<TransformComponent>(m_characterEntity);
    const auto* skelMesh = m_ecsWorld.GetComponent<SkeletalMeshComponent>(m_characterEntity);
    if (!camComp || !charTf || !skelMesh || !skelMesh->IsValid()) return;

    DebugDraw3D::SetViewProjection(camComp->viewProjMatrix, 1280.0f, 720.0f, 0.20f);

    // 1. Esqueleto resuelto (huesos FK + post-IK)
    const uint32_t jCount = std::min(skelMesh->jointCount, MAX_SKELETON_JOINTS);
    for (uint32_t i = 0; i < jCount; ++i) {
        const int16_t parentIdx = m_skeleton.joints[i].parentIndex;
        if (parentIdx >= 0 && static_cast<uint32_t>(parentIdx) < jCount) {
            const Point3D pParent = charTf->worldMatrix.TransformPoint(skelMesh->worldPoses[parentIdx].GetTranslation());
            const Point3D pChild = charTf->worldMatrix.TransformPoint(skelMesh->worldPoses[i].GetTranslation());
            DebugDraw3D::DrawLine(pParent, pChild, 2.2f, ColorRGBA{80, 235, 255, 210});
        }
    }

    // 2. Escalón procedimental bajo pie izquierdo (Two-Bone Foot IK)
    DebugDraw3D::DrawBox(Point3D{0.35f, 0.46f, 0.18f}, Point3D{0.26f, 0.21f, 0.28f}, 2.0f, ColorRGBA{80, 255, 130, 220});
    DebugDraw3D::DrawSphere(m_leftFootTargetPos, 0.08f, 2.0f, ColorRGBA::Green(), 10);

    // 3. Objetivo orbital de Look-At IK
    DebugDraw3D::DrawSphere(m_lookAtTargetPos, 0.14f, 2.4f, ColorRGBA::Yellow(), 12);
    const int32_t headIdx = m_skeleton.FindJointIndex("Neck_Head");
    if (headIdx >= 0) {
        const Point3D headWorld = charTf->worldMatrix.TransformPoint(skelMesh->worldPoses[headIdx].GetTranslation());
        DebugDraw3D::DrawLine(headWorld, m_lookAtTargetPos, 1.6f, ColorRGBA{255, 220, 60, 140});
    }

    // 4. Objetivo multi-hueso FABRIK (cuando modo == 2)
    if (m_ikDemoMode == 2) {
        DebugDraw3D::DrawSphere(m_fabrikTargetPos, 0.16f, 2.6f, ColorRGBA{255, 70, 220, 255}, 14);
    }
}

void SceneSkeletalAnimationGX2::Update(float dt) {
    // 1. Telemetría de FPS y avance temporal de demostración IK
    m_fpsTimer += dt;
    ++m_frameCount;
    if (m_fpsTimer >= 0.5f) {
        m_fps = static_cast<float>(m_frameCount) / m_fpsTimer;
        m_frameTimeMs = (m_fps > 0.0f) ? (1000.0f / m_fps) : 16.6f;
        m_frameCount = 0;
        m_fpsTimer = 0.0f;
    }
    if (!m_isPaused) {
        m_demoOrbitTime += dt;
    }

    auto& input = InputManager::Instance();
    const auto& touch = input.GetTouchState();

    // 2. Control de Animación, Cross-Fade, Capas Parciales e IK (Botones Físicos)
#if defined(__WIIU__) || defined(__wiiu__)
    if (input.IsButtonTriggered(VPAD_BUTTON_A) || input.IsButtonTriggered(WPAD_PRO_BUTTON_A)) {
        TriggerClipCrossFade(0);
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_B) || input.IsButtonTriggered(WPAD_PRO_BUTTON_B)) {
        TriggerClipCrossFade(1);
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_X) || input.IsButtonTriggered(WPAD_PRO_BUTTON_X)) {
        TriggerClipCrossFade(2);
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_Y) || input.IsButtonTriggered(WPAD_PRO_BUTTON_Y)) {
        ResetDemoCamera();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_L) || input.IsButtonTriggered(WPAD_PRO_BUTTON_L)) {
        ToggleOverlayLayer();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_R) || input.IsButtonTriggered(WPAD_PRO_BUTTON_R)) {
        CycleIKDemoMode();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_STICK_L)) {
        m_isPaused = !m_isPaused;
        if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_characterEntity)) {
            if (m_isPaused) anim->Pause(); else anim->Resume();
        }
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonHeld(VPAD_BUTTON_ZL) || input.IsButtonHeld(WPAD_PRO_BUTTON_ZL)) {
        m_targetCamDistance = std::clamp(m_targetCamDistance - 6.5f * dt, 1.5f, 25.0f);
    }
    if (input.IsButtonHeld(VPAD_BUTTON_ZR) || input.IsButtonHeld(WPAD_PRO_BUTTON_ZR)) {
        m_targetCamDistance = std::clamp(m_targetCamDistance + 6.5f * dt, 1.5f, 25.0f);
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_UP)) {
        m_playbackSpeed = std::min(m_playbackSpeed + 0.25f, 2.5f);
        if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_characterEntity)) {
            anim->playbackSpeed = m_playbackSpeed;
        }
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_DOWN)) {
        m_playbackSpeed = std::max(m_playbackSpeed - 0.25f, 0.25f);
        if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_characterEntity)) {
            anim->playbackSpeed = m_playbackSpeed;
        }
    }
#endif

    // 3. Control Táctil de Botones del HUD
    if (touch.active) {
        float tx = touch.x;
        float ty = touch.y;

        if (ty >= 65.0f && ty <= 590.0f) {
            if (touch.justPressed) {
                m_isDraggingCamera = true;
                m_dragStartX = tx;
                m_dragStartY = ty;
                m_initialYaw = m_targetCamYaw;
                m_initialPitch = m_targetCamPitch;
            } else if (m_isDraggingCamera) {
                m_targetCamYaw = m_initialYaw - (tx - m_dragStartX) * 0.006f;
                m_targetCamPitch = std::clamp(m_initialPitch + (ty - m_dragStartY) * 0.005f, -0.35f, 1.35f);
            }
        }

        if (touch.justPressed) {
            if (s_btnVolverTop.Contains(tx, ty) || s_btnVolverBot.Contains(tx, ty)) {
                AudioManager::Instance().PlayClick();
                SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
                return;
            }
            if (s_btnIdle.Contains(tx, ty)) {
                TriggerClipCrossFade(0);
            } else if (s_btnWalk.Contains(tx, ty)) {
                TriggerClipCrossFade(1);
            } else if (s_btnWave.Contains(tx, ty)) {
                TriggerClipCrossFade(2);
            } else if (s_btnLayer.Contains(tx, ty)) {
                ToggleOverlayLayer();
            } else if (s_btnIKMode.Contains(tx, ty)) {
                CycleIKDemoMode();
            } else if (s_btnPause.Contains(tx, ty)) {
                m_isPaused = !m_isPaused;
                if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_characterEntity)) {
                    if (m_isPaused) anim->Pause(); else anim->Resume();
                }
                AudioManager::Instance().PlayClick();
            } else if (s_btnSpeed.Contains(tx, ty)) {
                m_playbackSpeed = (m_playbackSpeed >= 2.0f) ? 0.5f : (m_playbackSpeed + 0.5f);
                if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_characterEntity)) {
                    anim->playbackSpeed = m_playbackSpeed;
                }
                AudioManager::Instance().PlayClick();
            } else if (s_btnClay.Contains(tx, ty)) {
                m_clayMode = !m_clayMode;
                EngineConfig::Instance().graphics.renderMode = m_clayMode ? ShadingRenderMode::Clay : ShadingRenderMode::Textured;
                AudioManager::Instance().PlayClick();
            } else if (s_btnZoomIn.Contains(tx, ty)) {
                m_targetCamDistance = std::clamp(m_targetCamDistance - 1.2f, 1.5f, 25.0f);
                if (auto* arm = m_ecsWorld.GetComponent<SpringArmComponent>(m_cameraEntity)) {
                    arm->targetArmLength = m_targetCamDistance;
                }
                AudioManager::Instance().PlayClick();
            } else if (s_btnZoomOut.Contains(tx, ty)) {
                m_targetCamDistance = std::clamp(m_targetCamDistance + 1.2f, 1.5f, 25.0f);
                if (auto* arm = m_ecsWorld.GetComponent<SpringArmComponent>(m_cameraEntity)) {
                    arm->targetArmLength = m_targetCamDistance;
                }
                AudioManager::Instance().PlayClick();
            } else if (s_btnResetCam.Contains(tx, ty)) {
                ResetDemoCamera();
            }
        }
    } else {
        m_isDraggingCamera = false;
    }

    // 4. Control orbital de cámara (Joystick derecho y arrastre táctil)
    const auto stickL = input.GetLeftStick();
    const auto stickR = input.GetRightStick();
    if (std::abs(stickR.x) > 0.05f) m_targetCamYaw += stickR.x * dt * 2.2f;
    if (std::abs(stickR.y) > 0.05f) m_targetCamPitch = std::clamp(m_targetCamPitch + stickR.y * dt * 1.8f, -0.25f, 1.25f);

    if (std::abs(stickL.y) > 0.05f) {
        m_targetCamDistance = std::clamp(m_targetCamDistance - stickL.y * dt * 6.0f, 2.0f, 20.0f);
    }

    // 5. Actualizar sistemas ECS (Transform antes de IK para matriz de modelo fresca)
    TransformSystem::Update(m_ecsWorld);
    UpdateIKDemoTargets();
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

    if (const auto* tvCam = m_ecsWorld.GetComponent<CameraComponent>(m_cameraEntity)) {
        CullingSystem::Update(m_ecsWorld, *tvCam, 1280.0f, 720.0f);
    }

    // 6. Retorno físico al menú si se solicita
    if (input.IsBackRequested()) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
    }
}

void SceneSkeletalAnimationGX2::BindColorBuffer(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    m_pColorBuffer = GetSDL2ColorBuffer(renderer);
#else
    (void)renderer;
#endif
}

void SceneSkeletalAnimationGX2::RenderScene3D_GX2() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_nativeContextState || !m_pColorBuffer || !m_shaderLoaded || !m_characterBuffersCreated) return;

    GX2SetContextState(m_nativeContextState);
    GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    if (m_depthBufferCreated) {
        GX2SetDepthBuffer(&m_depthBuffer);
    }
    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);
    GX2ClearColor(m_pColorBuffer, 0.05f, 0.06f, 0.08f, 1.0f);
    if (m_depthBufferCreated) {
        GX2ClearDepthStencilEx(&m_depthBuffer, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
    }

    // Restaurar estado de contexto y render targets tras el clear en silicio GX2
    GX2SetContextState(m_nativeContextState);
    GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    if (m_depthBufferCreated) {
        GX2SetDepthBuffer(&m_depthBuffer);
    }
    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);
    GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, FALSE,
                         GX2_POLYGON_MODE_TRIANGLE, GX2_POLYGON_MODE_TRIANGLE,
                         TRUE, TRUE, FALSE);

    // 1. Obtener matrices de vista y proyección desde CameraComponent
    const auto* camComp = m_ecsWorld.GetComponent<CameraComponent>(m_cameraEntity);
    Matrix4x4 vpMatrix = camComp ? camComp->viewProjMatrix : Matrix4x4::Identity();

    // 2. Renderizar Personaje con Deformación Esquelética en GPU Latte GX2
    GX2SetFetchShader(&m_directFs);
    GX2SetVertexShader(m_directVs);
    GX2SetPixelShader(m_directPs);

    const auto* transform = m_ecsWorld.GetComponent<TransformComponent>(m_characterEntity);
    Matrix4x4 model = transform ? transform->worldMatrix : Matrix4x4::Identity();
    Matrix4x4 mvp = vpMatrix * model;

    // -------------------------------------------------------------------------
    // Subir registros de uniformes de vértices (KC2[0..103] = 104 vec4 = 416 floats):
    // KC2[0..3]   = uMVP (16 floats)
    // KC2[4..7]   = uModel (16 floats)
    // KC2[8..103] = uBones[24] (24 * 16 = 384 floats)
    // -------------------------------------------------------------------------
    constexpr uint32_t TOTAL_VS_UNIFORM_FLOATS = 32 + 24 * 16; // 416 floats exactos
    alignas(256) float vsUniforms[TOTAL_VS_UNIFORM_FLOATS];

    std::memcpy(&vsUniforms[0], mvp.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], model.m, 16 * sizeof(float));

    if (const auto* skelMesh = m_ecsWorld.GetComponent<SkeletalMeshComponent>(m_characterEntity)) {
        uint32_t count = std::min(skelMesh->jointCount, 24u);
        for (uint32_t j = 0; j < count; ++j) {
            std::memcpy(&vsUniforms[32 + j * 16], skelMesh->skinningMatrices[j].m, 16 * sizeof(float));
        }
        for (uint32_t j = count; j < 24u; ++j) {
            std::memcpy(&vsUniforms[32 + j * 16], Matrix4x4::Identity().m, 16 * sizeof(float));
        }
    } else {
        for (uint32_t j = 0; j < 24u; ++j) {
            std::memcpy(&vsUniforms[32 + j * 16], Matrix4x4::Identity().m, 16 * sizeof(float));
        }
    }

    GX2SetVertexUniformReg(0, TOTAL_VS_UNIFORM_FLOATS, vsUniforms);

    // -------------------------------------------------------------------------
    // Subir registros de uniformes de píxeles (KC2[0..3] = 4 vec4 = 16 floats):
    // KC2[0] = uCamPos (4 floats)
    // KC2[1] = uLightDir (4 floats)
    // KC2[2] = uLightColor + roughness (4 floats)
    // KC2[3] = uMaterialMode (4 floats)
    // -------------------------------------------------------------------------
    constexpr uint32_t TOTAL_PS_UNIFORM_FLOATS = 16;
    alignas(256) float psUniforms[TOTAL_PS_UNIFORM_FLOATS];
    const auto* camTrans = m_ecsWorld.GetComponent<TransformComponent>(m_cameraEntity);
    Point3D camPos = camTrans ? camTrans->position : Point3D{0.0f, 2.0f, 5.0f};

    psUniforms[0] = camPos.x; psUniforms[1] = camPos.y; psUniforms[2] = camPos.z; psUniforms[3] = 0.0f;
    psUniforms[4] = 0.55f;    psUniforms[5] = 0.75f;    psUniforms[6] = 0.35f;    psUniforms[7] = 1.0f; // uLightDir
    psUniforms[8] = 1.0f;     psUniforms[9] = 0.96f;    psUniforms[10] = 0.88f;  psUniforms[11] = 0.35f; // uLightColor + roughness
    psUniforms[12] = m_clayMode ? 2.0f : 0.0f; // uMaterialMode (0 = Shaded, 2 = Clay)
    psUniforms[13] = 1.0f;    psUniforms[14] = 0.0f;    psUniforms[15] = 0.0f;

    GX2SetPixelUniformReg(0, TOTAL_PS_UNIFORM_FLOATS, psUniforms);

    // Configurar estado de profundidad y culling
    RHIPipelineCache::Instance().BindState(RHIPipelineState(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false));

    // Emitir llamada de dibujo indexado en silicio Latte GX2
    GX2RSetAttributeBuffer(&m_characterVBO, 0, SKINNED_VERTEX_STRIDE, 0);
    GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_characterIBO, GX2_INDEX_TYPE_U16, m_characterIndexCount, 0, 0, 1);

    m_drawCallsCount = 1;
    m_trianglesCount = m_characterIndexCount / 3;
#endif
}

void SceneSkeletalAnimationGX2::RenderHUD_GX2() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_pColorBuffer) return;

    UI::ScopedUIPass uiPass(m_nativeContextState, m_pColorBuffer);

    // Primitivas wireframe 3D para objetivos IK y jerarquía de huesos resuelta
    DrawIKDebugOverlay();

    // 1. Barra Superior con Botón Volver y Título
    UI::UIToolkit::DrawPanel(s_topBarPanel);
    UI::UIToolkit::DrawButton(s_btnVolverTop, "< VOLVER", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawLabel(125.0f, 22.0f, "ANIMACION ESQUELETICA, CROSS-FADE E IK EN SILICIO (CAP. 12)", UI::TextScale::Normal, ColorRGBA::Cyan());

    // 2. Panel de Control de Animación (Izquierda)
    UI::UIToolkit::DrawPanel(s_panelInfo);
    UI::UIToolkit::DrawLabel(30.0f, 74.0f, "BLENDING Y CINEMATICA INVERSA", UI::TextScale::Normal, ColorRGBA::Yellow());

    const char* clipName = (!m_animationClips.empty() && m_activeClipIndex < static_cast<int>(m_animationClips.size()))
        ? m_animationClips[m_activeClipIndex].name : "NINGUNO";
    const auto* animComp = m_ecsWorld.GetComponent<AnimatorComponent>(m_characterEntity);
    float pTime = animComp ? animComp->playbackTime : 0.0f;
    StackString<64> clipLabel;
    if (animComp && animComp->IsCrossFading() && animComp->nextClip) {
        clipLabel.Format("XFADE: %s->%s %.0f%%", clipName, animComp->nextClip->name, animComp->GetCrossFadeAlpha() * 100.0f);
    } else {
        clipLabel.Format("CLIP: %s | %.2fs (%.1fx)", clipName, pTime, m_playbackSpeed);
    }
    UI::UIToolkit::DrawLabel(30.0f, 102.0f, clipLabel.c_str(), UI::TextScale::Normal, ColorRGBA::Green());

    StackString<64> layerLabel;
    layerLabel.Format("CAPA PARCIAL: %s", m_layerBlendActive ? "WALK+WAVE (SUP)" : "BASE UNICA");
    UI::UIToolkit::DrawLabel(30.0f, 132.0f, layerLabel.c_str(), UI::TextScale::Small, m_layerBlendActive ? ColorRGBA::Cyan() : ColorRGBA::White());

    const char* ikDesc = (m_ikDemoMode == 0) ? "OFF (FK PURO)" : ((m_ikDemoMode == 1) ? "2-BONE FOOT + LOOK-AT" : "2-BONE + AIM + FABRIK");
    StackString<64> ikLabel;
    ikLabel.Format("IK SOLVER: %s", ikDesc);
    UI::UIToolkit::DrawLabel(30.0f, 160.0f, ikLabel.c_str(), UI::TextScale::Small, (m_ikDemoMode > 0) ? ColorRGBA{120, 255, 160, 255} : ColorRGBA{255, 200, 100, 255});

    UI::UIToolkit::DrawLabel(30.0f, 192.0f, "SPRING-ARM ANTI-CLIPPING", UI::TextScale::Small, ColorRGBA::Cyan());

    // Botones interactivos para Cap. 12.4 (Layer Mask) y Cap. 12.5 (IK Modes)
    UI::UIToolkit::DrawToggleButton(s_btnLayer, m_layerBlendActive ? "CAPA: WALK+WAVE" : "CAPA: SOLO (L)", m_layerBlendActive, false, UI::TextScale::Small);
    const char* ikBtnTxt = (m_ikDemoMode == 0) ? "IK: OFF (R)" : ((m_ikDemoMode == 1) ? "IK: FOOT+AIM" : "IK: +FABRIK");
    UI::UIToolkit::DrawToggleButton(s_btnIKMode, ikBtnTxt, m_ikDemoMode > 0, false, UI::TextScale::Small);

    // 3. Panel de Telemetría Hardware (Derecha)
    UI::UIToolkit::DrawPanel(s_panelTelem);
    UI::UIToolkit::DrawLabel(934.0f, 74.0f, "TELEMETRIA SILICIO", UI::TextScale::Normal, ColorRGBA::Yellow());

    StackString<64> fpsLabel;
    fpsLabel.Format("FPS: %.1f (%.2f ms)", m_fps, m_frameTimeMs);
    UI::UIToolkit::DrawLabel(934.0f, 104.0f, fpsLabel.c_str(), UI::TextScale::Normal, (m_fps >= 58.0f) ? ColorRGBA::Green() : ColorRGBA::Red());

    StackString<64> meshLabel;
    meshLabel.Format("HUESOS: %u | STRIDE: %uB", m_skeleton.jointCount, SKINNED_VERTEX_STRIDE);
    UI::UIToolkit::DrawLabel(934.0f, 134.0f, meshLabel.c_str(), UI::TextScale::Normal, ColorRGBA::White());

    const auto& metrics = ResourceMonitor::Instance().GetMetrics();
    float mem1MB = static_cast<float>(metrics.mem1Bytes) / (1024.0f * 1024.0f);
    float mem2MB = static_cast<float>(metrics.mem2Bytes) / (1024.0f * 1024.0f);
    StackString<64> memLabel;
    memLabel.Format("MEM1: %.1fMB | MEM2: %.1fMB", mem1MB, mem2MB);
    UI::UIToolkit::DrawLabel(934.0f, 164.0f, memLabel.c_str(), UI::TextScale::Normal, ColorRGBA::Cyan());

    StackString<64> drawLabel;
    drawLabel.Format("DRAWS: %u | TRIS: %u", m_drawCallsCount, m_trianglesCount);
    UI::UIToolkit::DrawLabel(934.0f, 194.0f, drawLabel.c_str(), UI::TextScale::Small, ColorRGBA{180, 220, 255, 255});

    StackString<32> distStr;
    distStr.Format("DIST: %.1fM", m_camDistance);
    UI::UIToolkit::DrawLabel(230.0f, 194.0f, distStr.c_str(), UI::TextScale::Small, ColorRGBA::Yellow());

    // Botones Flotantes de Zoom Rápido en Margen Derecho
    UI::UIToolkit::DrawButton(s_btnZoomIn, "ZOOM+", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnZoomOut, "ZOOM-", false, UI::TextScale::Normal);

    // 4. Barra Inferior de Botones Táctiles Interactivos
    UI::UIToolkit::DrawPanel(s_bottomBarPanel);
    UI::UIToolkit::DrawButton(s_btnIdle, "1: IDLE (A)", m_activeClipIndex == 0, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnWalk, "2: WALK (B)", m_activeClipIndex == 1, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnWave, "3: WAVE (X)", m_activeClipIndex == 2, UI::TextScale::Normal);
    UI::UIToolkit::DrawToggleButton(s_btnPause, m_isPaused ? "PAUSA:SI" : "PAUSA:NO", m_isPaused, false, UI::TextScale::Normal);

    StackString<32> spdLabel;
    spdLabel.Format("VEL:%.1fx", m_playbackSpeed);
    UI::UIToolkit::DrawButton(s_btnSpeed, spdLabel.c_str(), false, UI::TextScale::Normal);

    UI::UIToolkit::DrawToggleButton(s_btnClay, m_clayMode ? "CLAY:SI" : "CLAY:NO", m_clayMode, false, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnResetCam, "CAM:RESET", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnVolverBot, "< MENU (MINUS)", false, UI::TextScale::Normal);

    // 5. Barra Guía de Controles
    UI::UIToolkit::DrawGuideBar("L: CAPA WALK+WAVE | R: MODO IK | A/B/X: CROSS-FADE | STICK R: ORBITAR | -: MENU", 636.0f, UI::TextScale::Normal);

    // 6. Barra Inferior de Telemetría
    StackString<256> telemetria;
    telemetria.Format("FPS: %.1f | CLIP: %s (%.1fx) | HUESOS: %u | VERTS: %u | TRIS: %u | LBS: GPU AMD LATTE",
                      m_fps, clipName, m_playbackSpeed, m_skeleton.jointCount, m_characterVertexCount, m_trianglesCount);
    UI::UIToolkit::DrawTelemetryBar(telemetria.c_str(), 672.0f, UI::TextScale::Normal);
#endif
}

void SceneSkeletalAnimationGX2::Render(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_shaderLoaded || !m_characterBuffersCreated || !m_depthBufferCreated) {
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

} // namespace WiiU
