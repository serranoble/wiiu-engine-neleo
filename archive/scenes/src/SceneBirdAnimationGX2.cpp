#include "Scene/SceneBirdAnimationGX2.hpp"
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
#include "Graphics/UI/UIToolkit.hpp"
#include "Graphics/GX2StateHelper.hpp"
#include "Graphics/SkinningShaderBinary.hpp"
#include "Graphics/Animation/AnimationLoader.hpp"
#include "Core/ECS/Systems/AnimationSystem.hpp"
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

constexpr UI::UIRect s_panelInfo  {18.0f, 64.0f, 350.0f, 160.0f};
constexpr UI::UIRect s_panelTelem {922.0f, 64.0f, 340.0f, 160.0f};

constexpr UI::UIRect s_btnZoomIn  {1180.0f, 240.0f, 80.0f, 44.0f};
constexpr UI::UIRect s_btnZoomOut {1180.0f, 292.0f, 80.0f, 44.0f};

constexpr UI::UIRect s_bottomBarPanel{12.0f, 576.0f, 1256.0f, 52.0f};
constexpr UI::UIRect s_btnClipSalto   {18.0f,  582.0f, 138.0f, 40.0f};
constexpr UI::UIRect s_btnClipPiar    {162.0f, 582.0f, 134.0f, 40.0f};
constexpr UI::UIRect s_btnClipMirar   {302.0f, 582.0f, 140.0f, 40.0f};
constexpr UI::UIRect s_btnClipCompleta{448.0f, 582.0f, 144.0f, 40.0f};
constexpr UI::UIRect s_btnPause       {598.0f, 582.0f, 132.0f, 40.0f};
constexpr UI::UIRect s_btnSpeed       {736.0f, 582.0f, 120.0f, 40.0f};
constexpr UI::UIRect s_btnClay        {862.0f, 582.0f, 120.0f, 40.0f};
constexpr UI::UIRect s_btnResetCam    {988.0f, 582.0f, 128.0f, 40.0f};
constexpr UI::UIRect s_btnVolverBot   {1122.0f, 582.0f, 140.0f, 40.0f};
}

SceneBirdAnimationGX2::SceneBirdAnimationGX2() = default;

SceneBirdAnimationGX2::~SceneBirdAnimationGX2() {
    DestroyGX2Pipeline();
}

void SceneBirdAnimationGX2::LoadAsync() {
    // 1. Carga asíncrona de archivos binarios en Core 2
    AssetManager::LoadBinaryFile("models/bird_orange.wmesh", m_stagedMesh);
    AssetManager::LoadBinaryFile("models/bird_orange.wskel", m_stagedSkel);
    AssetManager::LoadBinaryFile("models/bird_orange.wanim", m_stagedAnim);

    // 2. Parsear esqueleto y animaciones
    AnimationLoader::LoadSkeleton("models/bird_orange.wskel", m_skeleton);
    AnimationLoader::LoadClips("models/bird_orange.wanim", m_animationClips);
}

bool SceneBirdAnimationGX2::InitGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_shaderLoaded && m_birdBuffersCreated && m_depthBufferCreated && (m_nativeContextState != nullptr)) {
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

            m_directVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHeaderSize, MemoryTag::ShaderProgram, "BirdAnim_VS_Hdr"));
            m_directVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "BirdAnim_VS_Prog");
            m_directPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHeaderSize, MemoryTag::ShaderProgram, "BirdAnim_PS_Hdr"));
            m_directPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "BirdAnim_PS_Prog");

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
                    m_directFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "BirdAnim_FS_Prog");
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

    // 2. Subir buffers VBO/IBO del pájaro a GPU
    if (!m_birdBuffersCreated && !m_stagedMesh.empty() && m_stagedMesh.size() >= 96) {
        const auto* hdr = reinterpret_cast<const MeshHeader*>(m_stagedMesh.data());
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
                std::memcpy(vboPtr, m_stagedMesh.data() + hdr->vboOffset, hdr->vboSize);
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
                std::memcpy(iboPtr, m_stagedMesh.data() + hdr->iboOffset, hdr->iboSize);
                GX2RUnlockBufferEx(&m_birdIBO, GX2R_RESOURCE_BIND_NONE);
            }
        }

        m_birdBuffersCreated = true;
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

    // 4. Alocar Context State nativo
    if (!m_nativeContextState) {
        m_nativeContextState = static_cast<GX2ContextState*>(
            MemoryManager::Instance().AllocAligned(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState),
                                                  MemoryTag::ContextState, "BirdAnim_ContextState")
        );
        if (m_nativeContextState) {
            GX2SetupContextStateEx(m_nativeContextState, TRUE);
        }
    }

    return m_shaderLoaded && m_birdBuffersCreated && m_depthBufferCreated && (m_nativeContextState != nullptr);
