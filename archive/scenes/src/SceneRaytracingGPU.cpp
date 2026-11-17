#include "Scene/SceneRaytracingGPU.hpp"
#include "Scene/SceneMenu.hpp"
#include "Scene/SceneMenuGX2.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/AssetManager.hpp"
#include "Core/StackString.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include "Graphics/GX2StateHelper.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Core/MemoryManager.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <malloc.h>
#include "Graphics/RaytracingShaderBinary.hpp"

#if defined(__WIIU__) || defined(__wiiu__)
#include <gfd.h>
#include <coreinit/thread.h>
#include <gx2/event.h>
#include <proc_ui/procui.h>
#endif

namespace WiiU {

static inline int clampInt(int val, int minV, int maxV) {
    return (val < minV) ? minV : ((val > maxV) ? maxV : val);
}

SceneRaytracingGPU::SceneRaytracingGPU() {
    // Fila 1 (Y = 15): Volver, Buffer Interno y Target Salida HD
    m_backBtnRect   = { 15, 15, 130, 44 };
    m_qualBtnRect   = { 160, 15, 540, 44 };
    m_targetBtnRect = { 715, 15, 550, 44 };

    // Fila 2 (Y = 66): Aceleración GPU, Multihilo CPU y Suavizado AA
    m_gpuBtnRect    = { 15, 66, 380, 44 };
    m_cpuBtnRect    = { 410, 66, 380, 44 };
    m_smoothBtnRect = { 805, 66, 460, 44 };

    m_camPivot = Point3D{0.0f, 0.0f, 2.8f};
    m_camDistance = 3.6f;
    m_camYaw = 0.0f;
    m_camPitch = 0.0f;

    m_smoothingMode = SmoothingMode::BILINEAR_GX2;
    m_outputTarget = OutputTargetHD::TARGET_1080P_FULLHD;

    // Esfera 0: Cyan Espejo
    m_sceneUniforms.spheres[0].center = Point3D{-0.9f, 0.0f, 3.0f};
    m_sceneUniforms.spheres[0].radius = 0.75f;
    m_sceneUniforms.spheres[0].radiusSq = 0.75f * 0.75f;
    m_sceneUniforms.spheres[0].reflectivity = 0.65f;
    m_sceneUniforms.spheres[0].color = ColorRGBA{0, 220, 255, 255};

    // Esfera 1: Dorado Metálico
    m_sceneUniforms.spheres[1].center = Point3D{0.9f, 0.2f, 3.2f};
    m_sceneUniforms.spheres[1].radius = 0.8f;
    m_sceneUniforms.spheres[1].radiusSq = 0.8f * 0.8f;
    m_sceneUniforms.spheres[1].reflectivity = 0.55f;
    m_sceneUniforms.spheres[1].color = ColorRGBA{255, 215, 0, 255};

    // Cubo Magenta
    m_sceneUniforms.box.center = Point3D{0.0f, -0.3f, 2.3f};
    m_sceneUniforms.box.halfExtents = Point3D{0.42f, 0.42f, 0.42f};
    m_sceneUniforms.box.reflectivity = 0.40f;
    m_sceneUniforms.box.color = ColorRGBA{235, 50, 150, 255};

    SetQuality(RayQualityGPU::FULL_1080P_1920x1080, nullptr);
    InitThreadPool();
}

SceneRaytracingGPU::~SceneRaytracingGPU() {
    OnExit();
}

void SceneRaytracingGPU::SetQuality(RayQualityGPU quality, SDL_Renderer* renderer) {
    m_quality = quality;
    switch (quality) {
        case RayQualityGPU::GAMEPAD_480P_854x480:
            m_gpuWidth = 854;
            m_gpuHeight = 480;
            break;
        case RayQualityGPU::QHD_540P_960x540:
            m_gpuWidth = 960;
            m_gpuHeight = 540;
            break;
        case RayQualityGPU::HD_576P_1024x576:
            m_gpuWidth = 1024;
            m_gpuHeight = 576;
            break;
        case RayQualityGPU::HD_648P_1152x648:
            m_gpuWidth = 1152;
            m_gpuHeight = 648;
            break;
        case RayQualityGPU::FULL_720P_1280x720:
            m_gpuWidth = 1152; // Buffer interno 648p (1152x648) escalado a 720p HD
            m_gpuHeight = 648;
            m_outputTarget = OutputTargetHD::TARGET_720P_HD;
            break;
        case RayQualityGPU::FULL_1080P_1920x1080:
            m_gpuWidth = 1152; // Buffer interno 648p (1152x648) escalado a 1080p Full HD
            m_gpuHeight = 648;
            m_outputTarget = OutputTargetHD::TARGET_1080P_FULLHD;
            break;
    }

    m_pixelBuffer.assign(m_gpuWidth * m_gpuHeight, 0x050A19FF);
    m_screenPlaneDirs.resize(m_gpuWidth * m_gpuHeight);

    float aspect = static_cast<float>(m_gpuWidth) / static_cast<float>(m_gpuHeight);
    for (int y = 0; y < m_gpuHeight; ++y) {
        float vy = (1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(m_gpuHeight));
        for (int x = 0; x < m_gpuWidth; ++x) {
            float vx = (2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(m_gpuWidth) - 1.0f) * aspect;
            m_screenPlaneDirs[y * m_gpuWidth + x] = Point3D{vx, vy, 1.0f}.normalized();
        }
    }

    if (m_gpuSurface) {
        SDL_FreeSurface(m_gpuSurface);
        m_gpuSurface = nullptr;
    }
    m_gpuSurface = SDL_CreateRGBSurfaceWithFormat(0, m_gpuWidth, m_gpuHeight, 32, SDL_PIXELFORMAT_RGBA8888);

    if (m_gpuTexture) {
        SDL_DestroyTexture(m_gpuTexture);
        m_gpuTexture = nullptr;
    }

    if (renderer) {
        m_gpuTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, 
                                         SDL_TEXTUREACCESS_STREAMING, m_gpuWidth, m_gpuHeight);
        if (!m_gpuTexture) {
            m_gpuTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, 
                                             SDL_TEXTUREACCESS_STATIC, m_gpuWidth, m_gpuHeight);
        }
        if (m_gpuTexture) {
            SDL_ScaleMode scaleMode = (m_smoothingMode == SmoothingMode::OFF_NEAREST) ? SDL_ScaleModeNearest : SDL_ScaleModeLinear;
            SDL_SetTextureScaleMode(m_gpuTexture, scaleMode);
        }
    }
}

void SceneRaytracingGPU::SetSmoothingMode(SmoothingMode mode) {
    m_smoothingMode = mode;
    if (m_gpuTexture) {
        SDL_ScaleMode scaleMode = (m_smoothingMode == SmoothingMode::OFF_NEAREST) ? SDL_ScaleModeNearest : SDL_ScaleModeLinear;
        SDL_SetTextureScaleMode(m_gpuTexture, scaleMode);
    }
}

void SceneRaytracingGPU::InitRayGrid() {
    float cosP = std::cos(m_camPitch);
    float sinP = std::sin(m_camPitch);
    float cosY = std::cos(m_camYaw);
    float sinY = std::sin(m_camYaw);

    Point3D offset{
        m_camDistance * sinY * cosP,
        m_camDistance * sinP,
        -m_camDistance * cosY * cosP
    };

    m_camPos = m_camPivot + offset;

    m_camForward = (m_camPivot - m_camPos).normalized();
    Point3D worldUp{0.0f, 1.0f, 0.0f};
    m_camRight = Point3D::cross(worldUp, m_camForward).normalized();
    m_camUp = Point3D::cross(m_camForward, m_camRight).normalized();
}

void SceneRaytracingGPU::UpdateBoundingBoxes() {
    float aspect = (m_gpuHeight > 0) ? (static_cast<float>(m_gpuWidth) / static_cast<float>(m_gpuHeight)) : (16.0f / 9.0f);
    for (auto& s : m_sceneUniforms.spheres) {
        Point3D rel = s.center - m_camPos;
        float cx = Point3D::dot(rel, m_camRight);
        float cy = Point3D::dot(rel, m_camUp);
        float cz = Point3D::dot(rel, m_camForward);

        if (cz <= 0.6f) {
            s.bbox = {0, m_gpuWidth - 1, 0, m_gpuHeight - 1};
        } else {
            float r = s.radius * 1.25f;
            float invZ = 1.0f / cz;
            float vxMin = (cx - r) * invZ;
            float vxMax = (cx + r) * invZ;
            float vyMin = (cy - r) * invZ;
            float vyMax = (cy + r) * invZ;

            int xMin = clampInt(static_cast<int>(((vxMin / aspect + 1.0f) * 0.5f) * m_gpuWidth), 0, m_gpuWidth - 1);
            int xMax = clampInt(static_cast<int>(((vxMax / aspect + 1.0f) * 0.5f) * m_gpuWidth) + 1, 0, m_gpuWidth - 1);
            int yMin = clampInt(static_cast<int>(((1.0f - vyMax) * 0.5f) * m_gpuHeight), 0, m_gpuHeight - 1);
            int yMax = clampInt(static_cast<int>(((1.0f - vyMin) * 0.5f) * m_gpuHeight) + 1, 0, m_gpuHeight - 1);

            s.bbox = {xMin, xMax, yMin, yMax};
        }
    }

    // Box Bounding Box
    Point3D boxRel = m_sceneUniforms.box.center - m_camPos;
    float cx = Point3D::dot(boxRel, m_camRight);
    float cy = Point3D::dot(boxRel, m_camUp);
    float cz = Point3D::dot(boxRel, m_camForward);

    if (cz <= 0.85f) {
        m_sceneUniforms.box.bbox = {0, m_gpuWidth - 1, 0, m_gpuHeight - 1};
    } else {
        float r = 0.85f;
        float invZ = 1.0f / cz;
        float vxMin = (cx - r) * invZ;
        float vxMax = (cx + r) * invZ;
        float vyMin = (cy - r) * invZ;
        float vyMax = (cy + r) * invZ;

        int xMin = clampInt(static_cast<int>(((vxMin / aspect + 1.0f) * 0.5f) * m_gpuWidth), 0, m_gpuWidth - 1);
        int xMax = clampInt(static_cast<int>(((vxMax / aspect + 1.0f) * 0.5f) * m_gpuWidth) + 1, 0, m_gpuWidth - 1);
        int yMin = clampInt(static_cast<int>(((1.0f - vyMax) * 0.5f) * m_gpuHeight), 0, m_gpuHeight - 1);
        int yMax = clampInt(static_cast<int>(((1.0f - vyMin) * 0.5f) * m_gpuHeight) + 1, 0, m_gpuHeight - 1);

        m_sceneUniforms.box.bbox = {xMin, xMax, yMin, yMax};
    }
}

void SceneRaytracingGPU::UpdateGPUUniforms() {
    m_sceneUniforms.camera.pos = m_camPos;
    m_sceneUniforms.camera.forward = m_camForward;
    m_sceneUniforms.camera.right = m_camRight;
    m_sceneUniforms.camera.up = m_camUp;
    m_sceneUniforms.camera.aspect = (m_gpuHeight > 0) ? (static_cast<float>(m_gpuWidth) / static_cast<float>(m_gpuHeight)) : (16.0f / 9.0f);
    m_sceneUniforms.camera.fov = 60.0f;

    m_sceneUniforms.spheres[0].center.y = std::sin(m_animTime * 1.5f) * 0.4f;
    m_sceneUniforms.spheres[1].center.y = std::cos(m_animTime * 1.5f) * 0.4f;
    m_sceneUniforms.box.center.x        = std::sin(m_animTime * 2.0f) * 0.6f;

    m_lightPos.x = std::sin(m_animTime) * 3.0f;
    m_lightPos.z = 1.0f + std::cos(m_animTime) * 2.0f;

    m_sceneUniforms.lightPos = m_lightPos;
    m_sceneUniforms.animTime = m_animTime;

    UpdateBoundingBoxes();
}

bool SceneRaytracingGPU::InitGX2Pipeline() {
#ifdef __wiiu__
    if (m_gfdShaderLoaded && m_quadBufferCreated) {
        return true;
    }

    if (!m_gfdShaderLoaded) {
        std::vector<uint8_t> buffer;
        bool loaded = AssetManager::LoadBinaryFile("shaders/raytracing.gsh", buffer);
        if (!loaded || buffer.empty()) {
            buffer.assign(RAYTRACING_GSH_DATA, RAYTRACING_GSH_DATA + RAYTRACING_GSH_SIZE);
            loaded = true;
        }

        if (loaded && !buffer.empty()) {
            // Direct GFD sin dependencia de heaps de WHB
            uint32_t vsHeaderSize = GFDGetVertexShaderHeaderSize(0, buffer.data());
            uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, buffer.data());
            uint32_t psHeaderSize = GFDGetPixelShaderHeaderSize(0, buffer.data());
            uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, buffer.data());

                if (vsHeaderSize > 0 && vsProgSize > 0 && psHeaderSize > 0 && psProgSize > 0) {
                    m_directVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHeaderSize, MemoryTag::ShaderProgram, "RaytracingGPU_VS_Hdr"));
                    m_directVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "RaytracingGPU_VS_Prog");
                    m_directPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHeaderSize, MemoryTag::ShaderProgram, "RaytracingGPU_PS_Hdr"));
                    m_directPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "RaytracingGPU_PS_Prog");

                    if (m_directVs && m_directVsProgram && m_directPs && m_directPsProgram) {
                        BOOL vsOk = GFDGetVertexShader(m_directVs, m_directVsProgram, 0, buffer.data());
                        BOOL psOk = GFDGetPixelShader(m_directPs, m_directPsProgram, 0, buffer.data());
                        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directVsProgram, vsProgSize);
                        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directPsProgram, psProgSize);

                        if (vsOk && psOk) {
                            GX2AttribStream attrs[2];
                            std::memset(attrs, 0, sizeof(attrs));
                            attrs[0].location = 0;
                            attrs[0].buffer = 0;
                            attrs[0].offset = 0;
                            attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
                            attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attrs[0].aluDivisor = 0;
                            attrs[0].mask = 0x00010405;
                            attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            attrs[1].location = 1;
                            attrs[1].buffer = 0;
                            attrs[1].offset = 8;
                            attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
                            attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attrs[1].aluDivisor = 0;
                            attrs[1].mask = 0x00010405;
                            attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            uint32_t fsSize = GX2CalcFetchShaderSizeEx(2, GX2_FETCH_SHADER_TESSELLATION_NONE,
                                                                       GX2_TESSELLATION_MODE_DISCRETE);
                            m_directFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "RaytracingGPU_FS_Prog");
                            if (m_directFsProgram) {
                                GX2InitFetchShaderEx(&m_directFetchShader, static_cast<uint8_t*>(m_directFsProgram), 2, attrs,
                                                     GX2_FETCH_SHADER_TESSELLATION_NONE,
                                                     GX2_TESSELLATION_MODE_DISCRETE);
                                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directFsProgram, fsSize);
                                m_useDirectGFD = true;
                                m_gfdShaderLoaded = true;
                            }
                        }
                    }
                }
            }
        }

    if (m_gfdShaderLoaded && !m_quadBufferCreated) {
        struct QuadVertex {
            float x, y;
            float u, v;
        };

        QuadVertex verts[6] = {
            // Triángulo 1
            { -1.0f, -1.0f,  0.0f, 0.0f },
            {  1.0f, -1.0f,  1.0f, 0.0f },
            { -1.0f,  1.0f,  0.0f, 1.0f },

            // Triángulo 2
            { -1.0f,  1.0f,  0.0f, 1.0f },
            {  1.0f, -1.0f,  1.0f, 0.0f },
            {  1.0f,  1.0f,  1.0f, 1.0f }
        };

        m_quadBuffer.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_quadBuffer.elemSize = sizeof(QuadVertex);
        m_quadBuffer.elemCount = 6;

        bool vboOk = false;
        if (GX2RCreateBuffer(&m_quadBuffer)) {
            void* ptr = GX2RLockBufferEx(&m_quadBuffer, GX2R_RESOURCE_BIND_NONE);
            if (ptr) {
                std::memcpy(ptr, verts, sizeof(verts));
                GX2RUnlockBufferEx(&m_quadBuffer, GX2R_RESOURCE_BIND_NONE);
                vboOk = true;
            }
        }

        m_quadBufferCreated = vboOk;
    }

    if (!m_nativeContextState) {
        m_nativeContextState = static_cast<GX2ContextState*>(MemoryManager::Instance().AllocAligned(
            GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState), MemoryTag::ContextState, "RaytracingGPU_ContextState"));
        if (m_nativeContextState) {
            GX2SetupContextStateEx(m_nativeContextState, TRUE);
        }
    }

    return m_gfdShaderLoaded && m_quadBufferCreated;