#else
    return true;
#endif
}

void SceneBirdAnimationGX2::DestroyGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIIsRunning() && ProcUIInForeground()) {
        GX2DrawDone();
    }

    if (m_birdBuffersCreated) {
        GX2RDestroyBufferEx(&m_birdVBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_birdIBO, GX2R_RESOURCE_BIND_NONE);
        m_birdBuffersCreated = false;
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

void SceneBirdAnimationGX2::OnEnter() {
    AudioManager::Instance().PlayBGM();
    m_clayMode = (EngineConfig::Get().graphics.renderMode == ShadingRenderMode::Clay);

    // 1. Si no se ejecutó LoadAsync, cargar síncronamente
    if (m_skeleton.jointCount == 0) {
        AnimationLoader::LoadSkeleton("models/bird_orange.wskel", m_skeleton);
        AnimationLoader::LoadClips("models/bird_orange.wanim", m_animationClips);
    }
    if (m_stagedMesh.empty()) {
        AssetManager::LoadBinaryFile("models/bird_orange.wmesh", m_stagedMesh);
    }

    // 2. Inicializar pipeline de hardware
    InitGX2Pipeline();

    // 3. Crear entidad del pájaro esquelético
    m_birdEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_birdEntity, Point3D{0.0f, 0.0f, 0.0f});
    auto& meshComp = m_ecsWorld.EmplaceComponent<MeshComponent>(m_birdEntity);
#if defined(__WIIU__) || defined(__wiiu__)
    meshComp.vbo = &m_birdVBO;
    meshComp.ibo = &m_birdIBO;
    meshComp.indexCount = m_birdIndexCount;
    meshComp.vertexCount = m_birdVertexCount;
    meshComp.vertexStride = SKINNED_VERTEX_STRIDE;
    meshComp.primitiveMode = GX2_PRIMITIVE_MODE_TRIANGLES;
#endif
    m_ecsWorld.EmplaceComponent<MaterialComponent>(m_birdEntity, 1.0f, 1.0f, 0.35f, 0.0f, BlendMode::Opaque, true, true);
    m_ecsWorld.EmplaceComponent<SkeletalMeshComponent>(m_birdEntity, &m_skeleton);

    const AnimationClip* initClip = (!m_animationClips.empty()) ? &m_animationClips[0] : nullptr;
    m_ecsWorld.EmplaceComponent<AnimatorComponent>(m_birdEntity, initClip, true, 1.0f);
    m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_birdEntity, Point3D{0.0f, 1.4f, 0.0f}, 2.4f);

    // 4. Cámara orbital con Spring-Arm y Turntable
    m_camTarget = Point3D{0.0f, 1.4f, 0.0f};
    m_targetCamTarget = m_camTarget;
    m_camYaw = 0.45f;
    m_targetCamYaw = 0.45f;
    m_camPitch = 0.20f;
    m_targetCamPitch = 0.20f;
    m_camDistance = 5.2f;
    m_targetCamDistance = 5.2f;

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
    AnimationSystem::Update(m_ecsWorld, 0.001f);
    StudioTurntableSystem::Update(m_ecsWorld, 0.001f);
    SpringArmSystem::Update(m_ecsWorld, 0.001f);
    TransformSystem::Update(m_ecsWorld);
    CameraSystem::Update(m_ecsWorld);

    m_stagedMesh.clear();
    m_stagedMesh.shrink_to_fit();
    m_stagedSkel.clear();
    m_stagedSkel.shrink_to_fit();
    m_stagedAnim.clear();
    m_stagedAnim.shrink_to_fit();
}

void SceneBirdAnimationGX2::OnExit() {
    m_ecsWorld.Clear();
}

void SceneBirdAnimationGX2::SelectClip(int index) {
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

void SceneBirdAnimationGX2::Update(float dt) {
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
        SelectClip(m_activeClipIndex - 1);
        AudioManager::Instance().PlayClick();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_R) || input.IsButtonTriggered(WPAD_PRO_BUTTON_R)) {
        SelectClip(m_activeClipIndex + 1);
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
        m_targetCamTarget = Point3D{0.0f, 1.4f, 0.0f};
        m_targetCamYaw = 0.45f;
        m_targetCamPitch = 0.20f;
        m_targetCamDistance = 5.2f;
        if (auto* arm = m_ecsWorld.GetComponent<SpringArmComponent>(m_cameraEntity)) {
            arm->targetArmLength = 5.2f;
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
    // Zoom continuo con ZL / ZR
    if (input.IsButtonHeld(VPAD_BUTTON_ZL) || input.IsButtonHeld(WPAD_PRO_BUTTON_ZL)) {
        m_targetCamDistance = std::clamp(m_targetCamDistance - 6.5f * dt, 1.5f, 25.0f);
    }
    if (input.IsButtonHeld(VPAD_BUTTON_ZR) || input.IsButtonHeld(WPAD_PRO_BUTTON_ZR)) {
        m_targetCamDistance = std::clamp(m_targetCamDistance + 6.5f * dt, 1.5f, 25.0f);
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_UP)) {
        m_playbackSpeed = std::min(m_playbackSpeed + 0.25f, 2.5f);
        if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_birdEntity)) {
            anim->playbackSpeed = m_playbackSpeed;
        }
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_DOWN)) {
        m_playbackSpeed = std::max(m_playbackSpeed - 0.25f, 0.25f);
        if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_birdEntity)) {
            anim->playbackSpeed = m_playbackSpeed;
        }
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
            if (s_btnSpeed.Contains(tx, ty)) {
                if (m_playbackSpeed >= 1.9f) {
                    m_playbackSpeed = 0.25f;
                } else if (m_playbackSpeed >= 0.9f) {
                    m_playbackSpeed = 2.0f;
                } else if (m_playbackSpeed >= 0.45f) {
                    m_playbackSpeed = 1.0f;
                } else {
                    m_playbackSpeed = 0.5f;
                }
                if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_birdEntity)) {
                    anim->playbackSpeed = m_playbackSpeed;
                }
                AudioManager::Instance().PlayClick();
            }
            if (s_btnClay.Contains(tx, ty)) {
                m_clayMode = !m_clayMode;
                EngineConfig::Instance().graphics.renderMode = m_clayMode ? ShadingRenderMode::Clay : ShadingRenderMode::Textured;
                AudioManager::Instance().PlayClick();
            }
            if (s_btnZoomIn.Contains(tx, ty)) {
                m_targetCamDistance = std::clamp(m_targetCamDistance - 1.2f, 1.5f, 25.0f);
                if (auto* arm = m_ecsWorld.GetComponent<SpringArmComponent>(m_cameraEntity)) {
                    arm->targetArmLength = m_targetCamDistance;
                }
                AudioManager::Instance().PlayClick();
            }
            if (s_btnZoomOut.Contains(tx, ty)) {
                m_targetCamDistance = std::clamp(m_targetCamDistance + 1.2f, 1.5f, 25.0f);
                if (auto* arm = m_ecsWorld.GetComponent<SpringArmComponent>(m_cameraEntity)) {
                    arm->targetArmLength = m_targetCamDistance;
                }
                AudioManager::Instance().PlayClick();
            }
            if (s_btnResetCam.Contains(tx, ty)) {
                m_targetCamTarget = Point3D{0.0f, 1.4f, 0.0f};
                m_targetCamYaw = 0.45f;
                m_targetCamPitch = 0.20f;
                m_targetCamDistance = 5.2f;
                if (auto* arm = m_ecsWorld.GetComponent<SpringArmComponent>(m_cameraEntity)) {
                    arm->targetArmLength = 5.2f;
                }
                AudioManager::Instance().PlayClick();
            }
        }
    } else {
        m_isDraggingCamera = false;
    }

    // 4. Control orbital con sticks
    const auto stickL = input.GetLeftStick();
    const auto stickR = input.GetRightStick();
    if (std::abs(stickR.x) > 0.05f) m_targetCamYaw += stickR.x * dt * 2.2f;
    if (std::abs(stickR.y) > 0.05f) m_targetCamPitch = std::clamp(m_targetCamPitch + stickR.y * dt * 1.8f, -0.25f, 1.25f);
    if (std::abs(stickL.y) > 0.05f) {
        m_targetCamDistance = std::clamp(m_targetCamDistance - stickL.y * dt * 6.0f, 2.0f, 20.0f);
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

    if (const auto* tvCam = m_ecsWorld.GetComponent<CameraComponent>(m_cameraEntity)) {
        CullingSystem::Update(m_ecsWorld, *tvCam, 1280.0f, 720.0f);
    }

    // 6. Retorno con botón atrás
    if (input.IsBackRequested()) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
    }
}