#else
    return false;
#endif
}

void SceneRaytracingGPU::DestroyGX2Pipeline() {
#ifdef __wiiu__
    if (ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown()) {
        GX2DrawDone();
    }
    if (m_nativeContextState) {
        void* rawCtx = static_cast<void*>(m_nativeContextState);
        MemoryManager::Instance().FreeAligned(rawCtx);
        m_nativeContextState = nullptr;
    }
    if (m_quadBufferCreated) {
        GX2RDestroyBufferEx(&m_quadBuffer, GX2R_RESOURCE_BIND_NONE);
        m_quadBufferCreated = false;
    }
    if (m_useDirectGFD) {
        if (m_directFsProgram) { MemoryManager::Instance().FreeAligned(m_directFsProgram); }
        if (m_directPsProgram) { MemoryManager::Instance().FreeAligned(m_directPsProgram); }
        if (m_directPs) { void* raw = static_cast<void*>(m_directPs); MemoryManager::Instance().Free(raw); m_directPs = nullptr; }
        if (m_directVsProgram) { MemoryManager::Instance().FreeAligned(m_directVsProgram); }
        if (m_directVs) { void* raw = static_cast<void*>(m_directVs); MemoryManager::Instance().Free(raw); m_directVs = nullptr; }
        m_useDirectGFD = false;
        m_gfdShaderLoaded = false;
    } else if (m_gfdShaderLoaded) {
        WHBGfxFreeShaderGroup(&m_gfdShaderGroup);
        m_gfdShaderLoaded = false;
    }
    m_pColorBuffer = nullptr;
    m_colorBufferBound = false;
#endif
}

void SceneRaytracingGPU::BindColorBuffer(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!renderer) return;

    m_pColorBuffer = GetSDL2ColorBuffer(renderer);
    if (m_pColorBuffer && m_nativeContextState) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
        m_colorBufferBound = true;
    }
#else
    (void)renderer;
#endif
}

void SceneRaytracingGPU::RenderGX2() {
#ifdef __wiiu__
    if (!m_gfdShaderLoaded || !m_quadBufferCreated || !m_pColorBuffer) {
        return;
    }

    if (m_nativeContextState) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    }

    // Limpieza directa de color en silicio de la GPU Latte
    GX2ClearColor(m_pColorBuffer, 0.03f, 0.06f, 0.15f, 1.0f);

    // Re-activar Context State tras GX2ClearColor
    if (m_nativeContextState) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    }

    alignas(256) float uniforms[52];
    uniforms[0] = m_camPos.x;
    uniforms[1] = m_camPos.y;
    uniforms[2] = m_camPos.z;
    uniforms[3] = 0.0f;

    uniforms[4] = m_camForward.x;
    uniforms[5] = m_camForward.y;
    uniforms[6] = m_camForward.z;
    uniforms[7] = 0.0f;

    uniforms[8] = m_camRight.x;
    uniforms[9] = m_camRight.y;
    uniforms[10] = m_camRight.z;
    uniforms[11] = 0.0f;

    uniforms[12] = m_camUp.x;
    uniforms[13] = m_camUp.y;
    uniforms[14] = m_camUp.z;
    uniforms[15] = 0.0f;

    float aspect = (m_gpuHeight > 0) ? (static_cast<float>(m_gpuWidth) / static_cast<float>(m_gpuHeight)) : (16.0f / 9.0f);
    uniforms[16] = aspect;
    uniforms[17] = m_animTime;
    uniforms[18] = 0.0f;
    uniforms[19] = 0.0f;

    const auto& s0 = m_sceneUniforms.spheres[0];
    uniforms[20] = s0.center.x;
    uniforms[21] = s0.center.y;
    uniforms[22] = s0.center.z;
    uniforms[23] = s0.radius;

    uniforms[24] = static_cast<float>(s0.color.r) * (1.0f / 255.0f);
    uniforms[25] = static_cast<float>(s0.color.g) * (1.0f / 255.0f);
    uniforms[26] = static_cast<float>(s0.color.b) * (1.0f / 255.0f);
    uniforms[27] = s0.reflectivity;

    const auto& s1 = m_sceneUniforms.spheres[1];
    uniforms[28] = s1.center.x;
    uniforms[29] = s1.center.y;
    uniforms[30] = s1.center.z;
    uniforms[31] = s1.radius;

    uniforms[32] = static_cast<float>(s1.color.r) * (1.0f / 255.0f);
    uniforms[33] = static_cast<float>(s1.color.g) * (1.0f / 255.0f);
    uniforms[34] = static_cast<float>(s1.color.b) * (1.0f / 255.0f);
    uniforms[35] = s1.reflectivity;

    const auto& box = m_sceneUniforms.box;
    uniforms[36] = box.center.x;
    uniforms[37] = box.center.y;
    uniforms[38] = box.center.z;
    uniforms[39] = 0.0f;

    uniforms[40] = box.halfExtents.x;
    uniforms[41] = box.halfExtents.y;
    uniforms[42] = box.halfExtents.z;
    uniforms[43] = 0.0f;

    uniforms[44] = static_cast<float>(box.color.r) * (1.0f / 255.0f);
    uniforms[45] = static_cast<float>(box.color.g) * (1.0f / 255.0f);
    uniforms[46] = static_cast<float>(box.color.b) * (1.0f / 255.0f);
    uniforms[47] = box.reflectivity;

    uniforms[48] = m_lightPos.x;
    uniforms[49] = m_lightPos.y;
    uniforms[50] = m_lightPos.z;
    uniforms[51] = 0.0f;

    if (m_useDirectGFD) {
        GX2SetFetchShader(&m_directFetchShader);
        GX2SetVertexShader(m_directVs);
        GX2SetPixelShader(m_directPs);
    } else {
        GX2SetFetchShader(&m_gfdShaderGroup.fetchShader);
        GX2SetVertexShader(m_gfdShaderGroup.vertexShader);
        GX2SetPixelShader(m_gfdShaderGroup.pixelShader);
    }

    GX2SetPixelUniformReg(0, 52, uniforms);

    GX2RSetAttributeBuffer(&m_quadBuffer, 0, 16, 0);

    RHIPipelineState state(false, false, RHI_COMPARE_ALWAYS, BlendMode::Opaque, false, RHI_FRONT_FACE_CCW);
    RHIPipelineCache::Instance().BindState(state);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);

    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);

    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);
    GX2Flush();
#endif
}

void SceneRaytracingGPU::OnEnter() {
    m_animTime = 0.0f;
    m_lastFrameTicks = SDL_GetTicks();
    m_smoothedFps = 60.0f;
    AudioManager::Instance().StopBGM();

    InitGX2Pipeline();
    if (m_pixelBuffer.empty()) {
        SetQuality(m_quality, nullptr);
        InitThreadPool();
    }
}