void SceneBirdAnimationGX2::BindColorBuffer(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    m_pColorBuffer = GetSDL2ColorBuffer(renderer);
#else
    (void)renderer;
#endif
}

void SceneBirdAnimationGX2::RenderScene3D_GX2() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_nativeContextState || !m_pColorBuffer || !m_shaderLoaded || !m_birdBuffersCreated) return;

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

    // 2. Renderizar Pájaro con Deformación Esquelética en GPU Latte GX2
    GX2SetFetchShader(&m_directFs);
    GX2SetVertexShader(m_directVs);
    GX2SetPixelShader(m_directPs);

    const auto* transform = m_ecsWorld.GetComponent<TransformComponent>(m_birdEntity);
    Matrix4x4 model = transform ? transform->worldMatrix : Matrix4x4::Identity();
    Matrix4x4 mvp = vpMatrix * model;

    constexpr uint32_t TOTAL_VS_UNIFORM_FLOATS = 32 + 24 * 16;
    alignas(256) float vsUniforms[TOTAL_VS_UNIFORM_FLOATS];

    std::memcpy(&vsUniforms[0], mvp.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], model.m, 16 * sizeof(float));

    if (const auto* skelMesh = m_ecsWorld.GetComponent<SkeletalMeshComponent>(m_birdEntity)) {
        if (skelMesh->IsValid()) {
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
    } else {
        for (uint32_t j = 0; j < 24u; ++j) {
            std::memcpy(&vsUniforms[32 + j * 16], Matrix4x4::Identity().m, 16 * sizeof(float));
        }
    }

    GX2SetVertexUniformReg(0, TOTAL_VS_UNIFORM_FLOATS, vsUniforms);

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
    GX2RSetAttributeBuffer(&m_birdVBO, 0, SKINNED_VERTEX_STRIDE, 0);
    GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_birdIBO, GX2_INDEX_TYPE_U16, m_birdIndexCount, 0, 0, 1);

    m_drawCallsCount = 1;
    m_trianglesCount = m_birdIndexCount / 3;
#endif
}

void SceneBirdAnimationGX2::RenderHUD_GX2() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_pColorBuffer) return;

    UI::ScopedUIPass uiPass(m_nativeContextState, m_pColorBuffer);

    // 1. Barra Superior con Botón Volver y Título
    UI::UIToolkit::DrawPanel(s_topBarPanel);
    UI::UIToolkit::DrawButton(s_btnVolverTop, "< VOLVER", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawLabel(125.0f, 22.0f, "PAJARO ANIMADO Y LBS EN SILICIO (BIRD ORANGE FBX)", UI::TextScale::Normal, ColorRGBA::Cyan());

    // 2. Panel de Control de Animación (Izquierda)
    UI::UIToolkit::DrawPanel(s_panelInfo);
    UI::UIToolkit::DrawLabel(30.0f, 74.0f, "ESTADO DE ANIMACION (FBX)", UI::TextScale::Normal, ColorRGBA::Yellow());

    const char* clipName = (!m_animationClips.empty() && m_activeClipIndex < static_cast<int>(m_animationClips.size()))
        ? m_animationClips[m_activeClipIndex].name : "SALTO";
    float clipDur = (!m_animationClips.empty() && m_activeClipIndex < static_cast<int>(m_animationClips.size()))
        ? m_animationClips[m_activeClipIndex].duration : 0.65f;
    StackString<64> clipLabel;
    clipLabel.Format("CLIP [%d/4]: %s (%.2fs)", m_activeClipIndex + 1, clipName, clipDur);
    UI::UIToolkit::DrawLabel(30.0f, 104.0f, clipLabel.c_str(), UI::TextScale::Normal, ColorRGBA::Green());

    float pTime = 0.0f;
    if (const auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_birdEntity)) {
        pTime = anim->playbackTime;
    }
    StackString<64> timeLabel;
    timeLabel.Format("TIEMPO: %.2fs (%.1fx)", pTime, m_playbackSpeed);
    UI::UIToolkit::DrawLabel(30.0f, 134.0f, timeLabel.c_str(), UI::TextScale::Normal, ColorRGBA::White());

    StackString<64> stateLabel;
    stateLabel.Format("ESTADO: %s | %s", m_isPaused ? "PAUSADO" : "PLAY", m_clayMode ? "CLAY" : "BASECOLOR");
    UI::UIToolkit::DrawLabel(30.0f, 164.0f, stateLabel.c_str(), UI::TextScale::Normal, ColorRGBA{255, 200, 100, 255});

    UI::UIToolkit::DrawLabel(30.0f, 194.0f, "SPRING-ARM ANTI-CLIPPING: ACTIVO", UI::TextScale::Small, ColorRGBA::Cyan());

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

    // 4. Barra Inferior de Botones Táctiles Interactivos (Selector de Clips de Animación)
    UI::UIToolkit::DrawPanel(s_bottomBarPanel);
    UI::UIToolkit::DrawButton(s_btnClipSalto, "1: SALTO (A)", m_activeClipIndex == 0, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnClipPiar, "2: PIAR (X)", m_activeClipIndex == 1, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnClipMirar, "3: MIRAR (Y)", m_activeClipIndex == 2, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnClipCompleta, "4: TODO (B)", m_activeClipIndex == 3, UI::TextScale::Normal);
    UI::UIToolkit::DrawToggleButton(s_btnPause, m_isPaused ? "PAUSA:SI" : "PAUSA:NO", m_isPaused, false, UI::TextScale::Normal);

    StackString<32> spdLabel;
    spdLabel.Format("VEL:%.1fx", m_playbackSpeed);
    UI::UIToolkit::DrawButton(s_btnSpeed, spdLabel.c_str(), false, UI::TextScale::Normal);

    UI::UIToolkit::DrawToggleButton(s_btnClay, m_clayMode ? "CLAY:SI" : "CLAY:NO", m_clayMode, false, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnResetCam, "CAM (R3)", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnVolverBot, "MENU (-)", false, UI::TextScale::Normal);

    // 5. Barra Guía de Controles
    UI::UIToolkit::DrawGuideBar("ZL/ZR o STICK L: ZOOM | STICK R: ORBITA | A/X/Y/B: CLIPS | +: CLAY | -: MENU", 636.0f, UI::TextScale::Normal);

    // 6. Barra Inferior de Telemetría
    StackString<256> telemetria;
    telemetria.Format("FPS: %.1f | CLIP: %s (%.1fx) | HUESOS: %u | VERTS: %u | TRIS: %u | LBS: GPU AMD LATTE",
                      m_fps, clipName, m_playbackSpeed, m_skeleton.jointCount, m_birdVertexCount, m_trianglesCount);
    UI::UIToolkit::DrawTelemetryBar(telemetria.c_str(), 672.0f, UI::TextScale::Normal);
#endif
}

void SceneBirdAnimationGX2::Render(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_shaderLoaded || !m_birdBuffersCreated || !m_depthBufferCreated) {
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