void SceneRaytracingGPU::OnExit() {
    ShutdownThreadPool();
    DestroyGX2Pipeline();

    if (m_gpuSurface) {
        SDL_FreeSurface(m_gpuSurface);
        m_gpuSurface = nullptr;
    }

    if (m_gpuTexture) {
        SDL_DestroyTexture(m_gpuTexture);
        m_gpuTexture = nullptr;
    }

    // Desalojo proactivo de memoria en CPU heap (OPT-45)
    m_pixelBuffer.clear();
    m_pixelBuffer.shrink_to_fit();
    m_screenPlaneDirs.clear();
    m_screenPlaneDirs.shrink_to_fit();
}

void SceneRaytracingGPU::Update(float dt) {
    m_animTime += 1.5f * dt;
    m_frameParity = 1 - m_frameParity;

    // Control de Cámara Orbital
    auto& input = InputManager::Instance();
    const auto& rightStick = input.GetRightStick();

    if (std::abs(rightStick.x) > 0.05f || std::abs(rightStick.y) > 0.05f) {
        m_camYaw   += rightStick.x * 2.2f * dt;
        m_camPitch += rightStick.y * 2.2f * dt;

        constexpr float MAX_PITCH = 1.35f;
        m_camPitch = std::clamp(m_camPitch, -MAX_PITCH, MAX_PITCH);
    }

    InitRayGrid();
    UpdateGPUUniforms();

    if (m_clickEffect.active) {
        m_clickEffect.radius += 180.0f * dt;
        m_clickEffect.alpha -= 2.2f * dt;
        if (m_clickEffect.alpha <= 0.0f) {
            m_clickEffect.active = false;
        }
    }

    const auto& touch = input.GetTouchState();
    m_backHover   = false;
    m_qualHover   = false;
    m_targetHover = false;
    m_gpuHover    = false;
    m_cpuHover    = false;
    m_smoothHover = false;

    if (input.IsBackRequested()) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuGX2>());
        return;
    }

    if (touch.active) {
        if (touch.x >= m_backBtnRect.x && touch.x <= (m_backBtnRect.x + m_backBtnRect.w) &&
            touch.y >= m_backBtnRect.y && touch.y <= (m_backBtnRect.y + m_backBtnRect.h)) {
            m_backHover = true;
            if (touch.justPressed) {
                input.TriggerAction("VOLVER MENU");
                SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuGX2>());
                return;
            }
        }

        if (touch.x >= m_qualBtnRect.x && touch.x <= (m_qualBtnRect.x + m_qualBtnRect.w) &&
            touch.y >= m_qualBtnRect.y && touch.y <= (m_qualBtnRect.y + m_qualBtnRect.h)) {
            m_qualHover = true;
            if (touch.justPressed) {
                AudioManager::Instance().PlayClick();
                if (m_quality == RayQualityGPU::GAMEPAD_480P_854x480) {
                    SetQuality(RayQualityGPU::QHD_540P_960x540, nullptr);
                } else if (m_quality == RayQualityGPU::QHD_540P_960x540) {
                    SetQuality(RayQualityGPU::HD_576P_1024x576, nullptr);
                } else if (m_quality == RayQualityGPU::HD_576P_1024x576) {
                    SetQuality(RayQualityGPU::HD_648P_1152x648, nullptr);
                } else if (m_quality == RayQualityGPU::HD_648P_1152x648) {
                    SetQuality(RayQualityGPU::FULL_720P_1280x720, nullptr);
                } else if (m_quality == RayQualityGPU::FULL_720P_1280x720) {
                    SetQuality(RayQualityGPU::FULL_1080P_1920x1080, nullptr);
                } else {
                    SetQuality(RayQualityGPU::GAMEPAD_480P_854x480, nullptr);
                }
            }
        }

        if (touch.x >= m_targetBtnRect.x && touch.x <= (m_targetBtnRect.x + m_targetBtnRect.w) &&
            touch.y >= m_targetBtnRect.y && touch.y <= (m_targetBtnRect.y + m_targetBtnRect.h)) {
            m_targetHover = true;
            if (touch.justPressed) {
                AudioManager::Instance().PlayClick();
                if (m_outputTarget == OutputTargetHD::TARGET_720P_HD) {
                    m_outputTarget = OutputTargetHD::TARGET_1080P_FULLHD;
                } else {
                    m_outputTarget = OutputTargetHD::TARGET_720P_HD;
                }
            }
        }

        if (touch.x >= m_gpuBtnRect.x && touch.x <= (m_gpuBtnRect.x + m_gpuBtnRect.w) &&
            touch.y >= m_gpuBtnRect.y && touch.y <= (m_gpuBtnRect.y + m_gpuBtnRect.h)) {
            m_gpuHover = true;
            if (touch.justPressed) {
                AudioManager::Instance().PlayClick();
                m_gpuAccel = !m_gpuAccel;
            }
        }

        if (touch.x >= m_cpuBtnRect.x && touch.x <= (m_cpuBtnRect.x + m_cpuBtnRect.w) &&
            touch.y >= m_cpuBtnRect.y && touch.y <= (m_cpuBtnRect.y + m_cpuBtnRect.h)) {
            m_cpuHover = true;
            if (touch.justPressed) {
                AudioManager::Instance().PlayClick();
                m_multithreading = !m_multithreading;
            }
        }

        if (touch.x >= m_smoothBtnRect.x && touch.x <= (m_smoothBtnRect.x + m_smoothBtnRect.w) &&
            touch.y >= m_smoothBtnRect.y && touch.y <= (m_smoothBtnRect.y + m_smoothBtnRect.h)) {
            m_smoothHover = true;
            if (touch.justPressed) {
                AudioManager::Instance().PlayClick();
                if (m_smoothingMode == SmoothingMode::OFF_NEAREST) {
                    SetSmoothingMode(SmoothingMode::BILINEAR_GX2);
                } else if (m_smoothingMode == SmoothingMode::BILINEAR_GX2) {
                    SetSmoothingMode(SmoothingMode::SUBPIXEL_AA);
                } else if (m_smoothingMode == SmoothingMode::SUBPIXEL_AA) {
                    SetSmoothingMode(SmoothingMode::FXAA_FULL);
                } else if (m_smoothingMode == SmoothingMode::FXAA_FULL) {
                    SetSmoothingMode(SmoothingMode::BICUBIC_SHARPEN);
                } else {
                    SetSmoothingMode(SmoothingMode::OFF_NEAREST);
                }
            }
        }

        if (!m_backHover && !m_qualHover && !m_targetHover && !m_gpuHover && !m_cpuHover && !m_smoothHover) {
            if (touch.justPressed) {
                m_clickEffect.x = touch.x;
                m_clickEffect.y = touch.y;
                m_clickEffect.radius = 10.0f;
                m_clickEffect.alpha = 1.0f;
                m_clickEffect.active = true;
            }
        }
    }
}

void SceneRaytracingGPU::TraceSceneGPU(uint32_t* pixelBuffer, int width, int height, int pitchPixels) {
    if (m_multithreading) {
        m_currentPixelBuffer = pixelBuffer;
        m_currentWidth = width;
        m_currentHeight = height;
        m_currentPitchPixels = pitchPixels;
        m_completedWorkers.store(0, std::memory_order_relaxed);

        m_targetFrame.fetch_add(1, std::memory_order_release);

        TraceRowsGPU(0, NUM_WORKERS + 1, pixelBuffer, width, height, pitchPixels);

        while (m_completedWorkers.load(std::memory_order_acquire) < NUM_WORKERS) {
#ifdef __wiiu__
            OSYieldThread();
#else
            std::this_thread::yield();
#endif
        }
    } else {
        TraceRowsGPU(0, 1, pixelBuffer, width, height, pitchPixels);
    }
}

void SceneRaytracingGPU::InitThreadPool() {
    m_stopThreads = false;
    m_targetFrame = 0;
    m_completedWorkers = 0;
    for (int i = 0; i < NUM_WORKERS; ++i) {
        m_workerThreads.emplace_back(&SceneRaytracingGPU::WorkerLoop, this, i + 1);
    }
}

void SceneRaytracingGPU::ShutdownThreadPool() {
    m_stopThreads = true;
    m_targetFrame.fetch_add(1, std::memory_order_release);
    for (auto& thread : m_workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_workerThreads.clear();
}

void SceneRaytracingGPU::WorkerLoop(int threadId) {
#ifdef __wiiu__
    if (threadId == 1) {
        OSSetThreadAffinity(OSGetCurrentThread(), 1 << 1);
    } else if (threadId == 2) {
        OSSetThreadAffinity(OSGetCurrentThread(), 1 << 2);
    }
#endif

    int lastProcessedFrame = 0;
    while (!m_stopThreads) {
        int currentTarget = m_targetFrame.load(std::memory_order_acquire);
        if (currentTarget > lastProcessedFrame) {
            lastProcessedFrame = currentTarget;
            if (m_stopThreads) break;

            TraceRowsGPU(threadId, NUM_WORKERS + 1, m_currentPixelBuffer, m_currentWidth, m_currentHeight, m_currentPitchPixels);
            m_completedWorkers.fetch_add(1, std::memory_order_release);
        } else {
#ifdef __wiiu__
            OSYieldThread();
#else
            std::this_thread::yield();
#endif
        }
    }
}

static inline bool intersectSpherePrimaryGPU(const Point3D& rayDir, const GPUSphereUniform& sphere, const Point3D& sRel, float sCConst, float& tHit) {
    float b = rayDir.x * sRel.x + rayDir.y * sRel.y + rayDir.z * sRel.z;
    if (b <= 0.0f) return false;
    float disc = b * b - sCConst;
    if (disc < 0.0f) return false;
    float t0 = b - std::sqrtf(disc);
    if (t0 > 0.001f) {
        tHit = t0;
        return true;
    }
    return false;
}

static inline bool intersectBoxPrimaryGPUInv(const Point3D& invRayDir, const Point3D& boxMin, const Point3D& boxMax, float& tHit) {
    float t1 = boxMin.x * invRayDir.x; float t2 = boxMax.x * invRayDir.x;
    float t3 = boxMin.y * invRayDir.y; float t4 = boxMax.y * invRayDir.y;
    float t5 = boxMin.z * invRayDir.z; float t6 = boxMax.z * invRayDir.z;

    float tminX = (t1 < t2) ? t1 : t2;
    float tmaxX = (t1 > t2) ? t1 : t2;
    float tminY = (t3 < t4) ? t3 : t4;
    float tmaxY = (t3 > t4) ? t3 : t4;
    float tminZ = (t5 < t6) ? t5 : t6;
    float tmaxZ = (t5 > t6) ? t5 : t6;

    float tNear = (tminX > tminY) ? ((tminX > tminZ) ? tminX : tminZ) : ((tminY > tminZ) ? tminY : tminZ);
    float tFar  = (tmaxX < tmaxY) ? ((tmaxX < tmaxZ) ? tmaxX : tmaxZ) : ((tmaxY < tmaxZ) ? tmaxY : tmaxZ);

    if (tFar < tNear || tFar < 0.001f) return false;

    tHit = (tNear > 0.001f) ? tNear : tFar;
    return true;
}

void SceneRaytracingGPU::TraceRowsGPU(int threadId, int numThreads, uint32_t* pixelBuffer, int width, int height, int pitchPixels) {
    const auto& cam = m_sceneUniforms.camera;
    const auto& s0  = m_sceneUniforms.spheres[0];
    const auto& s1  = m_sceneUniforms.spheres[1];
    const auto& box = m_sceneUniforms.box;
    const auto& lightPos = m_sceneUniforms.lightPos;

    Point3D s0Rel = s0.center - cam.pos;
    float s0CConst = Point3D::dot(s0Rel, s0Rel) - s0.radiusSq;
    float s0InvRadius = (s0.radius > 0.0001f) ? (1.0f / s0.radius) : 1.0f;

    Point3D s1Rel = s1.center - cam.pos;
    float s1CConst = Point3D::dot(s1Rel, s1Rel) - s1.radiusSq;
    float s1InvRadius = (s1.radius > 0.0001f) ? (1.0f / s1.radius) : 1.0f;

    Point3D boxMin = (box.center - cam.pos) - box.halfExtents;
    Point3D boxMax = (box.center - cam.pos) + box.halfExtents;

    const float rxX = cam.right.x,   rxY = cam.right.y,   rxZ = cam.right.z;
    const float ryX = cam.up.x,      ryY = cam.up.y,      ryZ = cam.up.z;
    const float rzX = cam.forward.x, rzY = cam.forward.y, rzZ = cam.forward.z;

    int stepY  = m_gpuAccel ? 2 : 1;
    int startY = (m_gpuAccel ? m_frameParity : 0) + threadId * stepY;
    int strideY = numThreads * stepY;

    for (int y = startY; y < height; y += strideY) {
        int texRowOffset0 = y * pitchPixels;
        uint32_t* rowPtr0 = pixelBuffer + texRowOffset0;
        uint32_t* rowPtr1 = (m_gpuAccel && (y + 1 < height)) ? (pixelBuffer + (y + 1) * pitchPixels) : nullptr;

        int rowBase = y * width;

        bool yInS0  = (y >= s0.bbox.minY && y <= s0.bbox.maxY);
        bool yInS1  = (y >= s1.bbox.minY && y <= s1.bbox.maxY);
        bool yInBox = (y >= box.bbox.minY && y <= box.bbox.maxY);

        if (!yInS0 && !yInS1 && !yInBox) {
            std::fill_n(rowPtr0, width, 0x050A19FF);
            if (rowPtr1) {
                std::fill_n(rowPtr1, width, 0x050A19FF);
            }
            continue;
        }

        int minX = width;
        int maxX = -1;

        if (yInS0) {
            minX = std::min(minX, s0.bbox.minX);
            maxX = std::max(maxX, s0.bbox.maxX);
        }
        if (yInS1) {
            minX = std::min(minX, s1.bbox.minX);
            maxX = std::max(maxX, s1.bbox.maxX);
        }
        if (yInBox) {
            minX = std::min(minX, box.bbox.minX);
            maxX = std::max(maxX, box.bbox.maxX);
        }

        minX = clampInt(minX, 0, width - 1);
        maxX = clampInt(maxX, 0, width - 1);

        if (minX > 0) {
            std::fill_n(rowPtr0, minX, 0x050A19FF);
            if (rowPtr1) {
                std::fill_n(rowPtr1, minX, 0x050A19FF);
            }
        }

        if (maxX < width - 1) {
            std::fill_n(rowPtr0 + maxX + 1, width - 1 - maxX, 0x050A19FF);
            if (rowPtr1) {
                std::fill_n(rowPtr1 + maxX + 1, width - 1 - maxX, 0x050A19FF);
            }
        }

        for (int x = minX; x <= maxX; ++x) {
            bool testS0  = yInS0  && (x >= s0.bbox.minX && x <= s0.bbox.maxX);
            bool testS1  = yInS1  && (x >= s1.bbox.minX && x <= s1.bbox.maxX);
            bool testBox = yInBox && (x >= box.bbox.minX && x <= box.bbox.maxX);

            uint32_t packedColor = 0x050A19FF;

            if (testS0 || testS1 || testBox) {
                const Point3D& dirLocal = m_screenPlaneDirs[rowBase + x];
                Point3D rayDir{
                    rxX * dirLocal.x + ryX * dirLocal.y + rzX * dirLocal.z,
                    rxY * dirLocal.x + ryY * dirLocal.y + rzY * dirLocal.z,
                    rxZ * dirLocal.x + ryZ * dirLocal.y + rzZ * dirLocal.z
                };

                float nearestT = 1e9f;
                int hitObj = -1;

                if (testS0) {
                    float t0 = 0.0f;
                    if (intersectSpherePrimaryGPU(rayDir, s0, s0Rel, s0CConst, t0)) {
                        if (t0 < nearestT) {
                            nearestT = t0;
                            hitObj = 0;
                        }
                    }
                }

                if (testS1) {
                    float t1 = 0.0f;
                    if (intersectSpherePrimaryGPU(rayDir, s1, s1Rel, s1CConst, t1)) {
                        if (t1 < nearestT) {
                            nearestT = t1;
                            hitObj = 1;
                        }
                    }
                }

                if (testBox) {
                    float dx = (std::abs(rayDir.x) < 1e-6f) ? (rayDir.x < 0.0f ? -1e-6f : 1e-6f) : rayDir.x;
                    float dy = (std::abs(rayDir.y) < 1e-6f) ? (rayDir.y < 0.0f ? -1e-6f : 1e-6f) : rayDir.y;
                    float dz = (std::abs(rayDir.z) < 1e-6f) ? (rayDir.z < 0.0f ? -1e-6f : 1e-6f) : rayDir.z;
                    Point3D invDir{1.0f / dx, 1.0f / dy, 1.0f / dz};

                    float tB = 0.0f;
                    if (intersectBoxPrimaryGPUInv(invDir, boxMin, boxMax, tB)) {
                        if (tB < nearestT) {
                            nearestT = tB;
                            hitObj = 2;
                        }
                    }
                }

                if (hitObj >= 0) {
                    Point3D normal;
                    Point3D hitPos = cam.pos + rayDir * nearestT;
                    ColorRGBA baseColor = (hitObj == 0) ? s0.color : ((hitObj == 1) ? s1.color : box.color);
                    float reflectivity = (hitObj == 0) ? s0.reflectivity : ((hitObj == 1) ? s1.reflectivity : box.reflectivity);

                    if (hitObj == 0) {
                        normal = ((rayDir * nearestT) - s0Rel) * s0InvRadius;
                    } else if (hitObj == 1) {
                        normal = ((rayDir * nearestT) - s1Rel) * s1InvRadius;
                    } else {
                        Point3D localPos = hitPos - box.center;
                        Point3D absPos{std::abs(localPos.x) / box.halfExtents.x,
                                       std::abs(localPos.y) / box.halfExtents.y,
                                       std::abs(localPos.z) / box.halfExtents.z};
                        if (absPos.x >= absPos.y && absPos.x >= absPos.z) {
                            normal = Point3D{(localPos.x > 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f};
                        } else if (absPos.y >= absPos.x && absPos.y >= absPos.z) {
                            normal = Point3D{0.0f, (localPos.y > 0.0f) ? 1.0f : -1.0f, 0.0f};
                        } else {
                            normal = Point3D{0.0f, 0.0f, (localPos.z > 0.0f) ? 1.0f : -1.0f};
                        }
                    }

                    Point3D lightDir = (lightPos - hitPos).normalized();

                    Point3D shadowOrigin = hitPos + normal * 0.005f;
                    bool inShadow = false;

                    for (int i = 0; i < 2; ++i) {
                        if (hitObj == i) continue;
                        Point3D oc = shadowOrigin - m_sceneUniforms.spheres[i].center;
                        float b = Point3D::dot(oc, lightDir);
                        float c = Point3D::dot(oc, oc) - m_sceneUniforms.spheres[i].radiusSq;
                        float disc = b * b - c;
                        if (disc >= 0.0f) {
                            float tS = -b - std::sqrtf(disc);
                            if (tS > 0.001f) {
                                inShadow = true;
                                break;
                            }
                        }
                    }

                    float diff = inShadow ? 0.15f : std::max(0.20f, Point3D::dot(normal, lightDir));

                    ColorRGBA reflectColor{15, 25, 45, 255};
                    if (!inShadow && reflectivity > 0.05f) {
                        Point3D reflectDir = (rayDir - normal * (2.0f * Point3D::dot(rayDir, normal))).normalized();
                        float refNearestT = 1e9f;
                        int refHitObj = -1;
                        Point3D refNormal;

                        for (int i = 0; i < 2; ++i) {
                            if (hitObj == i) continue;
                            Point3D oc = shadowOrigin - m_sceneUniforms.spheres[i].center;
                            float b = Point3D::dot(oc, reflectDir);
                            float c = Point3D::dot(oc, oc) - m_sceneUniforms.spheres[i].radiusSq;
                            float disc = b * b - c;
                            if (disc >= 0.0f) {
                                float tR = -b - std::sqrtf(disc);
                                if (tR > 0.001f && tR < refNearestT) {
                                    refNearestT = tR;
                                    refHitObj = i;
                                    Point3D refHitPos = shadowOrigin + reflectDir * tR;
                                    refNormal = (refHitPos - m_sceneUniforms.spheres[i].center) * ((i == 0) ? s0InvRadius : s1InvRadius);
                                }
                            }
                        }

                        if (refHitObj >= 0) {
                            Point3D refHitPos = shadowOrigin + reflectDir * refNearestT;
                            Point3D refLightDir = (lightPos - refHitPos).normalized();
                            float refDiff = std::max(0.2f, Point3D::dot(refNormal, refLightDir));
                            ColorRGBA refObjColor = m_sceneUniforms.spheres[refHitObj].color;
                            reflectColor = ColorRGBA{
                                static_cast<Uint8>(refObjColor.r * refDiff),
                                static_cast<Uint8>(refObjColor.g * refDiff),
                                static_cast<Uint8>(refObjColor.b * refDiff),
                                255
                            };
                        }
                    }

                    float refl = reflectivity;
                    float invRefl = 1.0f - refl;
                    Uint8 r = static_cast<Uint8>(std::min(255.0f, baseColor.r * diff * invRefl + reflectColor.r * refl));
                    Uint8 g = static_cast<Uint8>(std::min(255.0f, baseColor.g * diff * invRefl + reflectColor.g * refl));
                    Uint8 b = static_cast<Uint8>(std::min(255.0f, baseColor.b * diff * invRefl + reflectColor.b * refl));

                    packedColor = (r << 24) | (g << 16) | (b << 8) | 0xFF;
                }
            }

            rowPtr0[x] = packedColor;
            if (rowPtr1) {
                rowPtr1[x] = packedColor;
            }
        }
    }
}

static inline uint32_t fastBlend50GPU(uint32_t c1, uint32_t c2) {
    return ((c1 & 0xFEFEFEFE) >> 1) + ((c2 & 0xFEFEFEFE) >> 1);
}

static inline uint32_t fastBlend75_25GPU(uint32_t c1, uint32_t c2) {
    uint32_t b50 = ((c1 & 0xFEFEFEFE) >> 1) + ((c2 & 0xFEFEFEFE) >> 1);
    return ((c1 & 0xFEFEFEFE) >> 1) + ((b50 & 0xFEFEFEFE) >> 1);
}

static inline uint8_t getFastLumaGPUInt(uint32_t c) {
    uint32_t r = (c >> 24) & 0xFF;
    uint32_t g = (c >> 16) & 0xFF;
    uint32_t b = (c >> 8) & 0xFF;
    return static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
}

static void ApplyBufferSmoothingGPU(uint32_t* buffer, int width, int height, int pitchPixels, SmoothingMode mode) {
    if (mode == SmoothingMode::OFF_NEAREST || mode == SmoothingMode::BILINEAR_GX2) {
        return;
    }

    static std::vector<uint32_t> tempBuffer;
    size_t requiredSize = static_cast<size_t>(height * pitchPixels);
    if (tempBuffer.size() < requiredSize) {
        tempBuffer.resize(requiredSize);
    }
    std::memcpy(tempBuffer.data(), buffer, requiredSize * sizeof(uint32_t));

    if (mode == SmoothingMode::FXAA_FULL) {
        for (int y = 1; y < height - 1; ++y) {
            int rowCurr = y * pitchPixels;
            int rowPrev = (y - 1) * pitchPixels;
            int rowNext = (y + 1) * pitchPixels;

            for (int x = 1; x < width - 1; ++x) {
                uint32_t m = tempBuffer[rowCurr + x];
                uint8_t lM = getFastLumaGPUInt(m);
                uint8_t lN = getFastLumaGPUInt(tempBuffer[rowPrev + x]);
                uint8_t lS = getFastLumaGPUInt(tempBuffer[rowNext + x]);
                uint8_t lE = getFastLumaGPUInt(tempBuffer[rowCurr + x + 1]);
                uint8_t lW = getFastLumaGPUInt(tempBuffer[rowCurr + x - 1]);

                uint8_t minL = std::min({lM, lN, lS, lE, lW});
                uint8_t maxL = std::max({lM, lN, lS, lE, lW});
                if ((maxL - minL) < 10) continue;

                uint32_t avgV = fastBlend50GPU(tempBuffer[rowPrev + x], tempBuffer[rowNext + x]);
                uint32_t avgH = fastBlend50GPU(tempBuffer[rowCurr + x - 1], tempBuffer[rowCurr + x + 1]);
                uint32_t avg4 = fastBlend50GPU(avgV, avgH);

                buffer[rowCurr + x] = fastBlend50GPU(m, avg4);
            }
        }
    } else if (mode == SmoothingMode::SUBPIXEL_AA) {
        for (int y = 1; y < height - 1; ++y) {
            int rowCurr = y * pitchPixels;
            int rowPrev = (y - 1) * pitchPixels;
            int rowNext = (y + 1) * pitchPixels;

            for (int x = 1; x < width - 1; ++x) {
                uint32_t m = tempBuffer[rowCurr + x];
                uint8_t lM = getFastLumaGPUInt(m);
                uint8_t lN = getFastLumaGPUInt(tempBuffer[rowPrev + x]);
                uint8_t lS = getFastLumaGPUInt(tempBuffer[rowNext + x]);
                uint8_t lE = getFastLumaGPUInt(tempBuffer[rowCurr + x + 1]);
                uint8_t lW = getFastLumaGPUInt(tempBuffer[rowCurr + x - 1]);

                if (std::abs(lM - lN) < 8 && std::abs(lM - lS) < 8 &&
                    std::abs(lM - lE) < 8 && std::abs(lM - lW) < 8) continue;

                uint32_t avgV = fastBlend50GPU(tempBuffer[rowPrev + x], tempBuffer[rowNext + x]);
                uint32_t avgH = fastBlend50GPU(tempBuffer[rowCurr + x - 1], tempBuffer[rowCurr + x + 1]);
                uint32_t avg4 = fastBlend50GPU(avgV, avgH);

                buffer[rowCurr + x] = fastBlend75_25GPU(m, avg4);
            }
        }
    } else if (mode == SmoothingMode::BICUBIC_SHARPEN) {
        for (int y = 1; y < height - 1; ++y) {
            int rowCurr = y * pitchPixels;
            int rowPrev = (y - 1) * pitchPixels;
            int rowNext = (y + 1) * pitchPixels;

            for (int x = 1; x < width - 1; ++x) {
                uint32_t m = tempBuffer[rowCurr + x];
                uint8_t lM = getFastLumaGPUInt(m);
                uint8_t lN = getFastLumaGPUInt(tempBuffer[rowPrev + x]);
                uint8_t lS = getFastLumaGPUInt(tempBuffer[rowNext + x]);
                uint8_t lE = getFastLumaGPUInt(tempBuffer[rowCurr + x + 1]);
                uint8_t lW = getFastLumaGPUInt(tempBuffer[rowCurr + x - 1]);

                if (std::abs(lM - lN) < 8 && std::abs(lM - lS) < 8 &&
                    std::abs(lM - lE) < 8 && std::abs(lM - lW) < 8) continue;

                uint32_t rM = (m >> 24) & 0xFF, gM = (m >> 16) & 0xFF, bM = (m >> 8) & 0xFF;
                uint32_t rN = (tempBuffer[rowPrev + x] >> 24) & 0xFF, rS = (tempBuffer[rowNext + x] >> 24) & 0xFF;
                uint32_t rE = (tempBuffer[rowCurr + x + 1] >> 24) & 0xFF, rW = (tempBuffer[rowCurr + x - 1] >> 24) & 0xFF;

                uint32_t gN = (tempBuffer[rowPrev + x] >> 16) & 0xFF, gS = (tempBuffer[rowNext + x] >> 16) & 0xFF;
                uint32_t gE = (tempBuffer[rowCurr + x + 1] >> 16) & 0xFF, gW = (tempBuffer[rowCurr + x - 1] >> 16) & 0xFF;

                uint32_t bN = (tempBuffer[rowPrev + x] >> 8) & 0xFF,  bS = (tempBuffer[rowNext + x] >> 8) & 0xFF;
                uint32_t bE = (tempBuffer[rowCurr + x + 1] >> 8) & 0xFF,  bW = (tempBuffer[rowCurr + x - 1] >> 8) & 0xFF;

                int rAvg = (rN + rS + rE + rW) >> 2;
                int gAvg = (gN + gS + gE + gW) >> 2;
                int bAvg = (bN + bS + bE + bW) >> 2;

                int rNew = std::clamp((static_cast<int>(rM) * 9 - rAvg * 5) >> 2, 0, 255);
                int gNew = std::clamp((static_cast<int>(gM) * 9 - gAvg * 5) >> 2, 0, 255);
                int bNew = std::clamp((static_cast<int>(bM) * 9 - bAvg * 5) >> 2, 0, 255);

                buffer[rowCurr + x] = (rNew << 24) | (gNew << 16) | (bNew << 8) | 0xFF;
            }
        }
    }
}

void SceneRaytracingGPU::RenderHUD_GX2() {
    auto& r2d = Render2DGX2::Instance();

#if defined(__WIIU__) || defined(__wiiu__)
    if (m_nativeContextState && m_pColorBuffer) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    }
#endif

    r2d.Begin();

    // 1. Botón Volver con fondo sólido de alto contraste (Fila 1)
    r2d.DrawButton("< VOLVER", static_cast<float>(m_backBtnRect.x), static_cast<float>(m_backBtnRect.y),
                   static_cast<float>(m_backBtnRect.w), static_cast<float>(m_backBtnRect.h), m_backHover,
                   ColorRGBA{12, 18, 32, 255}, ColorRGBA{200, 40, 40, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(),
                   ColorRGBA::White(), 2.0f);

    // 2. Botón Calidad / Resolución Buffer Interno (Fila 1)
    const char* qualStr = "BUFFER: 854X480 (480P)";
    if (m_quality == RayQualityGPU::GAMEPAD_480P_854x480) qualStr = "BUFFER: 854X480 (480P)";
    if (m_quality == RayQualityGPU::QHD_540P_960x540) qualStr = "BUFFER: 960X540 (540P)";
    if (m_quality == RayQualityGPU::HD_576P_1024x576) qualStr = "BUFFER: 1024X576 (576P)";
    if (m_quality == RayQualityGPU::HD_648P_1152x648) qualStr = "BUFFER: 1152X648 (648P)";
    if (m_quality == RayQualityGPU::FULL_720P_1280x720) qualStr = "BUFFER: 1152X648 (720P ESC)";
    if (m_quality == RayQualityGPU::FULL_1080P_1920x1080) qualStr = "BUFFER: 1152X648 (1080P ESC)";

    r2d.DrawButton(qualStr, static_cast<float>(m_qualBtnRect.x), static_cast<float>(m_qualBtnRect.y),
                   static_cast<float>(m_qualBtnRect.w), static_cast<float>(m_qualBtnRect.h), m_qualHover,
                   ColorRGBA{15, 30, 50, 220}, ColorRGBA{0, 180, 240, 230},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(),
                   ColorRGBA::White(), 2.0f);

    // 3. Botón Target HD de Salida (Fila 1)
    const char* targetStr = (m_outputTarget == OutputTargetHD::TARGET_720P_HD) ? "SALIDA: 720P HD" : "SALIDA: 1080P FULL HD";
    r2d.DrawButton(targetStr, static_cast<float>(m_targetBtnRect.x), static_cast<float>(m_targetBtnRect.y),
                   static_cast<float>(m_targetBtnRect.w), static_cast<float>(m_targetBtnRect.h), m_targetHover,
                   ColorRGBA{15, 45, 35, 220}, ColorRGBA{0, 200, 120, 230},
                   ColorRGBA::Green(), ColorRGBA::Yellow(),
                   ColorRGBA::White(), 2.0f);

    // 4. Botón Alternar Aceleración GPU (Fila 2)
    const char* gpuStr = m_gpuAccel ? "GPU ACCEL: ON" : "GPU ACCEL: OFF";
    r2d.DrawButton(gpuStr, static_cast<float>(m_gpuBtnRect.x), static_cast<float>(m_gpuBtnRect.y),
                   static_cast<float>(m_gpuBtnRect.w), static_cast<float>(m_gpuBtnRect.h), m_gpuHover,
                   ColorRGBA{60, 30, 10, 220}, ColorRGBA{240, 140, 0, 230},
                   ColorRGBA{255, 160, 0, 255}, ColorRGBA::Yellow(),
                   ColorRGBA::White(), 2.0f);

    // 5. Botón Alternar Multihilo CPU (Fila 2)
    const char* cpuStr = m_multithreading ? "CPU: 2 CORES" : "CPU: 1 CORE";
    r2d.DrawButton(cpuStr, static_cast<float>(m_cpuBtnRect.x), static_cast<float>(m_cpuBtnRect.y),
                   static_cast<float>(m_cpuBtnRect.w), static_cast<float>(m_cpuBtnRect.h), m_cpuHover,
                   ColorRGBA{40, 10, 50, 220}, ColorRGBA{180, 0, 220, 230},
                   ColorRGBA{220, 50, 255, 255}, ColorRGBA::Yellow(),
                   ColorRGBA::White(), 2.0f);

    // 6. Botón Cambiar Modo de Suavizado (Fila 2)
    const char* smoothStr = "SUAVIZADO: FXAA FULL";
    if (m_smoothingMode == SmoothingMode::OFF_NEAREST) smoothStr = "SUAVIZADO: DESACTIVADO";
    if (m_smoothingMode == SmoothingMode::BILINEAR_GX2) smoothStr = "SUAVIZADO: BILINEAL HW";
    if (m_smoothingMode == SmoothingMode::SUBPIXEL_AA) smoothStr = "SUAVIZADO: SUBPIXEL AA";
    if (m_smoothingMode == SmoothingMode::FXAA_FULL) smoothStr = "SUAVIZADO: FXAA FULL";
    if (m_smoothingMode == SmoothingMode::BICUBIC_SHARPEN) smoothStr = "SUAVIZADO: BICUBICO";

    r2d.DrawButton(smoothStr, static_cast<float>(m_smoothBtnRect.x), static_cast<float>(m_smoothBtnRect.y),
                   static_cast<float>(m_smoothBtnRect.w), static_cast<float>(m_smoothBtnRect.h), m_smoothHover,
                   ColorRGBA{45, 30, 15, 220}, ColorRGBA{220, 120, 0, 230},
                   ColorRGBA{255, 165, 0, 255}, ColorRGBA::Yellow(),
                   ColorRGBA::White(), 2.0f);

    // 7. Panel Inferior de Telemetría Estandarizada (UIToolkit - DrawHudFooter)
    bool fpsLimited = SceneManager::Instance().IsFPSLimitEnabled();
    StackString<256> line1;
    line1.Format("DEMO 6: RAY TRACING GPU GX2 (LATTE GSH) | FPS: %.1f%s",
                 m_smoothedFps,
                 fpsLimited ? " (LIMITADO 60 FPS)" : " (UNCAPPED)");

    StackString<256> line3;
    line3.Format("AUDIO: BGM OK (48KHZ) | CLICK: %s | UI: 100%% GX2 NATIVO",
                 AudioManager::Instance().GetStatusString().c_str());

    UI::UIToolkit::DrawHudFooter({
        { "CAMARA ORBITAL 3D: JOYSTICK DERECHO / FLECHAS | [B]: VOLVER", ColorRGBA::White() },
        { line1.string_view(), ColorRGBA::Yellow() },
        { line3.string_view(), ColorRGBA{0, 240, 255, 255} }
    });

    // 8. Efecto Táctil
    if (m_clickEffect.active && m_clickEffect.alpha > 0.0f) {
        float r = m_clickEffect.radius;
        Uint8 a = static_cast<Uint8>(std::clamp(m_clickEffect.alpha * 255.0f, 0.0f, 255.0f));
        r2d.DrawBox(m_clickEffect.x - r, m_clickEffect.y - r, r * 2.0f, r * 2.0f,
                    ColorRGBA{0, 0, 0, 0}, ColorRGBA{0, 240, 255, a}, 2.0f);
        float r2 = r * 0.65f;
        r2d.DrawBox(m_clickEffect.x - r2, m_clickEffect.y - r2, r2 * 2.0f, r2 * 2.0f,
                    ColorRGBA{0, 0, 0, 0}, ColorRGBA{255, 230, 0, a}, 2.0f);
    }

    r2d.End();
}

void SceneRaytracingGPU::Render(SDL_Renderer* renderer) {
    Uint32 nowTicks = SDL_GetTicks();
    if (m_lastFrameTicks > 0) {
        Uint32 deltaTicks = nowTicks - m_lastFrameTicks;
        if (deltaTicks > 0) {
            float instFps = 1000.0f / static_cast<float>(deltaTicks);
            m_smoothedFps = m_smoothedFps * 0.88f + instFps * 0.12f;
        }
    }
    m_lastFrameTicks = nowTicks;

#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_gfdShaderLoaded || !m_quadBufferCreated) {
        InitGX2Pipeline();
    }
    if (!m_colorBufferBound && renderer) {
        BindColorBuffer(renderer);
    }

    if (m_pColorBuffer && m_nativeContextState) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
        GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
        GX2SetScissor(0, 0, 1280, 720);
        GX2ClearColor(m_pColorBuffer, 0.03f, 0.06f, 0.15f, 1.0f);
    }

    RenderGX2();
    RenderHUD_GX2();
#else
    // Fallback Desktop PC (Software Rendering + SDL)
    if (!m_gpuTexture && renderer) {
        m_gpuTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, 
                                         SDL_TEXTUREACCESS_STREAMING, m_gpuWidth, m_gpuHeight);
    }
    if (m_gpuTexture) {
        SDL_ScaleMode scaleMode = (m_smoothingMode == SmoothingMode::OFF_NEAREST) ? SDL_ScaleModeNearest : SDL_ScaleModeLinear;
        SDL_SetTextureScaleMode(m_gpuTexture, scaleMode);

        void* lockedPixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(m_gpuTexture, nullptr, &lockedPixels, &pitch) == 0 && lockedPixels != nullptr) {
            uint32_t* dstBuffer = static_cast<uint32_t*>(lockedPixels);
            int pitchPixels = pitch / static_cast<int>(sizeof(uint32_t));
            TraceSceneGPU(dstBuffer, m_gpuWidth, m_gpuHeight, pitchPixels);
            ApplyBufferSmoothingGPU(dstBuffer, m_gpuWidth, m_gpuHeight, pitchPixels, m_smoothingMode);
            SDL_UnlockTexture(m_gpuTexture);
        }
        SDL_RenderCopy(renderer, m_gpuTexture, nullptr, nullptr);
    }
#endif
}

} // namespace WiiU
