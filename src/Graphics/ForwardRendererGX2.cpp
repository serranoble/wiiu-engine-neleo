#include "Graphics/ForwardRendererGX2.hpp"
#include "Core/Diagnostics/EngineProfiler.hpp"
#include "Graphics/Mesh/MeshBinaryFormat.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/AssetManager.hpp"
#if defined(__WIIU__) || defined(__wiiu__)
#include "Core/Window.hpp"
#endif
#include "Graphics/SkyboxShaderBinary.hpp"
#include "Graphics/AdvancedMeshShaderBinary.hpp"
#include "Graphics/AdvancedSkelShaderBinary.hpp"
#include "Graphics/InstancedMeshShaderBinary.hpp"
#include "Graphics/ShadowDepthShaderBinary.hpp"
#include "Graphics/PBRMeshShaderBinary.hpp"
#include "Graphics/PBRVariantsShaderBinary.hpp"
#include "Graphics/WaterGlassShaderBinary.hpp"
#include "Graphics/HDRFogShaderBinary.hpp"
#include "Graphics/ShadowMapRendererGX2.hpp"
#include <algorithm>
#include <cstring>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/registers.h>
#include <gx2/mem.h>
#include <gx2/state.h>
#include <gx2/clear.h>
#include <gx2/enum.h>
#include <gx2/utils.h>
#include <gfd.h>
#include <proc_ui/procui.h>
#endif

namespace Neleo {

#if defined(__WIIU__) || defined(__wiiu__)
namespace {
void UploadSkinnedVertexUniforms(uint32_t mvpOffset,
                                 uint32_t modelOffset,
                                 uint32_t bonesOffset,
                                 const Matrix4x4& vpMatrix,
                                 const Matrix4x4& modelMatrix,
                                 const Matrix4x4* boneMatrices,
                                 uint32_t boneCount) {
    alignas(256) float mvpBuf[16];
    alignas(256) float modelBuf[16];
    alignas(256) float bonePaletteBuf[24 * 16];

    const Matrix4x4 mvp = vpMatrix * modelMatrix;
    std::memcpy(mvpBuf, mvp.m, sizeof(mvpBuf));
    std::memcpy(modelBuf, modelMatrix.m, sizeof(modelBuf));

    const uint32_t clampedBones = (boneCount > 24u) ? 24u : boneCount;
    const Matrix4x4 ident = Matrix4x4::Identity();
    for (uint32_t b = 0u; b < clampedBones; ++b) {
        std::memcpy(&bonePaletteBuf[b * 16u], boneMatrices[b].m, 16u * sizeof(float));
    }
    for (uint32_t b = clampedBones; b < 24u; ++b) {
        std::memcpy(&bonePaletteBuf[b * 16u], ident.m, 16u * sizeof(float));
    }

    GX2SetVertexUniformReg(mvpOffset, 16u, mvpBuf);
    GX2SetVertexUniformReg(modelOffset, 16u, modelBuf);
    GX2SetVertexUniformReg(bonesOffset, 24u * 16u, bonePaletteBuf);
}
} // namespace
#endif

ForwardRendererGX2& ForwardRendererGX2::Instance() noexcept {
    static ForwardRendererGX2 s_instance;
    return s_instance;
}

bool ForwardRendererGX2::Initialize() {
    if (m_initialized) return true;

#if defined(__WIIU__) || defined(__wiiu__)
    if (!InitSkyShader())      { Shutdown(); return false; }
    if (!InitMeshShader())     { Shutdown(); return false; }
    if (!InitSkinningShader()) { Shutdown(); return false; }
    (void)InitInstancedShader();
    (void)InitDepthPrePassShader();
    (void)InitPBRShader();
    (void)InitPBRVariants();
    (void)InitWaterGlassShader();
    (void)InitHDRFogShader();
    (void)InitPBRInstancedShader();

    // Inicializar Samplers GX2 (todos los samplers single-mip fijan MIP_FILTER_NONE + MIP_PERF_DISABLE + LOD 0..0
    // para evitar bloqueos TA/TD en AMD Latte R700 cuando MIP_ADDRESS == NULL)
    GX2InitSampler(&m_skySampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerXYFilter(&m_skySampler, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_ANISO_RATIO_NONE);
    GX2InitSamplerZMFilter(&m_skySampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_NONE);
    GX2InitSamplerFilterAdjust(&m_skySampler, FALSE, GX2_TEX_MIP_PERF_MODE_DISABLE, GX2_TEX_Z_PERF_MODE_DISABLED);
    GX2InitSamplerLOD(&m_skySampler, 0.0f, 0.0f, 0.0f);

    GX2InitSampler(&m_meshSampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerLOD(&m_meshSampler, 0.0f, 10.0f, 0.0f);
    GX2InitSampler(&m_meshNoMipSampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerZMFilter(&m_meshNoMipSampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_NONE);
    GX2InitSamplerFilterAdjust(&m_meshNoMipSampler, FALSE, GX2_TEX_MIP_PERF_MODE_DISABLE, GX2_TEX_Z_PERF_MODE_DISABLED);
    GX2InitSamplerLOD(&m_meshNoMipSampler, 0.0f, 0.0f, 0.0f);
    ApplyTextureFilterMode(EngineConfig::Instance().graphics.textureFilter);

    GX2InitSampler(&m_cubeSampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerXYFilter(&m_cubeSampler, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_ANISO_RATIO_NONE);
    GX2InitSamplerZMFilter(&m_cubeSampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_NONE);
    GX2InitSamplerFilterAdjust(&m_cubeSampler, FALSE, GX2_TEX_MIP_PERF_MODE_DISABLE, GX2_TEX_Z_PERF_MODE_DISABLED);
    GX2InitSamplerLOD(&m_cubeSampler, 0.0f, 0.0f, 0.0f);
    m_cubeSamplerCreated = true;

    // Textura neutra 4x4 (0,0,0,0) en búfer estático alineado a 256B (0 leaks en MemoryManager).
    // Activa la ruta vColor.rgb en advanced_mesh_gx2.frag cuando diffuseTexture == nullptr.
    alignas(256) static uint8_t s_defaultNeutralTexData[4096]{};
    std::memset(&m_defaultNeutralTex, 0, sizeof(m_defaultNeutralTex));
    m_defaultNeutralTex.surface.dim       = GX2_SURFACE_DIM_TEXTURE_2D;
    m_defaultNeutralTex.surface.width     = 4;
    m_defaultNeutralTex.surface.height    = 4;
    m_defaultNeutralTex.surface.depth     = 1;
    m_defaultNeutralTex.surface.mipLevels = 1;
    m_defaultNeutralTex.surface.format    = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    m_defaultNeutralTex.surface.aa        = GX2_AA_MODE1X;
    m_defaultNeutralTex.surface.use       = GX2_SURFACE_USE_TEXTURE;
    m_defaultNeutralTex.surface.tileMode  = GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(&m_defaultNeutralTex.surface);
    if (m_defaultNeutralTex.surface.imageSize <= sizeof(s_defaultNeutralTexData)) {
        std::memset(s_defaultNeutralTexData, 0, m_defaultNeutralTex.surface.imageSize);
        m_defaultNeutralTex.surface.image    = s_defaultNeutralTexData;
        m_defaultNeutralTex.viewFirstMip     = 0;
        m_defaultNeutralTex.viewNumMips      = 1;
        m_defaultNeutralTex.viewFirstSlice   = 0;
        m_defaultNeutralTex.viewNumSlices    = 1;
        m_defaultNeutralTex.compMap          = 0x00010203;
        GX2InitTextureRegs(&m_defaultNeutralTex);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, s_defaultNeutralTexData, m_defaultNeutralTex.surface.imageSize);
        m_defaultTexCreated = true;
    }

    // CubeMap neutro fallback 1x1 (6 caras) en bufer estatico alineado a 256B (0 leaks)
    alignas(256) static uint8_t s_defaultNeutralCubeData[16384]{};
    std::memset(&m_defaultNeutralCubeMap, 0, sizeof(m_defaultNeutralCubeMap));
    m_defaultNeutralCubeMap.surface.dim       = GX2_SURFACE_DIM_TEXTURE_CUBE;
    m_defaultNeutralCubeMap.surface.width     = 4;
    m_defaultNeutralCubeMap.surface.height    = 4;
    m_defaultNeutralCubeMap.surface.depth     = 6;
    m_defaultNeutralCubeMap.surface.mipLevels = 1;
    m_defaultNeutralCubeMap.surface.format    = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    m_defaultNeutralCubeMap.surface.aa        = GX2_AA_MODE1X;
    m_defaultNeutralCubeMap.surface.use       = GX2_SURFACE_USE_TEXTURE;
    m_defaultNeutralCubeMap.surface.tileMode  = GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(&m_defaultNeutralCubeMap.surface);
    if (m_defaultNeutralCubeMap.surface.imageSize <= sizeof(s_defaultNeutralCubeData)) {
        std::memset(s_defaultNeutralCubeData, 0, m_defaultNeutralCubeMap.surface.imageSize);
        m_defaultNeutralCubeMap.surface.image  = s_defaultNeutralCubeData;
        m_defaultNeutralCubeMap.viewFirstMip   = 0;
        m_defaultNeutralCubeMap.viewNumMips    = 1;
        m_defaultNeutralCubeMap.viewFirstSlice = 0;
        m_defaultNeutralCubeMap.viewNumSlices  = 6;
        m_defaultNeutralCubeMap.compMap        = 0x00010203;
        GX2InitTextureRegs(&m_defaultNeutralCubeMap);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, s_defaultNeutralCubeData, m_defaultNeutralCubeMap.surface.imageSize);
        m_defaultCubeCreated = true;
    }
#endif

    m_initialized = true;
    return true;
}

void ForwardRendererGX2::Shutdown() {
#if defined(__WIIU__) || defined(__wiiu__)
    bool anyPbrVariant = false;
    for (const auto& v : m_pbrVariants) {
        if (v.loaded || v.vs != nullptr) {
            anyPbrVariant = true;
            break;
        }
    }
    const bool hasGpuResources =
        m_skyShaderLoaded || m_meshShaderLoaded || m_skelShaderLoaded ||
        m_instShaderLoaded || m_depthPreShaderLoaded || m_pbrShaderLoaded || anyPbrVariant ||
        m_waterGlassShaderLoaded || m_hdrFogPipe.loaded || m_pbrInstShaderLoaded || m_instanceBufferCreated ||
        m_skyVs || m_meshVs || m_skelVs || m_instVs || m_depthPreVs || m_pbrVs ||
        m_waterGlassVs || m_hdrFogPipe.vs || m_pbrInstVs;
    if (hasGpuResources && ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown() &&
        Window::HasForeground() && !Window::IsExitingProcUI()) {
        GX2DrawDone();
    }

    // Liberacion incondicional de cualquier puntero no nulo (0 leaks ante fallos parciales de Init*)
    auto freeShaderPtrs = [](GX2VertexShader*& vs, void*& vsPgm, GX2PixelShader*& ps, void*& psPgm, void*& fsPgm) {
        if (vs)    { void* raw = static_cast<void*>(vs); MemoryManager::Instance().Free(raw); vs = nullptr; }
        if (vsPgm) { MemoryManager::Instance().FreeAligned(vsPgm); vsPgm = nullptr; }
        if (ps)    { void* raw = static_cast<void*>(ps); MemoryManager::Instance().Free(raw); ps = nullptr; }
        if (psPgm) { MemoryManager::Instance().FreeAligned(psPgm); psPgm = nullptr; }
        if (fsPgm) { MemoryManager::Instance().FreeAligned(fsPgm); fsPgm = nullptr; }
    };

    freeShaderPtrs(m_skyVs, m_skyVsProgram, m_skyPs, m_skyPsProgram, m_skyFsProgram);
    m_skyShaderLoaded = false;

    freeShaderPtrs(m_meshVs, m_meshVsProgram, m_meshPs, m_meshPsProgram, m_meshFsProgram);
    m_meshShaderLoaded = false;

    freeShaderPtrs(m_skelVs, m_skelVsProgram, m_skelPs, m_skelPsProgram, m_skelFsProgram);
    m_skelMvpRegOffset = 0u;
    m_skelModelRegOffset = 16u;
    m_skelBonesRegOffset = 32u;
    m_skelDiffuseSlot = 0u;
    m_skelShaderLoaded = false;

    if (m_instanceBufferCreated) {
        GX2RDestroyBufferEx(&m_instanceMatrixBuffer, GX2R_RESOURCE_BIND_NONE);
        std::memset(&m_instanceMatrixBuffer, 0, sizeof(m_instanceMatrixBuffer));
        m_instanceBufferCreated = false;
    }
    freeShaderPtrs(m_instVs, m_instVsProgram, m_instPs, m_instPsProgram, m_instFsProgram);
    m_instShaderLoaded = false;

    if (m_depthPreVs)        { void* raw = static_cast<void*>(m_depthPreVs); MemoryManager::Instance().Free(raw); m_depthPreVs = nullptr; }
    if (m_depthPreVsProgram) { MemoryManager::Instance().FreeAligned(m_depthPreVsProgram); m_depthPreVsProgram = nullptr; }
    if (m_depthPrePs)        { void* raw = static_cast<void*>(m_depthPrePs); MemoryManager::Instance().Free(raw); m_depthPrePs = nullptr; }
    if (m_depthPrePsProgram) { MemoryManager::Instance().FreeAligned(m_depthPrePsProgram); m_depthPrePsProgram = nullptr; }
    if (m_depthPreFsProgram) { MemoryManager::Instance().FreeAligned(m_depthPreFsProgram); m_depthPreFsProgram = nullptr; }
    m_depthPreShaderLoaded = false;
    m_inDepthPrePass = false;
    m_depthPrePassEnabled = false;

    freeShaderPtrs(m_pbrVs, m_pbrVsProgram, m_pbrPs, m_pbrPsProgram, m_pbrFsProgram);
    m_pbrDiffuseSlot = 0;
    m_pbrSkyboxSlot = -1;
    m_pbrShadowSlot = 1;
    m_pbrShaderLoaded = false;

    for (auto& v : m_pbrVariants) {
        freeShaderPtrs(v.vs, v.vsProgram, v.ps, v.psProgram, v.fsProgram);
        v.diffuseSlot = 0;
        v.skyboxSlot = -1;
        v.shadowSlot = -1;
        v.loaded = false;
    }

    freeShaderPtrs(m_waterGlassVs, m_waterGlassVsProgram, m_waterGlassPs, m_waterGlassPsProgram, m_waterGlassFsProgram);
    m_waterGlassDiffuseSlot = 0;
    m_waterGlassSkyboxSlot = -1;
    m_waterGlassShaderLoaded = false;

    freeShaderPtrs(m_hdrFogPipe.vs, m_hdrFogPipe.vsProgram, m_hdrFogPipe.ps, m_hdrFogPipe.psProgram, m_hdrFogPipe.fsProgram);
    m_hdrFogPipe.diffuseSlot = 0;
    m_hdrFogPipe.skyboxSlot = -1;
    m_hdrFogPipe.shadowSlot = -1;
    m_hdrFogPipe.loaded = false;

    void* dummyFs = nullptr;
    freeShaderPtrs(m_pbrInstVs, m_pbrInstVsProgram, m_pbrInstPs, m_pbrInstPsProgram, dummyFs);
    m_pbrInstDiffuseSlot = 0;
    m_pbrInstSkyboxSlot = -1;
    m_pbrInstShaderLoaded = false;

    m_boundShaderKind = BoundShaderKind::None;
    m_boundPbrVariantIndex = 0xFFu;
#endif
    m_renderQueue.Clear();
    m_initialized = false;
}

void ForwardRendererGX2::BeginFrame(const Point3D& camPos, const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) {
#if defined(__WIIU__) || defined(__wiiu__)
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    // Sincronizar caché RHI tras Window::Clear() o pases 2D externos que mutan estado GX2
    RHIPipelineCache::Instance().Invalidate();
    m_boundShaderKind = BoundShaderKind::None;
    m_boundPbrVariantIndex = 0xFFu;
    // Rotar slice del Multi-Frame Ring Buffer de instancing (0 carreras CPU/GPU entre frames en vuelo)
    m_instanceFrameSlice = (m_instanceFrameSlice + 1u) % MAX_FRAMES_IN_FLIGHT;
    m_instanceWriteCursor = 0;
#endif
    m_camPos = camPos;
    m_viewMatrix = viewMatrix;
    m_renderQueue.Clear();
    m_projMatrix = projMatrix;
    m_vpMatrix = projMatrix * viewMatrix;
    m_activeFrustum.ExtractFromViewProj(m_vpMatrix);
    m_frustumCulledCount = 0u;
    m_autoBatchedDraws = 0u;
    m_depthPrePassEnabled = false;
    m_drawCallsCount = 0;
    m_trianglesCount = 0;
    m_instancedDrawCalls = 0;
    m_instancedCount = 0;
    m_shadowMode = 0u;
}

void ForwardRendererGX2::RenderSkydome(StaticMesh& skydomeMesh, GX2Texture* skyTexture) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!skydomeMesh.IsLoaded() || !skyTexture || !m_skyShaderLoaded) return;

    if (m_boundShaderKind != BoundShaderKind::Sky) {
        GX2SetFetchShader(&m_skyFs);
        GX2SetVertexShader(m_skyVs);
        GX2SetPixelShader(m_skyPs);
        m_boundShaderKind = BoundShaderKind::Sky;
    }

    // Cancelar traslación de la cámara para que el domo permanezca en el infinito óptico
    Matrix4x4 skyView = m_viewMatrix;
    skyView.m[12] = 0.0f;
    skyView.m[13] = 0.0f;
    skyView.m[14] = 0.0f;
    Matrix4x4 skyVP = m_projMatrix * skyView;

    alignas(256) float skyUniforms[16];
    std::memcpy(skyUniforms, skyVP.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 16, skyUniforms);

    const bool skyHasMips = (skyTexture->surface.mipLevels > 1u) && (skyTexture->surface.mipmaps != nullptr);
    GX2SetPixelTexture(skyTexture, 0);
    GX2SetPixelSampler(skyHasMips ? &m_meshSampler : &m_skySampler, 0);

    // Depth Test ON, Depth Write OFF (Z=W=1.0)
    RHIPipelineCache::Instance().BindState(RHIPipelineState(true, false, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false));

    if (skydomeMesh.GetVBO() && skydomeMesh.GetIBO() && skydomeMesh.GetIndexCount() > 0) {
        const GX2IndexType idxType = (skydomeMesh.GetHeader().indexFormat == 1) ? GX2_INDEX_TYPE_U32 : GX2_INDEX_TYPE_U16;
        GX2RSetAttributeBuffer(const_cast<GX2RBuffer*>(skydomeMesh.GetVBO()), 0, skydomeMesh.GetHeader().vertexStride, 0);
        GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, const_cast<GX2RBuffer*>(skydomeMesh.GetIBO()), 
                        idxType, skydomeMesh.GetIndexCount(), 0, 0, 1);
        m_drawCallsCount += 1;
        m_trianglesCount += skydomeMesh.GetIndexCount() / 3;
    }
#else
    (void)skydomeMesh; (void)skyTexture;
#endif
}

void ForwardRendererGX2::RenderStaticMesh(const StaticMesh& mesh, 
                                         const Matrix4x4& modelMatrix, 
                                         GX2Texture* diffuseTexture,
                                         GX2Texture* envCubeMap,
                                         float reflectionIntensity,
                                         bool normalMapEnabled,
                                         float bumpScale,
                                         float ambientIntensity) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!mesh.IsLoaded() || !m_meshShaderLoaded) return;

    if (m_boundShaderKind != BoundShaderKind::Mesh) {
        GX2SetFetchShader(&m_meshFs);
        GX2SetVertexShader(m_meshVs);
        GX2SetPixelShader(m_meshPs);
        m_boundShaderKind = BoundShaderKind::Mesh;
    }

    Matrix4x4 mvp = m_vpMatrix * modelMatrix;
    alignas(256) float vsUniforms[32];
    std::memcpy(&vsUniforms[0], mvp.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], modelMatrix.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 32, vsUniforms);

    const LightBlockUBO& lights = MaterialRegistry::Instance().GetActiveLightBlock();
    alignas(256) float psParams[16];
    psParams[0] = m_camPos.x; psParams[1] = m_camPos.y; psParams[2] = m_camPos.z; psParams[3] = 0.0f; // uCamPosTime
    psParams[4] = lights.dirLightDir[0]; psParams[5] = lights.dirLightDir[1]; psParams[6] = lights.dirLightDir[2]; psParams[7] = lights.dirLightDir[3];
    psParams[8] = lights.dirLightColor[0]; psParams[9] = lights.dirLightColor[1]; psParams[10] = lights.dirLightColor[2]; psParams[11] = lights.dirLightColor[3];
    psParams[12] = (envCubeMap && m_cubeSamplerCreated) ? reflectionIntensity : 0.0f;                                                              // uMaterialParams.x
    psParams[13] = normalMapEnabled ? 1.0f : 0.0f;                                                   // uMaterialParams.y
    psParams[14] = bumpScale;                                                                        // uMaterialParams.z
    psParams[15] = ambientIntensity;                                                                 // uMaterialParams.w
    GX2SetPixelUniformReg(0, 16, psParams);

    const GX2Texture* activeDiffuseTex = diffuseTexture ? diffuseTexture : (m_defaultTexCreated ? &m_defaultNeutralTex : nullptr);
    BindMeshTextureAndSampler(activeDiffuseTex, 0u);
    if (m_cubeSamplerCreated) {
        GX2Texture* activeCube = envCubeMap ? envCubeMap : (m_defaultCubeCreated ? &m_defaultNeutralCubeMap : nullptr);
        if (activeCube) {
            GX2SetPixelTexture(activeCube, 1);
            GX2SetPixelSampler(&m_cubeSampler, 1);
        }
    }

    RHIPipelineCache::Instance().BindState(RHIPipelineState(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false));

    if (mesh.GetVBO() && mesh.GetIBO()) {
        const GX2IndexType idxType = (mesh.GetHeader().indexFormat == 1) ? GX2_INDEX_TYPE_U32 : GX2_INDEX_TYPE_U16;
        GX2RSetAttributeBuffer(const_cast<GX2RBuffer*>(mesh.GetVBO()), 0, mesh.GetHeader().vertexStride, 0);
        GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, const_cast<GX2RBuffer*>(mesh.GetIBO()), 
                        idxType, mesh.GetIndexCount(), 0, 0, 1);
        m_drawCallsCount += 1;
        m_trianglesCount += mesh.GetIndexCount() / 3;
    }
#else
    (void)mesh; (void)modelMatrix; (void)diffuseTexture; (void)envCubeMap;
    (void)reflectionIntensity; (void)normalMapEnabled; (void)bumpScale; (void)ambientIntensity;
#endif
}

void ForwardRendererGX2::RenderMeshBuffers(GX2RBuffer& vbo,
                                          GX2RBuffer& ibo,
                                          uint32_t indexCount,
                                          uint32_t vertexStride,
                                          const Matrix4x4& modelMatrix,
                                          GX2Texture* diffuseTexture,
                                          GX2Texture* envCubeMap,
                                          float reflectionIntensity,
                                          bool normalMapEnabled,
                                          float bumpScale,
                                          float ambientIntensity,
                                          bool use32BitIndices,
                                          BlendMode blendMode,
                                          bool depthTest,
                                          bool depthWrite,
                                          uint8_t depthCompare) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_meshShaderLoaded || indexCount == 0) return;

    if (m_boundShaderKind != BoundShaderKind::Mesh) {
        GX2SetFetchShader(&m_meshFs);
        GX2SetVertexShader(m_meshVs);
        GX2SetPixelShader(m_meshPs);
        m_boundShaderKind = BoundShaderKind::Mesh;
    }

    Matrix4x4 mvp = m_vpMatrix * modelMatrix;
    alignas(256) float vsUniforms[32];
    std::memcpy(&vsUniforms[0], mvp.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], modelMatrix.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 32, vsUniforms);

    const LightBlockUBO& lights = MaterialRegistry::Instance().GetActiveLightBlock();
    alignas(256) float psParams[16];
    psParams[0] = m_camPos.x; psParams[1] = m_camPos.y; psParams[2] = m_camPos.z; psParams[3] = 0.0f;
    psParams[4] = lights.dirLightDir[0]; psParams[5] = lights.dirLightDir[1]; psParams[6] = lights.dirLightDir[2]; psParams[7] = lights.dirLightDir[3];
    psParams[8] = lights.dirLightColor[0]; psParams[9] = lights.dirLightColor[1]; psParams[10] = lights.dirLightColor[2]; psParams[11] = lights.dirLightColor[3];
    psParams[12] = (envCubeMap && m_cubeSamplerCreated) ? reflectionIntensity : 0.0f;
    psParams[13] = normalMapEnabled ? 1.0f : 0.0f;
    psParams[14] = bumpScale;
    psParams[15] = ambientIntensity;
    GX2SetPixelUniformReg(0, 16, psParams);

    const GX2Texture* activeDiffuseTex = diffuseTexture ? diffuseTexture
                                      : (m_defaultTexCreated ? &m_defaultNeutralTex : nullptr);
    BindMeshTextureAndSampler(activeDiffuseTex, 0u);
    if (m_cubeSamplerCreated) {
        GX2Texture* activeCube = envCubeMap ? envCubeMap : (m_defaultCubeCreated ? &m_defaultNeutralCubeMap : nullptr);
        if (activeCube) {
            GX2SetPixelTexture(activeCube, 1);
            GX2SetPixelSampler(&m_cubeSampler, 1);
        }
    }

    RHIPipelineCache::Instance().BindState(RHIPipelineState(depthTest, depthWrite, depthCompare, blendMode, false));

    GX2RSetAttributeBuffer(&vbo, 0, vertexStride, 0);
    const GX2IndexType idxType = use32BitIndices ? GX2_INDEX_TYPE_U32 : GX2_INDEX_TYPE_U16;
    GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &ibo, idxType, indexCount, 0, 0, 1);
    m_drawCallsCount += 1;
    m_trianglesCount += indexCount / 3;
#else
    (void)vbo; (void)ibo; (void)indexCount; (void)vertexStride; (void)modelMatrix;
    (void)diffuseTexture; (void)envCubeMap; (void)reflectionIntensity;
    (void)normalMapEnabled; (void)bumpScale; (void)ambientIntensity;
    (void)use32BitIndices; (void)blendMode; (void)depthTest; (void)depthWrite; (void)depthCompare;
#endif
}

void ForwardRendererGX2::RenderSkinnedMesh(const StaticMesh& skinnedMesh,
                                           const Matrix4x4& modelMatrix,
                                           const Matrix4x4* boneMatrices,
                                           uint32_t boneCount,
                                           GX2Texture* diffuseTexture,
                                           GX2Texture* envCubeMap,
                                           float reflectionIntensity,
                                           bool clayMode) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!skinnedMesh.IsLoaded() || !skinnedMesh.GetVBO() || !skinnedMesh.GetIBO()) {
        return;
    }
    const bool isU32 = (skinnedMesh.GetHeader().indexFormat == 1);
    RenderSkinnedMesh(
        *const_cast<GX2RBuffer*>(skinnedMesh.GetVBO()),
        *const_cast<GX2RBuffer*>(skinnedMesh.GetIBO()),
        skinnedMesh.GetIndexCount(),
        skinnedMesh.GetHeader().vertexStride,
        modelMatrix,
        boneMatrices,
        boneCount,
        diffuseTexture,
        envCubeMap,
        reflectionIntensity,
        clayMode,
        isU32
    );
#else
    (void)skinnedMesh; (void)modelMatrix; (void)boneMatrices; (void)boneCount;
    (void)diffuseTexture; (void)envCubeMap; (void)reflectionIntensity; (void)clayMode;
#endif
}

#if defined(__WIIU__) || defined(__wiiu__)
void ForwardRendererGX2::BindMeshTextureAndSampler(const GX2Texture* tex, uint32_t slot) noexcept {
    if (!tex) return;
    GX2SetPixelTexture(const_cast<GX2Texture*>(tex), slot);
    const bool hasValidMipChain = (tex->surface.mipLevels > 1u) && (tex->surface.mipmaps != nullptr);
    GX2SetPixelSampler(hasValidMipChain ? &m_meshSampler : &m_meshNoMipSampler, slot);
}

void ForwardRendererGX2::BindSkinnedPipeline(GX2Texture* diffuseTexture,
                                               float reflectionIntensity,
                                               bool clayMode) {
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    m_boundShaderKind = BoundShaderKind::None;
    GX2SetFetchShader(&m_skelFs);
    GX2SetVertexShader(m_skelVs);
    GX2SetPixelShader(m_skelPs);

    const LightBlockUBO& lights = MaterialRegistry::Instance().GetActiveLightBlock();
    alignas(256) float psParams[16];
    psParams[0] = m_camPos.x; psParams[1] = m_camPos.y; psParams[2] = m_camPos.z; psParams[3] = 0.0f;
    psParams[4] = lights.dirLightDir[0]; psParams[5] = lights.dirLightDir[1]; psParams[6] = lights.dirLightDir[2]; psParams[7] = lights.dirLightDir[3];
    psParams[8] = lights.dirLightColor[0]; psParams[9] = lights.dirLightColor[1]; psParams[10] = lights.dirLightColor[2]; psParams[11] = lights.dirLightColor[3];
    psParams[12] = reflectionIntensity;
    psParams[13] = clayMode ? 1.0f : 0.0f;
    psParams[14] = 2.0f;
    psParams[15] = 0.38f;
    GX2SetPixelUniformReg(0, 16, psParams);

    const GX2Texture* activeDiffuse = diffuseTexture ? diffuseTexture
                                    : (m_defaultTexCreated ? &m_defaultNeutralTex : nullptr);
    BindMeshTextureAndSampler(activeDiffuse, m_skelDiffuseSlot);

    RHIPipelineCache::Instance().BindState(RHIPipelineState(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false));
}
#endif

void ForwardRendererGX2::RenderSkinnedMesh(GX2RBuffer& vbo, 
                                          GX2RBuffer& ibo, 
                                          uint32_t indexCount, 
                                          uint32_t vertexStride,
                                          const Matrix4x4& modelMatrix, 
                                          const Matrix4x4* boneMatrices, 
                                          uint32_t boneCount,
                                          GX2Texture* diffuseTexture,
                                          GX2Texture* envCubeMap,
                                          float reflectionIntensity,
                                          bool clayMode,
                                          bool use32BitIndices) {
#if defined(__WIIU__) || defined(__wiiu__)
    (void)envCubeMap;
    if (!m_skelShaderLoaded || indexCount == 0 || boneCount == 0 || !boneMatrices ||
        vertexStride < SKINNED_VERTEX_STRIDE) {
        return;
    }

    BindSkinnedPipeline(diffuseTexture, reflectionIntensity, clayMode);
    m_boundShaderKind = BoundShaderKind::None;
    UploadSkinnedVertexUniforms(m_skelMvpRegOffset, m_skelModelRegOffset, m_skelBonesRegOffset,
                                m_vpMatrix, modelMatrix, boneMatrices, boneCount);

    const GX2IndexType idxType = use32BitIndices ? GX2_INDEX_TYPE_U32 : GX2_INDEX_TYPE_U16;
    GX2RSetAttributeBuffer(&vbo, 0, vertexStride, 0);
    GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &ibo, idxType, indexCount, 0, 0, 1);
    m_drawCallsCount += 1;
    m_trianglesCount += indexCount / 3;
#else
    (void)vbo; (void)ibo; (void)indexCount; (void)vertexStride; (void)modelMatrix;
    (void)boneMatrices; (void)boneCount; (void)diffuseTexture; (void)envCubeMap;
    (void)reflectionIntensity; (void)clayMode; (void)use32BitIndices;
#endif
}

void ForwardRendererGX2::SubmitStaticMeshPBR(const StaticMesh& mesh,
                                             const Matrix4x4& modelMatrix,
                                             const MaterialPropertyBlock& material,
                                             uint32_t shadingMode) {
    if (!m_initialized || !mesh.IsLoaded()) {
        return;
    }
    if (m_frustumCullingEnabled && !IsMeshVisibleInFrustum(mesh, modelMatrix, m_activeFrustum)) {
        ++m_frustumCulledCount;
        return;
    }
    if (!m_renderQueue.Submit(mesh, modelMatrix, material, shadingMode, m_camPos)) {
        // Fallback seguro si la cola alcanza MAX_COMMANDS: vaciar y encolar sin perder draws
        FlushRenderQueue();
        (void)m_renderQueue.Submit(mesh, modelMatrix, material, shadingMode, m_camPos);
    }
}

void ForwardRendererGX2::FlushRenderQueue() {
    if (m_renderQueue.GetCount() == 0u) {
        return;
    }
    NELEO_PROFILE_SCOPE(ProfileZone::QueueFlush);
    m_renderQueue.Sort();
    const uint32_t count = m_renderQueue.GetCount();

    // [OPT-462] Depth Pre-Pass opcional sobre la cubeta Opaque (ya ordenada Front-to-Back al frente de la cola)
    uint32_t prePassedOpaques = 0u;
    if (m_depthPrePassEnabled && IsDepthPrePassShaderLoaded()) {
        uint32_t opaqueCount = 0u;
        while (opaqueCount < count) {
            const RenderCommandPBR& cmd = m_renderQueue.GetSortedCommand(opaqueCount);
            const bool isCutout = (cmd.material.blendMode == BlendMode::AlphaCutout) ||
                                  (cmd.shadingMode == 7u && cmd.material.materialParams[0] > 0.5f);
            if (cmd.material.blendMode != BlendMode::Opaque || isCutout || !cmd.material.depthWrite || !cmd.material.depthTest) {
                break;
            }
            ++opaqueCount;
        }
        if (opaqueCount >= 2u) {
            BeginDepthPrePass();
            for (uint32_t i = 0u; i < opaqueCount; ++i) {
                const RenderCommandPBR& cmd = m_renderQueue.GetSortedCommand(i);
                if (cmd.mesh != nullptr && cmd.mesh->GetVBO() && cmd.mesh->GetIBO()) {
                    const bool use32 = (cmd.mesh->GetHeader().indexFormat == 1u);
                    RenderDepthPrePassBuffers(*const_cast<GX2RBuffer*>(cmd.mesh->GetVBO()),
                                              *const_cast<GX2RBuffer*>(cmd.mesh->GetIBO()),
                                              cmd.mesh->GetIndexCount(),
                                              cmd.mesh->GetHeader().vertexStride,
                                              cmd.modelMatrix,
                                              use32);
                }
            }
            EndDepthPrePass();
            prePassedOpaques = opaqueCount;
        }
    }

    const LightBlockUBO& activeLights = MaterialRegistry::Instance().GetActiveLightBlock();
#if defined(__WIIU__) || defined(__wiiu__)
    const bool pbrInstReady = m_pbrInstShaderLoaded && m_instShaderLoaded && m_instanceBufferCreated;
#else
    const bool pbrInstReady = true;
#endif
    const bool canAutoBatch = m_autoBatchingEnabled && pbrInstReady && (m_shadowMode == 0u) &&
                              (activeLights.activePointLights == 0u) &&
                              (activeLights.spotLightIntensity <= 0.01f);
    constexpr uint32_t kAutoBatchMinRun = 4u;
    constexpr uint32_t kAutoBatchMaxRun = 64u;
    Matrix4x4 batchMatrices[kAutoBatchMaxRun];

    MaterialPropertyBlock matBlock{};
    for (uint32_t i = 0u; i < count; ) {
        const RenderCommandPBR& cmd = m_renderQueue.GetSortedCommand(i);
        if (cmd.mesh == nullptr) {
            ++i;
            continue;
        }
        cmd.material.ToBlock(matBlock);
        if (i < prePassedOpaques) {
            // Z-Buffer ya poblado en el Pre-Pass: desactivar escritura Z en pase de color para Early-Z/HiZ
            matBlock.depthWrite = false;
        }

        if (canAutoBatch) {
            const uint32_t maxRun = (i < prePassedOpaques)
                ? std::min(kAutoBatchMaxRun, prePassedOpaques - i)
                : kAutoBatchMaxRun;
            const uint32_t runLen = m_renderQueue.FindAutoBatchRun(i, batchMatrices, maxRun);
            if (runLen >= kAutoBatchMinRun) {
                RenderStaticMeshInstanced(*cmd.mesh,
                                          batchMatrices,
                                          runLen,
                                          static_cast<GX2Texture*>(matBlock.diffuseTexture),
                                          static_cast<GX2Texture*>(matBlock.envCubeMap),
                                          &matBlock,
                                          true);
                m_autoBatchedDraws += runLen;
                i += runLen;
                continue;
            }
        }

        RenderStaticMeshPBR(*cmd.mesh, cmd.modelMatrix, matBlock, cmd.shadingMode);
        ++i;
    }
    m_renderQueue.Clear();
}

void ForwardRendererGX2::EndFrame() {
    FlushRenderQueue();
#if defined(__WIIU__) || defined(__wiiu__)
    m_boundShaderKind = BoundShaderKind::None;
#endif
}

#if defined(__WIIU__) || defined(__wiiu__)
bool ForwardRendererGX2::InitSkyShader() {
    if (m_skyShaderLoaded) return true;

    std::vector<uint8_t> shaderData;
    bool loaded = AssetManager::LoadBinaryFile("shaders/skydome_gx2.gsh", shaderData);
    if (!loaded || shaderData.empty()) {
        loaded = AssetManager::LoadBinaryFile("shaders/skybox_gx2.gsh", shaderData);
        if (!loaded || shaderData.empty()) {
            shaderData.assign(Neleo::SKYBOX_GX2_GSH_DATA, Neleo::SKYBOX_GX2_GSH_DATA + Neleo::SKYBOX_GX2_GSH_SIZE);
            loaded = true;
        }
    }

    if (loaded && !shaderData.empty()) {
        uint32_t vsHdrSize = GFDGetVertexShaderHeaderSize(0, shaderData.data());
        uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
        uint32_t psHdrSize = GFDGetPixelShaderHeaderSize(0, shaderData.data());
        uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());
        if (vsHdrSize == 0 || vsProgSize == 0 || psHdrSize == 0 || psProgSize == 0) {
            return false;
        }

        m_skyVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "Sky_VS_Hdr"));
        m_skyVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "Sky_VS_Prog");
        m_skyPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHdrSize, MemoryTag::ShaderProgram, "Sky_PS_Hdr"));
        m_skyPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "Sky_PS_Prog");

        if (m_skyVs && m_skyVsProgram && m_skyPs && m_skyPsProgram &&
            GFDGetVertexShader(m_skyVs, m_skyVsProgram, 0, shaderData.data()) &&
            GFDGetPixelShader(m_skyPs, m_skyPsProgram, 0, shaderData.data())) {
            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_skyVsProgram, vsProgSize);
            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_skyPsProgram, psProgSize);

            GX2AttribStream attrs[4]{};
            uint32_t numAttrs = 0;

            if (m_skyVs->attribVarCount > 0 && m_skyVs->attribVars != nullptr) {
                for (uint32_t i = 0; i < m_skyVs->attribVarCount && numAttrs < 4; ++i) {
                    const auto& var = m_skyVs->attribVars[i];
                    if (var.name && std::strcmp(var.name, "aPos") == 0) {
                        attrs[numAttrs].location = var.location;
                        attrs[numAttrs].buffer = 0;
                        attrs[numAttrs].offset = offsetof(MeshVertex, pos);
                        attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                        attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[numAttrs].aluDivisor = 0;
                        attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                        attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                        numAttrs++;
                    } else if (var.name && std::strcmp(var.name, "aNormal") == 0) {
                        attrs[numAttrs].location = var.location;
                        attrs[numAttrs].buffer = 0;
                        attrs[numAttrs].offset = offsetof(MeshVertex, normal);
                        attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                        attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[numAttrs].aluDivisor = 0;
                        attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                        attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                        numAttrs++;
                    } else if (var.name && std::strcmp(var.name, "aTexCoord") == 0) {
                        attrs[numAttrs].location = var.location;
                        attrs[numAttrs].buffer = 0;
                        attrs[numAttrs].offset = offsetof(MeshVertex, texCoord);
                        attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
                        attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[numAttrs].aluDivisor = 0;
                        attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                        attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                        numAttrs++;
                    } else if (var.name && std::strcmp(var.name, "aColor") == 0) {
                        attrs[numAttrs].location = var.location;
                        attrs[numAttrs].buffer = 0;
                        attrs[numAttrs].offset = offsetof(MeshVertex, color);
                        attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
                        attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[numAttrs].aluDivisor = 0;
                        attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                        attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                        numAttrs++;
                    }
                }
            }

            if (numAttrs == 0) {
                // skydome_gx2.gsh contiene aPos (loc 0) y aTexCoord (loc 2)
                attrs[0].location = 0; attrs[0].buffer = 0; attrs[0].offset = offsetof(MeshVertex, pos);
                attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32; attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[0].aluDivisor = 0; attrs[0].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[1].location = 2; attrs[1].buffer = 0; attrs[1].offset = offsetof(MeshVertex, texCoord);
                attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32; attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[1].aluDivisor = 0; attrs[1].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                numAttrs = 2;
            }

            uint32_t fsSize = GX2CalcFetchShaderSizeEx(numAttrs, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
            m_skyFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "Sky_FS_Prog");
            if (m_skyFsProgram) {
                GX2InitFetchShaderEx(&m_skyFs, static_cast<uint8_t*>(m_skyFsProgram), numAttrs, attrs,
                                     GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_skyFsProgram, fsSize);
                m_skyShaderLoaded = true;
            }
        }
    }
    return m_skyShaderLoaded;
}

bool ForwardRendererGX2::InitMeshShader() {
    if (m_meshShaderLoaded) return true;

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
        if (vsHdrSize == 0 || vsProgSize == 0 || psHdrSize == 0 || psProgSize == 0) {
            return false;
        }

        m_meshVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "Mesh_VS_Hdr"));
        m_meshVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "Mesh_VS_Prog");
        m_meshPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHdrSize, MemoryTag::ShaderProgram, "Mesh_PS_Hdr"));
        m_meshPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "Mesh_PS_Prog");

        if (m_meshVs && m_meshVsProgram && m_meshPs && m_meshPsProgram &&
            GFDGetVertexShader(m_meshVs, m_meshVsProgram, 0, shaderData.data()) &&
            GFDGetPixelShader(m_meshPs, m_meshPsProgram, 0, shaderData.data())) {
            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_meshVsProgram, vsProgSize);
            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_meshPsProgram, psProgSize);

            GX2AttribStream attrs[4]{};
            uint32_t numAttrs = 0;

            if (m_meshVs->attribVarCount > 0 && m_meshVs->attribVars != nullptr) {
                for (uint32_t i = 0; i < m_meshVs->attribVarCount && numAttrs < 4; ++i) {
                    const auto& var = m_meshVs->attribVars[i];
                    if (var.name && std::strcmp(var.name, "aPos") == 0) {
                        attrs[numAttrs].location = var.location;
                        attrs[numAttrs].buffer = 0;
                        attrs[numAttrs].offset = offsetof(MeshVertex, pos);
                        attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                        attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[numAttrs].aluDivisor = 0;
                        attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                        attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                        numAttrs++;
                    } else if (var.name && std::strcmp(var.name, "aNormal") == 0) {
                        attrs[numAttrs].location = var.location;
                        attrs[numAttrs].buffer = 0;
                        attrs[numAttrs].offset = offsetof(MeshVertex, normal);
                        attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                        attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[numAttrs].aluDivisor = 0;
                        attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                        attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                        numAttrs++;
                    } else if (var.name && std::strcmp(var.name, "aTexCoord") == 0) {
                        attrs[numAttrs].location = var.location;
                        attrs[numAttrs].buffer = 0;
                        attrs[numAttrs].offset = offsetof(MeshVertex, texCoord);
                        attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
                        attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[numAttrs].aluDivisor = 0;
                        attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                        attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                        numAttrs++;
                    } else if (var.name && std::strcmp(var.name, "aColor") == 0) {
                        attrs[numAttrs].location = var.location;
                        attrs[numAttrs].buffer = 0;
                        attrs[numAttrs].offset = offsetof(MeshVertex, color);
                        attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
                        attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[numAttrs].aluDivisor = 0;
                        attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                        attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                        numAttrs++;
                    }
                }
            }

            if (numAttrs == 0) {
                attrs[0].location = 0; attrs[0].buffer = 0; attrs[0].offset = offsetof(MeshVertex, pos);
                attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32; attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[0].aluDivisor = 0; attrs[0].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[1].location = 1; attrs[1].buffer = 0; attrs[1].offset = offsetof(MeshVertex, normal);
                attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32; attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[1].aluDivisor = 0; attrs[1].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[2].location = 2; attrs[2].buffer = 0; attrs[2].offset = offsetof(MeshVertex, texCoord);
                attrs[2].format = GX2_ATTRIB_FORMAT_FLOAT_32_32; attrs[2].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[2].aluDivisor = 0; attrs[2].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                attrs[2].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                attrs[3].location = 3; attrs[3].buffer = 0; attrs[3].offset = offsetof(MeshVertex, color);
                attrs[3].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32; attrs[3].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                attrs[3].aluDivisor = 0; attrs[3].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                attrs[3].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                numAttrs = 4;
            }

            uint32_t fsSize = GX2CalcFetchShaderSizeEx(numAttrs, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
            m_meshFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "Mesh_FS_Prog");
            if (m_meshFsProgram) {
                GX2InitFetchShaderEx(&m_meshFs, static_cast<uint8_t*>(m_meshFsProgram), numAttrs, attrs,
                                     GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_meshFsProgram, fsSize);
                m_meshShaderLoaded = true;
            }
        }
    }
    return m_meshShaderLoaded;
}

bool ForwardRendererGX2::InitSkinningShader() {
    if (m_skelShaderLoaded) return true;

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
        if (vsHdrSize == 0 || vsProgSize == 0 || psHdrSize == 0 || psProgSize == 0) {
            return false;
        }

        m_skelVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "Skel_VS_Hdr"));
        m_skelVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "Skel_VS_Prog");
        m_skelPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHdrSize, MemoryTag::ShaderProgram, "Skel_PS_Hdr"));
        m_skelPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "Skel_PS_Prog");

        if (m_skelVs && m_skelVsProgram && m_skelPs && m_skelPsProgram &&
            GFDGetVertexShader(m_skelVs, m_skelVsProgram, 0, shaderData.data()) &&
            GFDGetPixelShader(m_skelPs, m_skelPsProgram, 0, shaderData.data())) {
            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_skelVsProgram, vsProgSize);
            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_skelPsProgram, psProgSize);

            m_skelMvpRegOffset = 0u;
            m_skelModelRegOffset = 16u;
            m_skelBonesRegOffset = 32u;
            if (m_skelVs->uniformVars) {
                for (uint32_t i = 0u; i < m_skelVs->uniformVarCount; ++i) {
                    const char* vname = m_skelVs->uniformVars[i].name;
                    if (!vname) continue;
                    if (std::strcmp(vname, "uMVP") == 0) {
                        m_skelMvpRegOffset = m_skelVs->uniformVars[i].offset;
                    } else if (std::strcmp(vname, "uModel") == 0) {
                        m_skelModelRegOffset = m_skelVs->uniformVars[i].offset;
                    } else if (std::strcmp(vname, "uBones") == 0 || std::strcmp(vname, "uBones[0]") == 0) {
                        m_skelBonesRegOffset = m_skelVs->uniformVars[i].offset;
                    }
                }
            }

            m_skelDiffuseSlot = 0u;
            if (m_skelPs->samplerVars) {
                for (uint32_t s = 0u; s < m_skelPs->samplerVarCount; ++s) {
                    const char* sname = m_skelPs->samplerVars[s].name;
                    if (sname && std::strcmp(sname, "uTexture") == 0) {
                        m_skelDiffuseSlot = m_skelPs->samplerVars[s].location;
                        break;
                    }
                }
            }

            GX2AttribStream attrs[6]{};
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
            attrs[4].endianSwap = GX2_ENDIAN_SWAP_NONE;

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
    return m_skelShaderLoaded;
}

bool ForwardRendererGX2::InitInstancedShader() {
    if (m_instShaderLoaded) return true;

    std::vector<uint8_t> shaderData;
    if (!AssetManager::LoadBinaryFile("shaders/instanced_mesh_gx2.gsh", shaderData) || shaderData.empty()) {
        shaderData.assign(INSTANCED_MESH_GX2_GSH_DATA, INSTANCED_MESH_GX2_GSH_DATA + INSTANCED_MESH_GX2_GSH_SIZE);
    }
    if (shaderData.empty()) return false;

    const uint32_t vsHdrSize  = GFDGetVertexShaderHeaderSize(0, shaderData.data());
    const uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
    const uint32_t psHdrSize  = GFDGetPixelShaderHeaderSize(0, shaderData.data());
    const uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());
    if (vsHdrSize == 0 || vsProgSize == 0 || psHdrSize == 0 || psProgSize == 0) {
        return false;
    }

    auto cleanupInstPartial = [this]() {
        if (m_instVs)        { void* p = m_instVs; MemoryManager::Instance().Free(p); m_instVs = nullptr; }
        if (m_instVsProgram) { MemoryManager::Instance().FreeAligned(m_instVsProgram); m_instVsProgram = nullptr; }
        if (m_instPs)        { void* p = m_instPs; MemoryManager::Instance().Free(p); m_instPs = nullptr; }
        if (m_instPsProgram) { MemoryManager::Instance().FreeAligned(m_instPsProgram); m_instPsProgram = nullptr; }
        if (m_instFsProgram) { MemoryManager::Instance().FreeAligned(m_instFsProgram); m_instFsProgram = nullptr; }
    };

    m_instVs        = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "Inst_VS_Hdr"));
    m_instVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "Inst_VS_Prog");
    m_instPs        = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHdrSize, MemoryTag::ShaderProgram, "Inst_PS_Hdr"));
    m_instPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "Inst_PS_Prog");

    if (!m_instVs || !m_instVsProgram || !m_instPs || !m_instPsProgram ||
        !GFDGetVertexShader(m_instVs, m_instVsProgram, 0, shaderData.data()) ||
        !GFDGetPixelShader(m_instPs, m_instPsProgram, 0, shaderData.data())) {
        cleanupInstPartial();
        return false;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_instVsProgram, vsProgSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_instPsProgram, psProgSize);

    // 8 atributos: [0..3] geometria por vertice (buffer 0), [4..7] matriz de modelo por instancia (buffer 1, divisor 1)
    GX2AttribStream attrs[8]{};
    attrs[0] = {0, 0, static_cast<uint32_t>(offsetof(MeshVertex, pos)),      GX2_ATTRIB_FORMAT_FLOAT_32_32_32,    GX2_ATTRIB_INDEX_PER_VERTEX,   0, GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1), GX2_ENDIAN_SWAP_DEFAULT};
    attrs[1] = {1, 0, static_cast<uint32_t>(offsetof(MeshVertex, normal)),   GX2_ATTRIB_FORMAT_FLOAT_32_32_32,    GX2_ATTRIB_INDEX_PER_VERTEX,   0, GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1), GX2_ENDIAN_SWAP_DEFAULT};
    attrs[2] = {2, 0, static_cast<uint32_t>(offsetof(MeshVertex, texCoord)), GX2_ATTRIB_FORMAT_FLOAT_32_32,       GX2_ATTRIB_INDEX_PER_VERTEX,   0, GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1), GX2_ENDIAN_SWAP_DEFAULT};
    attrs[3] = {3, 0, static_cast<uint32_t>(offsetof(MeshVertex, color)),    GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32, GX2_ATTRIB_INDEX_PER_VERTEX,   0, GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W), GX2_ENDIAN_SWAP_DEFAULT};

    for (uint32_t col = 0; col < 4; ++col) {
        attrs[4 + col] = {
            4u + col, 1u, col * 16u,
            GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32,
            GX2_ATTRIB_INDEX_PER_INSTANCE,
            1u,
            GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W),
            GX2_ENDIAN_SWAP_DEFAULT
        };
    }

    const uint32_t fsSize = GX2CalcFetchShaderSizeEx(8, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    m_instFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "Inst_FS_Prog");
    if (!m_instFsProgram) {
        cleanupInstPartial();
        return false;
    }

    GX2InitFetchShaderEx(&m_instFs, static_cast<uint8_t*>(m_instFsProgram), 8, attrs,
                         GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_instFsProgram, fsSize);

    // Pre-alojar Multi-Frame Ring Buffer de matrices por instancia (3 frames * 1024 = 3072 matrices = 192 KB, 0 allocs a 60 FPS)
    std::memset(&m_instanceMatrixBuffer, 0, sizeof(m_instanceMatrixBuffer));
    m_instanceMatrixBuffer.flags = static_cast<GX2RResourceFlags>(
        GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
    m_instanceMatrixBuffer.elemSize  = sizeof(Matrix4x4);
    m_instanceMatrixBuffer.elemCount = INSTANCE_RING_CAPACITY;
    if (GX2RCreateBuffer(&m_instanceMatrixBuffer)) {
        m_instanceBufferCreated = true;
    } else {
        cleanupInstPartial();
        return false;
    }

    m_instShaderLoaded = m_instanceBufferCreated;
    return m_instShaderLoaded;
}

bool ForwardRendererGX2::InitDepthPrePassShader() {
    if (m_depthPreShaderLoaded) return true;

    std::vector<uint8_t> shaderData;
    if (!AssetManager::LoadBinaryFile("shaders/shadow_depth_gx2.gsh", shaderData) || shaderData.empty()) {
        shaderData.assign(SHADOW_DEPTH_GX2_GSH_DATA, SHADOW_DEPTH_GX2_GSH_DATA + SHADOW_DEPTH_GX2_GSH_SIZE);
    }
    if (shaderData.empty()) return false;

    const uint32_t vsHdrSize  = GFDGetVertexShaderHeaderSize(0, shaderData.data());
    const uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
    const uint32_t psHdrSize  = GFDGetPixelShaderHeaderSize(0, shaderData.data());
    const uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());
    if (vsHdrSize == 0 || vsProgSize == 0 || psHdrSize == 0 || psProgSize == 0) {
        return false;
    }

    auto cleanupDepthPartial = [this]() {
        if (m_depthPreVs)        { void* p = m_depthPreVs; MemoryManager::Instance().Free(p); m_depthPreVs = nullptr; }
        if (m_depthPreVsProgram) { MemoryManager::Instance().FreeAligned(m_depthPreVsProgram); m_depthPreVsProgram = nullptr; }
        if (m_depthPrePs)        { void* p = m_depthPrePs; MemoryManager::Instance().Free(p); m_depthPrePs = nullptr; }
        if (m_depthPrePsProgram) { MemoryManager::Instance().FreeAligned(m_depthPrePsProgram); m_depthPrePsProgram = nullptr; }
        if (m_depthPreFsProgram) { MemoryManager::Instance().FreeAligned(m_depthPreFsProgram); m_depthPreFsProgram = nullptr; }
    };

    m_depthPreVs        = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "EarlyZ_VS_Hdr"));
    m_depthPreVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "EarlyZ_VS_Prog");
    m_depthPrePs        = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHdrSize, MemoryTag::ShaderProgram, "EarlyZ_PS_Hdr"));
    m_depthPrePsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "EarlyZ_PS_Prog");

    if (!m_depthPreVs || !m_depthPreVsProgram || !m_depthPrePs || !m_depthPrePsProgram ||
        !GFDGetVertexShader(m_depthPreVs, m_depthPreVsProgram, 0, shaderData.data()) ||
        !GFDGetPixelShader(m_depthPrePs, m_depthPrePsProgram, 0, shaderData.data())) {
        cleanupDepthPartial();
        return false;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_depthPreVsProgram, vsProgSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_depthPrePsProgram, psProgSize);

    GX2AttribStream attr{};
    attr.location   = 0;
    attr.buffer     = 0;
    attr.offset     = offsetof(MeshVertex, pos);
    attr.format     = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
    attr.type       = GX2_ATTRIB_INDEX_PER_VERTEX;
    attr.aluDivisor = 0;
    attr.mask       = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
    attr.endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

    const uint32_t fsSize = GX2CalcFetchShaderSizeEx(1, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    m_depthPreFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "EarlyZ_FS_Prog");
    if (!m_depthPreFsProgram) {
        cleanupDepthPartial();
        return false;
    }

    GX2InitFetchShaderEx(&m_depthPreFs, static_cast<uint8_t*>(m_depthPreFsProgram), 1, &attr,
                         GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_depthPreFsProgram, fsSize);
    m_depthPreShaderLoaded = true;
    return true;
}
#endif

void ForwardRendererGX2::RenderStaticMeshWithMaterial(const StaticMesh& mesh,
                                                      const Matrix4x4& modelMatrix,
                                                      const MaterialPropertyBlock& material) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!mesh.IsLoaded() || !mesh.GetVBO() || !mesh.GetIBO()) return;
    RenderMeshBuffers(*const_cast<GX2RBuffer*>(mesh.GetVBO()),
                      *const_cast<GX2RBuffer*>(mesh.GetIBO()),
                      mesh.GetIndexCount(),
                      mesh.GetHeader().vertexStride,
                      modelMatrix,
                      static_cast<GX2Texture*>(material.diffuseTexture),
                      static_cast<GX2Texture*>(material.envCubeMap),
                      material.ubo.materialParams[0],
                      material.ubo.materialParams[1] > 0.5f,
                      material.ubo.materialParams[2],
                      material.ubo.materialParams[3],
                      mesh.GetHeader().indexFormat == 1,
                      material.blendMode,
                      material.depthTest,
                      material.depthWrite,
                      RHI_COMPARE_LEQUAL);
#else
    (void)mesh; (void)modelMatrix; (void)material;
#endif
}

#if defined(__WIIU__) || defined(__wiiu__)
namespace {
bool LoadPBRVariantFromGSH(const uint8_t* raw,
                           uint32_t rawSize,
                           const char* vsHdrTag,
                           const char* vsProgTag,
                           const char* psHdrTag,
                           const char* psProgTag,
                           const char* fsProgTag,
                           GX2VertexShader*& outVs,
                           void*& outVsProgram,
                           GX2PixelShader*& outPs,
                           void*& outPsProgram,
                           GX2FetchShader& outFs,
                           void*& outFsProgram,
                           int32_t& outDiffuseSlot,
                           int32_t& outSkyboxSlot,
                           int32_t* outShadowSlot) {
    if (!raw || rawSize == 0u) {
        return false;
    }

    const uint32_t vsHdrSize = GFDGetVertexShaderHeaderSize(0, raw);
    const uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, raw);
    const uint32_t psHdrSize = GFDGetPixelShaderHeaderSize(0, raw);
    const uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, raw);
    if (vsHdrSize == 0u || vsProgSize == 0u || psHdrSize == 0u || psProgSize == 0u) {
        return false;
    }

    auto cleanupPartial = [&]() {
        if (outVs)        { void* p = outVs; MemoryManager::Instance().Free(p); outVs = nullptr; }
        if (outVsProgram) { MemoryManager::Instance().FreeAligned(outVsProgram); outVsProgram = nullptr; }
        if (outPs)        { void* p = outPs; MemoryManager::Instance().Free(p); outPs = nullptr; }
        if (outPsProgram) { MemoryManager::Instance().FreeAligned(outPsProgram); outPsProgram = nullptr; }
        if (outFsProgram) { MemoryManager::Instance().FreeAligned(outFsProgram); outFsProgram = nullptr; }
    };

    outVs        = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, vsHdrTag));
    outVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, vsProgTag);
    outPs        = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHdrSize, MemoryTag::ShaderProgram, psHdrTag));
    outPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, psProgTag);

    if (!outVs || !outVsProgram || !outPs || !outPsProgram ||
        !GFDGetVertexShader(outVs, outVsProgram, 0, raw) ||
        !GFDGetPixelShader(outPs, outPsProgram, 0, raw)) {
        cleanupPartial();
        return false;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, outVsProgram, vsProgSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, outPsProgram, psProgSize);

    GX2AttribStream attrs[4]{};
    uint32_t numAttrs = 0u;
    if (outVs->attribVarCount > 0u && outVs->attribVars != nullptr) {
        for (uint32_t i = 0u; i < outVs->attribVarCount && numAttrs < 4u; ++i) {
            const auto& var = outVs->attribVars[i];
            if (!var.name) continue;
            if (std::strcmp(var.name, "aPos") == 0) {
                attrs[numAttrs] = {var.location, 0u, static_cast<uint32_t>(offsetof(MeshVertex, pos)),
                                   GX2_ATTRIB_FORMAT_FLOAT_32_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0u,
                                   GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1), GX2_ENDIAN_SWAP_DEFAULT};
                ++numAttrs;
            } else if (std::strcmp(var.name, "aNormal") == 0) {
                attrs[numAttrs] = {var.location, 0u, static_cast<uint32_t>(offsetof(MeshVertex, normal)),
                                   GX2_ATTRIB_FORMAT_FLOAT_32_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0u,
                                   GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1), GX2_ENDIAN_SWAP_DEFAULT};
                ++numAttrs;
            } else if (std::strcmp(var.name, "aTexCoord") == 0) {
                attrs[numAttrs] = {var.location, 0u, static_cast<uint32_t>(offsetof(MeshVertex, texCoord)),
                                   GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0u,
                                   GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1), GX2_ENDIAN_SWAP_DEFAULT};
                ++numAttrs;
            } else if (std::strcmp(var.name, "aColor") == 0) {
                attrs[numAttrs] = {var.location, 0u, static_cast<uint32_t>(offsetof(MeshVertex, color)),
                                   GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0u,
                                   GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W), GX2_ENDIAN_SWAP_DEFAULT};
                ++numAttrs;
            }
        }
    }
    if (numAttrs == 0u) {
        cleanupPartial();
        return false;
    }

    const uint32_t fsSize = GX2CalcFetchShaderSizeEx(numAttrs, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    outFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, fsProgTag);
    if (!outFsProgram) {
        cleanupPartial();
        return false;
    }

    GX2InitFetchShaderEx(&outFs, static_cast<uint8_t*>(outFsProgram), numAttrs, attrs,
                         GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, outFsProgram, fsSize);

    // Reflexion dinamica de slots TMU desde samplerVars (inmune a dead-code elimination de CafeGLSL)
    outDiffuseSlot = -1;
    outSkyboxSlot  = -1;
    if (outShadowSlot) {
        *outShadowSlot = -1;
    }
    if (outPs->samplerVarCount > 0u && outPs->samplerVars != nullptr) {
        for (uint32_t s = 0u; s < outPs->samplerVarCount; ++s) {
            const auto& svar = outPs->samplerVars[s];
            if (!svar.name) continue;
            if (std::strcmp(svar.name, "uTexture") == 0) {
                outDiffuseSlot = static_cast<int32_t>(svar.location);
            } else if (std::strcmp(svar.name, "uSkybox") == 0) {
                outSkyboxSlot = static_cast<int32_t>(svar.location);
            } else if (outShadowSlot && std::strcmp(svar.name, "uShadowMap") == 0) {
                *outShadowSlot = static_cast<int32_t>(svar.location);
            }
        }
    }
    if (outDiffuseSlot < 0) {
        outDiffuseSlot = 0;
    }
    return true;
}
} // namespace
#endif

bool ForwardRendererGX2::InitPBRShader() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_pbrShaderLoaded) return true;
    m_pbrShaderLoaded = LoadPBRVariantFromGSH(
        PBR_MESH_GX2_GSH_DATA, PBR_MESH_GX2_GSH_SIZE,
        "PBR_VS_Hdr", "PBR_VS_Prog", "PBR_PS_Hdr", "PBR_PS_Prog", "PBR_FS_Prog",
        m_pbrVs, m_pbrVsProgram, m_pbrPs, m_pbrPsProgram, m_pbrFs, m_pbrFsProgram,
        m_pbrDiffuseSlot, m_pbrSkyboxSlot, &m_pbrShadowSlot);
    return m_pbrShaderLoaded;
#else
    return true;
#endif
}

bool ForwardRendererGX2::InitPBRVariants() {
#if defined(__WIIU__) || defined(__wiiu__)
    struct VariantDesc {
        PBRVariantIndex index;
        const uint8_t* data;
        uint32_t size;
        const char* vsHdr;
        const char* vsProg;
        const char* psHdr;
        const char* psProg;
        const char* fsProg;
        bool hasShadow;
    };
    static constexpr VariantDesc kDescs[static_cast<size_t>(PBRVariantIndex::Count)] = {
        {PBRVariantIndex::DirOnly,    PBR_DIR_GX2_GSH_DATA,        PBR_DIR_GX2_GSH_SIZE,
         "PBRDir_VS_Hdr",  "PBRDir_VS_Prog",  "PBRDir_PS_Hdr",  "PBRDir_PS_Prog",  "PBRDir_FS_Prog",  false},
        {PBRVariantIndex::DirBump,    PBR_DIR_BUMP_GX2_GSH_DATA,   PBR_DIR_BUMP_GX2_GSH_SIZE,
         "PBRDirB_VS_Hdr", "PBRDirB_VS_Prog", "PBRDirB_PS_Hdr", "PBRDirB_PS_Prog", "PBRDirB_FS_Prog", false},
        {PBRVariantIndex::MultiLight, PBR_MULTI_GX2_GSH_DATA,      PBR_MULTI_GX2_GSH_SIZE,
         "PBRMlt_VS_Hdr",  "PBRMlt_VS_Prog",  "PBRMlt_PS_Hdr",  "PBRMlt_PS_Prog",  "PBRMlt_FS_Prog",  false},
        {PBRVariantIndex::MultiBump,  PBR_MULTI_BUMP_GX2_GSH_DATA, PBR_MULTI_BUMP_GX2_GSH_SIZE,
         "PBRMltB_VS_Hdr", "PBRMltB_VS_Prog", "PBRMltB_PS_Hdr", "PBRMltB_PS_Prog", "PBRMltB_FS_Prog", false},
        {PBRVariantIndex::Shadow,     PBR_SHADOW_GX2_GSH_DATA,     PBR_SHADOW_GX2_GSH_SIZE,
         "PBRShd_VS_Hdr",  "PBRShd_VS_Prog",  "PBRShd_PS_Hdr",  "PBRShd_PS_Prog",  "PBRShd_FS_Prog",  true}
    };

    bool allOk = true;
    for (const auto& d : kDescs) {
        auto& slot = m_pbrVariants[static_cast<size_t>(d.index)];
        if (slot.loaded) continue;
        slot.loaded = LoadPBRVariantFromGSH(
            d.data, d.size, d.vsHdr, d.vsProg, d.psHdr, d.psProg, d.fsProg,
            slot.vs, slot.vsProgram, slot.ps, slot.psProgram, slot.fs, slot.fsProgram,
            slot.diffuseSlot, slot.skyboxSlot, d.hasShadow ? &slot.shadowSlot : nullptr);
        if (!slot.loaded) {
            allOk = false;
        }
    }
    return allOk;
#else
    return true;
#endif
}

bool ForwardRendererGX2::InitWaterGlassShader() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_waterGlassShaderLoaded) return true;
    m_waterGlassShaderLoaded = LoadPBRVariantFromGSH(
        WATER_GLASS_GX2_GSH_DATA, WATER_GLASS_GX2_GSH_SIZE,
        "WtrGls_VS_Hdr", "WtrGls_VS_Prog", "WtrGls_PS_Hdr", "WtrGls_PS_Prog", "WtrGls_FS_Prog",
        m_waterGlassVs, m_waterGlassVsProgram, m_waterGlassPs, m_waterGlassPsProgram, m_waterGlassFs, m_waterGlassFsProgram,
        m_waterGlassDiffuseSlot, m_waterGlassSkyboxSlot, nullptr);
    return m_waterGlassShaderLoaded;
#else
    return true;
#endif
}

bool ForwardRendererGX2::InitHDRFogShader() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_hdrFogPipe.loaded) return true;
    m_hdrFogPipe.loaded = LoadPBRVariantFromGSH(
        HDR_FOG_GX2_GSH_DATA, HDR_FOG_GX2_GSH_SIZE,
        "HdrFog_VS_Hdr", "HdrFog_VS_Prog", "HdrFog_PS_Hdr", "HdrFog_PS_Prog", "HdrFog_FS_Prog",
        m_hdrFogPipe.vs, m_hdrFogPipe.vsProgram, m_hdrFogPipe.ps, m_hdrFogPipe.psProgram, m_hdrFogPipe.fs, m_hdrFogPipe.fsProgram,
        m_hdrFogPipe.diffuseSlot, m_hdrFogPipe.skyboxSlot, nullptr);
    return m_hdrFogPipe.loaded;
#else
    return true;
#endif
}

bool ForwardRendererGX2::InitPBRInstancedShader() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_pbrInstShaderLoaded) return true;
    const uint8_t* raw = PBR_INST_DIR_GX2_GSH_DATA;
    const uint32_t rawSize = PBR_INST_DIR_GX2_GSH_SIZE;
    if (!raw || rawSize == 0u) return false;

    const uint32_t vsHdrSize  = GFDGetVertexShaderHeaderSize(0, raw);
    const uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, raw);
    const uint32_t psHdrSize  = GFDGetPixelShaderHeaderSize(0, raw);
    const uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, raw);
    if (vsHdrSize == 0u || vsProgSize == 0u || psHdrSize == 0u || psProgSize == 0u) return false;

    auto cleanupPartial = [this]() {
        if (m_pbrInstVs)        { void* p = m_pbrInstVs; MemoryManager::Instance().Free(p); m_pbrInstVs = nullptr; }
        if (m_pbrInstVsProgram) { MemoryManager::Instance().FreeAligned(m_pbrInstVsProgram); m_pbrInstVsProgram = nullptr; }
        if (m_pbrInstPs)        { void* p = m_pbrInstPs; MemoryManager::Instance().Free(p); m_pbrInstPs = nullptr; }
        if (m_pbrInstPsProgram) { MemoryManager::Instance().FreeAligned(m_pbrInstPsProgram); m_pbrInstPsProgram = nullptr; }
    };

    m_pbrInstVs        = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "PBRInst_VS_Hdr"));
    m_pbrInstVsProgram = MemoryManager::Instance().AllocAligned(256u, vsProgSize, MemoryTag::ShaderProgram, "PBRInst_VS_Prog");
    m_pbrInstPs        = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHdrSize, MemoryTag::ShaderProgram, "PBRInst_PS_Hdr"));
    m_pbrInstPsProgram = MemoryManager::Instance().AllocAligned(256u, psProgSize, MemoryTag::ShaderProgram, "PBRInst_PS_Prog");

    if (!m_pbrInstVs || !m_pbrInstVsProgram || !m_pbrInstPs || !m_pbrInstPsProgram ||
        !GFDGetVertexShader(m_pbrInstVs, m_pbrInstVsProgram, 0, raw) ||
        !GFDGetPixelShader(m_pbrInstPs, m_pbrInstPsProgram, 0, raw)) {
        cleanupPartial();
        return false;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_pbrInstVsProgram, vsProgSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_pbrInstPsProgram, psProgSize);

    m_pbrInstDiffuseSlot = 0;
    m_pbrInstSkyboxSlot = -1;
    if (m_pbrInstPs->samplerVars) {
        for (uint32_t i = 0u; i < m_pbrInstPs->samplerVarCount; ++i) {
            const char* sname = m_pbrInstPs->samplerVars[i].name;
            if (!sname) continue;
            if (std::strcmp(sname, "uTexture") == 0) m_pbrInstDiffuseSlot = static_cast<int32_t>(m_pbrInstPs->samplerVars[i].location);
            else if (std::strcmp(sname, "uSkybox") == 0) m_pbrInstSkyboxSlot = static_cast<int32_t>(m_pbrInstPs->samplerVars[i].location);
        }
    }

    m_pbrInstShaderLoaded = true;
    return true;
#else
    return true;
#endif
}

void ForwardRendererGX2::SetDirectionalShadowState(const Matrix4x4& lightVP,
                                                   uint32_t shadowMode,
                                                   float depthBias,
                                                   float texelSize,
                                                   float minLitFactor) noexcept {
    m_shadowLightVP = lightVP;
    m_shadowMode = shadowMode;
    m_shadowDepthBias = depthBias;
    m_shadowTexelSize = (texelSize > 0.0f) ? texelSize : (1.0f / 1024.0f);
    m_shadowMinLit = minLitFactor;
}

void ForwardRendererGX2::RenderStaticMeshPBR(const StaticMesh& mesh,
                                             const Matrix4x4& modelMatrix,
                                             const MaterialPropertyBlock& material,
                                             uint32_t shadingMode) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!mesh.IsLoaded() || !mesh.GetVBO() || !mesh.GetIBO()) return;
    if (!m_pbrShaderLoaded) {
        RenderStaticMeshWithMaterial(mesh, modelMatrix, material);
        return;
    }

    const LightBlockUBO& lights = MaterialRegistry::Instance().GetActiveLightBlock();
    const bool shadowActive = (m_shadowMode > 0u) && ShadowMapRendererGX2::Instance().IsReady();
    const bool useWaterGlass = (shadingMode >= 4u && shadingMode <= 6u) && m_waterGlassShaderLoaded;
    const bool useHdrFog = (shadingMode == 7u) && m_hdrFogPipe.loaded;

    // [OPT-457] Light-Culling por Bounding Sphere en CPU (Zero-Alloc) + seleccion O(1) por ShaderVariantKey
    uint32_t culledIdx[4]{};
    const uint32_t numPoints = useWaterGlass
        ? 0u
        : CullPointLightsForMesh(mesh, modelMatrix, lights, culledIdx, 4u);
    const bool hasExtraLights = (numPoints > 0u) || (lights.spotLightIntensity > 0.01f);
    const bool hasNormalMap = (material.diffuseTexture != nullptr) &&
                              (material.ubo.materialParams[1] > 0.5f) &&
                              (material.ubo.materialParams[2] > 0.01f);

    const PBRVariantIndex chosenVariant = SelectPBRVariant(
        MakeShaderVariantKey(hasExtraLights, hasNormalMap, shadowActive));
    const uint8_t variantIdxU8 = static_cast<uint8_t>(chosenVariant);
    const PBRShaderPipeline& variantPipe = m_pbrVariants[variantIdxU8];
    const bool useVariant = !useWaterGlass && !useHdrFog && (shadingMode <= 1u) && variantPipe.loaded;

    if (useWaterGlass) {
        if (m_boundShaderKind != BoundShaderKind::WaterGlass) {
            GX2SetFetchShader(&m_waterGlassFs);
            GX2SetVertexShader(m_waterGlassVs);
            GX2SetPixelShader(m_waterGlassPs);
            m_boundShaderKind = BoundShaderKind::WaterGlass;
            m_boundPbrVariantIndex = 0xFFu;
        }
    } else if (useHdrFog) {
        if (m_boundShaderKind != BoundShaderKind::HDRFog) {
            GX2SetFetchShader(&m_hdrFogPipe.fs);
            GX2SetVertexShader(m_hdrFogPipe.vs);
            GX2SetPixelShader(m_hdrFogPipe.ps);
            m_boundShaderKind = BoundShaderKind::HDRFog;
            m_boundPbrVariantIndex = 0xFFu;
        }
    } else if (useVariant) {
        if (m_boundShaderKind != BoundShaderKind::PBRVariant || m_boundPbrVariantIndex != variantIdxU8) {
            GX2SetFetchShader(const_cast<GX2FetchShader*>(&variantPipe.fs));
            GX2SetVertexShader(variantPipe.vs);
            GX2SetPixelShader(variantPipe.ps);
            m_boundShaderKind = BoundShaderKind::PBRVariant;
            m_boundPbrVariantIndex = variantIdxU8;
        }
    } else {
        if (m_boundShaderKind != BoundShaderKind::PBRUber) {
            GX2SetFetchShader(&m_pbrFs);
            GX2SetVertexShader(m_pbrVs);
            GX2SetPixelShader(m_pbrPs);
            m_boundShaderKind = BoundShaderKind::PBRUber;
            m_boundPbrVariantIndex = 0xFFu;
        }
    }

    const Matrix4x4 mvp = m_vpMatrix * modelMatrix;
    alignas(256) float vsUniforms[48];
    std::memcpy(&vsUniforms[0], mvp.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], modelMatrix.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[32], m_shadowLightVP.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 48, vsUniforms);

    alignas(256) float psData[72]{};
    // [0] camPos + shadingMode
    psData[0] = m_camPos.x; psData[1] = m_camPos.y; psData[2] = m_camPos.z; psData[3] = static_cast<float>(shadingMode);
    // [1] dirLightDir + elapsedTime (para olas de agua y efectos temporales)
    psData[4] = lights.dirLightDir[0]; psData[5] = lights.dirLightDir[1]; psData[6] = lights.dirLightDir[2]; psData[7] = m_shaderTime;
    // [2] dirLightColor
    psData[8] = lights.dirLightColor[0]; psData[9] = lights.dirLightColor[1]; psData[10] = lights.dirLightColor[2]; psData[11] = lights.dirLightColor[3];
    // [3] albedoTint + customParam (alphaCutout flag en modos 0..1 / ior en vidrio / waveAmp en agua)
    psData[12] = material.ubo.albedoTint[0];
    psData[13] = material.ubo.albedoTint[1];
    psData[14] = material.ubo.albedoTint[2];
    psData[15] = (shadingMode <= 1u)
        ? ((material.blendMode == BlendMode::AlphaCutout) ? 1.0f : 0.0f)
        : material.ubo.pbrParams[3];
    // [4] materialParams (refl, normalMapEnabled, bumpScale, ambient)
    psData[16] = material.ubo.materialParams[0];
    psData[17] = material.ubo.materialParams[1];
    psData[18] = material.ubo.materialParams[2];
    psData[19] = material.ubo.materialParams[3];
    // [5] pbrParams (roughness, metallic, numPoints, spotIntensity)
    psData[20] = material.ubo.pbrParams[0];
    psData[21] = material.ubo.pbrParams[1];
    psData[22] = static_cast<float>(numPoints);
    psData[23] = lights.spotLightIntensity;

    const bool isDirOnlyVariant = useVariant &&
        (chosenVariant == PBRVariantIndex::DirOnly || chosenVariant == PBRVariantIndex::DirBump);
    if (useWaterGlass || isDirOnlyVariant) {
        // Ruta rapida 24 floats: water_glass_gx2 o variantes direccionales (pbr_dir / pbr_dir_bump)
        GX2SetPixelUniformReg(0, 24, psData);
    } else {
        // [6..13] pointLights compactadas tras Light-Culling por Bounding Sphere
        for (uint32_t i = 0u; i < numPoints; ++i) {
            const uint32_t srcIdx = culledIdx[i];
            std::memcpy(&psData[24u + i * 4u], lights.pointLightPosRadius[srcIdx], 4u * sizeof(float));
            std::memcpy(&psData[40u + i * 4u], lights.pointLightColorIntensity[srcIdx], 4u * sizeof(float));
        }
        // [14..16] spotLight
        if (lights.spotLightIntensity > 0.01f) {
            std::memcpy(&psData[56], lights.spotLightPosRange, 4u * sizeof(float));
            std::memcpy(&psData[60], lights.spotLightDirInner, 4u * sizeof(float));
            std::memcpy(&psData[64], lights.spotLightColorOuter, 4u * sizeof(float));
        }
        if (useHdrFog) {
            // Ruta rapida 56 floats: hdr_fog_gx2 (uPsData[14], sin Spot ni Shadow)
            GX2SetPixelUniformReg(0, 56, psData);
        } else if (useVariant && chosenVariant != PBRVariantIndex::Shadow) {
            GX2SetPixelUniformReg(0, 68, psData);
        } else {
            // [17] shadowParams (shadowMode, depthBias, texelSize, minLitFactor)
            psData[68] = shadowActive ? static_cast<float>(m_shadowMode) : 0.0f;
            psData[69] = m_shadowDepthBias;
            psData[70] = m_shadowTexelSize;
            psData[71] = m_shadowMinLit;
            GX2SetPixelUniformReg(0, 72, psData);
        }
    }

    GX2Texture* diffuseTex = static_cast<GX2Texture*>(material.diffuseTexture);
    GX2Texture* envCube = static_cast<GX2Texture*>(material.envCubeMap);
    const GX2Texture* activeDiffuse = diffuseTex ? diffuseTex : (m_defaultTexCreated ? &m_defaultNeutralTex : nullptr);
    const int32_t diffSlot = useWaterGlass ? m_waterGlassDiffuseSlot
                           : (useHdrFog ? m_hdrFogPipe.diffuseSlot
                           : (useVariant ? variantPipe.diffuseSlot : m_pbrDiffuseSlot));
    const int32_t skySlot  = useWaterGlass ? m_waterGlassSkyboxSlot
                           : (useHdrFog ? -1
                           : (useVariant ? variantPipe.skyboxSlot : m_pbrSkyboxSlot));
    const int32_t shdSlot  = useVariant ? variantPipe.shadowSlot
                           : ((useWaterGlass || useHdrFog) ? -1 : m_pbrShadowSlot);
    if (diffSlot >= 0 && activeDiffuse) {
        BindMeshTextureAndSampler(activeDiffuse, static_cast<uint32_t>(diffSlot));
    }
    if (skySlot >= 0 && m_cubeSamplerCreated) {
        // uSkybox es sampler2D (Atlas 3x2 de 6 caras); guarda defensiva TEXTURE_2D
        const bool validAtlas2D = (envCube != nullptr) && (envCube->surface.dim == GX2_SURFACE_DIM_TEXTURE_2D);
        GX2Texture* activeCube = validAtlas2D ? envCube : (m_defaultTexCreated ? &m_defaultNeutralTex : nullptr);
        if (activeCube) {
            const uint32_t slot = static_cast<uint32_t>(skySlot);
            GX2SetPixelTexture(activeCube, slot);
            GX2SetPixelSampler(&m_cubeSampler, slot);
        }
    }
    if (shdSlot >= 0) {
        const uint32_t slot = static_cast<uint32_t>(shdSlot);
        if (shadowActive) {
            ShadowMapRendererGX2::Instance().BindShadowMapRaw(slot, slot);
        } else if (m_defaultTexCreated) {
            BindMeshTextureAndSampler(&m_defaultNeutralTex, slot);
        }
    }

    RHIPipelineCache::Instance().BindState(
        RHIPipelineState(material.depthTest, material.depthWrite, RHI_COMPARE_LEQUAL, material.blendMode, false));

    const GX2IndexType idxType = (mesh.GetHeader().indexFormat == 1u) ? GX2_INDEX_TYPE_U32 : GX2_INDEX_TYPE_U16;
    GX2RSetAttributeBuffer(const_cast<GX2RBuffer*>(mesh.GetVBO()), 0, mesh.GetHeader().vertexStride, 0);
    GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, const_cast<GX2RBuffer*>(mesh.GetIBO()),
                    idxType, mesh.GetIndexCount(), 0, 0, 1);
    m_drawCallsCount += 1u;
    m_trianglesCount += mesh.GetIndexCount() / 3u;
#else
    (void)mesh; (void)modelMatrix; (void)material; (void)shadingMode;
#endif
}

void ForwardRendererGX2::RenderStaticMeshInstanced(const StaticMesh& mesh,
                                                   const Matrix4x4* modelMatrices,
                                                   uint32_t instanceCount,
                                                   GX2Texture* diffuseTexture,
                                                   GX2Texture* envCubeMap,
                                                   const MaterialPropertyBlock* material,
                                                   bool usePbrShading) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!mesh.IsLoaded() || !modelMatrices || instanceCount == 0 || !mesh.GetVBO() || !mesh.GetIBO()) return;

    const bool pbrPath = usePbrShading && m_pbrInstShaderLoaded;
    if (!m_instShaderLoaded || !m_instanceBufferCreated || (usePbrShading && !m_pbrInstShaderLoaded)) {
        for (uint32_t i = 0; i < instanceCount; ++i) {
            if (material) {
                if (usePbrShading) {
                    RenderStaticMeshPBR(mesh, modelMatrices[i], *material, 0u);
                } else {
                    RenderStaticMeshWithMaterial(mesh, modelMatrices[i], *material);
                }
            } else {
                RenderStaticMesh(mesh, modelMatrices[i], diffuseTexture, envCubeMap);
            }
        }
        return;
    }

    const LightBlockUBO& lights = MaterialRegistry::Instance().GetActiveLightBlock();

    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    m_boundShaderKind = BoundShaderKind::None;
    m_boundPbrVariantIndex = 0xFFu;
    GX2SetFetchShader(&m_instFs);
    GX2SetVertexShader(pbrPath ? m_pbrInstVs : m_instVs);
    GX2SetPixelShader(pbrPath ? m_pbrInstPs : m_instPs);

    alignas(256) float vpUniform[16];
    std::memcpy(vpUniform, m_vpMatrix.m, sizeof(vpUniform));
    GX2SetVertexUniformReg(0, 16, vpUniform);

    const GX2Texture* activeDiffuseTex = diffuseTexture ? diffuseTexture : (m_defaultTexCreated ? &m_defaultNeutralTex : nullptr);
    if (pbrPath) {
        alignas(256) float psData[24]{};
        psData[0] = m_camPos.x; psData[1] = m_camPos.y; psData[2] = m_camPos.z; psData[3] = 0.0f;
        psData[4] = lights.dirLightDir[0]; psData[5] = lights.dirLightDir[1]; psData[6] = lights.dirLightDir[2]; psData[7] = m_shaderTime;
        psData[8] = lights.dirLightColor[0]; psData[9] = lights.dirLightColor[1]; psData[10] = lights.dirLightColor[2]; psData[11] = lights.dirLightColor[3];
        psData[12] = material ? material->ubo.albedoTint[0] : 1.0f;
        psData[13] = material ? material->ubo.albedoTint[1] : 1.0f;
        psData[14] = material ? material->ubo.albedoTint[2] : 1.0f;
        psData[15] = (material && material->blendMode == BlendMode::AlphaCutout) ? 1.0f : 0.0f;
        psData[16] = material ? material->ubo.materialParams[0] : 0.0f;
        psData[17] = 0.0f; // normalMap OFF en pbr_inst_dir_gx2
        psData[18] = material ? material->ubo.materialParams[2] : 2.0f;
        psData[19] = material ? material->ubo.materialParams[3] : 0.38f;
        psData[20] = material ? material->ubo.pbrParams[0] : 0.5f;
        psData[21] = material ? material->ubo.pbrParams[1] : 0.0f;
        psData[22] = 0.0f;
        psData[23] = 0.0f;
        GX2SetPixelUniformReg(0, 24, psData);

        if (m_pbrInstDiffuseSlot >= 0 && activeDiffuseTex) {
            BindMeshTextureAndSampler(activeDiffuseTex, static_cast<uint32_t>(m_pbrInstDiffuseSlot));
        }
        if (m_pbrInstSkyboxSlot >= 0 && m_cubeSamplerCreated) {
            const bool validAtlas2D = (envCubeMap != nullptr) && (envCubeMap->surface.dim == GX2_SURFACE_DIM_TEXTURE_2D);
            GX2Texture* activeCube = validAtlas2D ? envCubeMap : (m_defaultTexCreated ? &m_defaultNeutralTex : nullptr);
            if (activeCube) {
                const uint32_t slot = static_cast<uint32_t>(m_pbrInstSkyboxSlot);
                GX2SetPixelTexture(activeCube, slot);
                GX2SetPixelSampler(&m_cubeSampler, slot);
            }
        }
    } else {
        alignas(256) float psParams[16];
        psParams[0] = m_camPos.x; psParams[1] = m_camPos.y; psParams[2] = m_camPos.z; psParams[3] = 0.0f;
        psParams[4] = lights.dirLightDir[0]; psParams[5] = lights.dirLightDir[1]; psParams[6] = lights.dirLightDir[2]; psParams[7] = lights.dirLightDir[3];
        psParams[8] = lights.dirLightColor[0]; psParams[9] = lights.dirLightColor[1]; psParams[10] = lights.dirLightColor[2]; psParams[11] = lights.dirLightColor[3];
        psParams[12] = (envCubeMap && m_cubeSamplerCreated) ? (material ? material->ubo.materialParams[0] : 0.0f) : 0.0f;
        psParams[13] = material ? material->ubo.materialParams[1] : 1.0f;
        psParams[14] = material ? material->ubo.materialParams[2] : 2.0f;
        psParams[15] = material ? material->ubo.materialParams[3] : 0.38f;
        GX2SetPixelUniformReg(0, 16, psParams);
        BindMeshTextureAndSampler(activeDiffuseTex, 0u);
    }

    RHIPipelineCache::Instance().BindState(RHIPipelineState(
        material ? material->depthTest : true,
        material ? material->depthWrite : true,
        RHI_COMPARE_LEQUAL,
        material ? material->blendMode : BlendMode::Opaque,
        material ? material->backfaceCull : false));

    const GX2IndexType idxType = (mesh.GetHeader().indexFormat == 1) ? GX2_INDEX_TYPE_U32 : GX2_INDEX_TYPE_U16;
    GX2RSetAttributeBuffer(const_cast<GX2RBuffer*>(mesh.GetVBO()), 0, mesh.GetHeader().vertexStride, 0);

    // Multi-Frame Ring Buffer lock-free: 1 memcpy + GX2Invalidate por llamada (0 stalls GX2R por bucket LOD)
    const uint32_t sliceBase = m_instanceFrameSlice * MAX_INSTANCES_PER_FRAME;
    const uint32_t availInSlice = (m_instanceWriteCursor < MAX_INSTANCES_PER_FRAME)
        ? (MAX_INSTANCES_PER_FRAME - m_instanceWriteCursor)
        : 0u;
    const uint32_t hwCount = std::min(instanceCount, availInSlice);
    uint32_t hwRendered = 0u;

    if (hwCount > 0u) {
        const uint32_t startSlot = sliceBase + m_instanceWriteCursor;
        const uint32_t totalByteOffset = startSlot * static_cast<uint32_t>(sizeof(Matrix4x4));
        const uint32_t totalByteSize = hwCount * static_cast<uint32_t>(sizeof(Matrix4x4));
        const uint32_t ringTotalBytes = INSTANCE_RING_CAPACITY * static_cast<uint32_t>(sizeof(Matrix4x4));

        // Escritura directa lock-free sobre el Ring Buffer de 3 frames (rangos disjuntos por frame y por bucket LOD):
        // Evita el deadlock de Cafe OS GX2RLockBufferEx cuando múltiples buckets LOD comparten m_instanceMatrixBuffer
        // dentro del mismo frame antes de GX2Flush().
        uint8_t* basePtr = static_cast<uint8_t*>(m_instanceMatrixBuffer.buffer);
        if (basePtr) {
            std::memcpy(basePtr + totalByteOffset, modelMatrices, totalByteSize);
            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, basePtr + totalByteOffset, totalByteSize);

            uint32_t offset = 0u;
            while (offset < hwCount) {
                const uint32_t batchCount = std::min(hwCount - offset, MAX_INSTANCES_PER_DRAW);
                const uint32_t batchByteOffset = (startSlot + offset) * static_cast<uint32_t>(sizeof(Matrix4x4));
                GX2SetAttribBuffer(1, ringTotalBytes - batchByteOffset, sizeof(Matrix4x4), basePtr + batchByteOffset);
                GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES,
                                const_cast<GX2RBuffer*>(mesh.GetIBO()),
                                idxType, mesh.GetIndexCount(), 0, 0, batchCount);
                m_drawCallsCount += 1u;
                m_instancedDrawCalls += 1u;
                m_instancedCount += batchCount;
                m_trianglesCount += (mesh.GetIndexCount() / 3u) * batchCount;
                offset += batchCount;
            }
            m_instanceWriteCursor += hwCount;
            hwRendered = hwCount;
        }
    }

    // Si el slice de 1024 instancias del frame se llena (o basePtr es nulo), delegar el excedente al draw estándar seguro
    for (uint32_t rem = hwRendered; rem < instanceCount; ++rem) {
        if (material) {
            RenderStaticMeshWithMaterial(mesh, modelMatrices[rem], *material);
        } else {
            RenderStaticMesh(mesh, modelMatrices[rem], diffuseTexture, envCubeMap);
        }
    }
#else
    (void)mesh; (void)modelMatrices; (void)instanceCount;
    (void)diffuseTexture; (void)envCubeMap; (void)material;
#endif
}

void ForwardRendererGX2::RenderSkinnedMeshInstanced(const StaticMesh& skinnedMesh,
                                                    const Matrix4x4* modelMatrices,
                                                    const Matrix4x4* const* bonePalettes,
                                                    uint32_t boneCount,
                                                    uint32_t instanceCount,
                                                    GX2Texture* diffuseTexture,
                                                    GX2Texture* envCubeMap,
                                                    float reflectionIntensity,
                                                    bool clayMode) {
#if defined(__WIIU__) || defined(__wiiu__)
    (void)envCubeMap;
    if (!m_skelShaderLoaded || !skinnedMesh.IsLoaded() || !skinnedMesh.GetVBO() || !skinnedMesh.GetIBO() ||
        skinnedMesh.GetHeader().vertexStride < SKINNED_VERTEX_STRIDE ||
        !modelMatrices || !bonePalettes || boneCount == 0 || instanceCount == 0) {
        return;
    }

    // Enlace UNICO de pipeline, shaders, texturas, samplers, PSO y VBO fuera del bucle (0 binds redundantes)
    BindSkinnedPipeline(diffuseTexture, reflectionIntensity, clayMode);

    const GX2IndexType idxType = (skinnedMesh.GetHeader().indexFormat == 1) ? GX2_INDEX_TYPE_U32 : GX2_INDEX_TYPE_U16;
    GX2RSetAttributeBuffer(const_cast<GX2RBuffer*>(skinnedMesh.GetVBO()), 0, skinnedMesh.GetHeader().vertexStride, 0);

    for (uint32_t i = 0; i < instanceCount; ++i) {
        if (!bonePalettes[i]) continue;

        UploadSkinnedVertexUniforms(m_skelMvpRegOffset, m_skelModelRegOffset, m_skelBonesRegOffset,
                                    m_vpMatrix, modelMatrices[i], bonePalettes[i], boneCount);

        GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES,
                        const_cast<GX2RBuffer*>(skinnedMesh.GetIBO()),
                        idxType, skinnedMesh.GetIndexCount(), 0, 0, 1);
        m_drawCallsCount += 1;
        m_trianglesCount += skinnedMesh.GetIndexCount() / 3;
    }
#else
    (void)skinnedMesh; (void)modelMatrices; (void)bonePalettes; (void)boneCount;
    (void)instanceCount; (void)diffuseTexture; (void)envCubeMap; (void)reflectionIntensity; (void)clayMode;
#endif
}

void ForwardRendererGX2::BeginDepthPrePass() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_depthPreShaderLoaded || !m_depthPrePs) return;
    m_inDepthPrePass = true;
    // Modo Z-only con Color Writes OFF + Dummy PS (evita crash 0xc0000005 de pixel shader nulo en Cemu 2.6)
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, FALSE);
    GX2SetTargetChannelMasks(static_cast<GX2ChannelMask>(0), static_cast<GX2ChannelMask>(0),
                             static_cast<GX2ChannelMask>(0), static_cast<GX2ChannelMask>(0),
                             static_cast<GX2ChannelMask>(0), static_cast<GX2ChannelMask>(0),
                             static_cast<GX2ChannelMask>(0), static_cast<GX2ChannelMask>(0));
    GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
    GX2SetDepthOnlyControl(TRUE, TRUE, GX2_COMPARE_FUNC_LEQUAL);
    m_boundShaderKind = BoundShaderKind::None;
    m_boundPbrVariantIndex = 0xFFu;
    GX2SetFetchShader(&m_depthPreFs);
    GX2SetVertexShader(m_depthPreVs);
    GX2SetPixelShader(m_depthPrePs);
#endif
}

void ForwardRendererGX2::RenderDepthPrePassBuffers(GX2RBuffer& vbo,
                                                   GX2RBuffer& ibo,
                                                   uint32_t indexCount,
                                                   uint32_t vertexStride,
                                                   const Matrix4x4& modelMatrix,
                                                   bool use32BitIndices) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_inDepthPrePass || !m_depthPreShaderLoaded || indexCount == 0) return;

    const Matrix4x4 mvp = m_vpMatrix * modelMatrix;
    alignas(256) float ubo[16];
    std::memcpy(ubo, mvp.m, sizeof(ubo));
    GX2SetVertexUniformReg(0, 16, ubo);

    GX2RSetAttributeBuffer(&vbo, 0, vertexStride, 0);
    GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &ibo,
                    use32BitIndices ? GX2_INDEX_TYPE_U32 : GX2_INDEX_TYPE_U16,
                    indexCount, 0, 0, 1);
    m_drawCallsCount += 1;
    m_trianglesCount += indexCount / 3;
#else
    (void)vbo; (void)ibo; (void)indexCount; (void)vertexStride; (void)modelMatrix; (void)use32BitIndices;
#endif
}

void ForwardRendererGX2::EndDepthPrePass() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_inDepthPrePass) return;
    m_inDepthPrePass = false;
    GX2SetTargetChannelMasks(GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
                             GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
                             GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
                             GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA);
    // 0x00 = sin blending hardware en RT0..RT7 para pases opacos (RHIPipelineCache habilita 0x01 solo en AlphaBlend/Additive)
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, TRUE);
    RHIPipelineCache::Instance().Invalidate();
#endif
}

void ForwardRendererGX2::SetTextureFilterMode(TextureFilterMode mode) {
    m_activeFilterMode = mode;
    m_activeMipFilterMode = (mode == TextureFilterMode::Point)
        ? TextureMipFilterMode::Point
        : TextureMipFilterMode::Linear;
    if (!m_initialized) return;
    ApplyMeshSamplerState();
}

void ForwardRendererGX2::SetTextureWrapMode(TextureWrapMode mode) {
    m_activeWrapMode = mode;
    if (!m_initialized) return;
    ApplyMeshSamplerState();
}

void ForwardRendererGX2::SetTextureMipFilter(TextureMipFilterMode mode) {
    m_activeMipFilterMode = mode;
    if (!m_initialized) return;
    ApplyMeshSamplerState();
}

void ForwardRendererGX2::ResetMeshSamplerDefaults() {
    m_activeWrapMode = TextureWrapMode::Wrap;
    m_activeFilterMode = EngineConfig::Instance().graphics.textureFilter;
    m_activeMipFilterMode = (m_activeFilterMode == TextureFilterMode::Point)
        ? TextureMipFilterMode::Point
        : TextureMipFilterMode::Linear;
    if (!m_initialized) return;
    ApplyMeshSamplerState();
}

void ForwardRendererGX2::ApplyTextureFilterMode(TextureFilterMode mode) {
    m_activeFilterMode = mode;
    m_activeMipFilterMode = (mode == TextureFilterMode::Point)
        ? TextureMipFilterMode::Point
        : TextureMipFilterMode::Linear;
    ApplyMeshSamplerState();
}

void ForwardRendererGX2::ApplyMeshSamplerState() {
#if defined(__WIIU__) || defined(__wiiu__)
    GX2TexXYFilterMode magFilter = GX2_TEX_XY_FILTER_MODE_LINEAR;
    GX2TexXYFilterMode minFilter = GX2_TEX_XY_FILTER_MODE_LINEAR;
    GX2TexAnisoRatio anisoRatio = GX2_TEX_ANISO_RATIO_NONE;

    switch (m_activeFilterMode) {
        case TextureFilterMode::Point:
            magFilter = GX2_TEX_XY_FILTER_MODE_POINT;
            minFilter = GX2_TEX_XY_FILTER_MODE_POINT;
            anisoRatio = GX2_TEX_ANISO_RATIO_NONE;
            break;
        case TextureFilterMode::Bilinear:
            magFilter = GX2_TEX_XY_FILTER_MODE_LINEAR;
            minFilter = GX2_TEX_XY_FILTER_MODE_LINEAR;
            anisoRatio = GX2_TEX_ANISO_RATIO_NONE;
            break;
        case TextureFilterMode::Aniso2x:
            magFilter = GX2_TEX_XY_FILTER_MODE_LINEAR;
            minFilter = GX2_TEX_XY_FILTER_MODE_LINEAR;
            anisoRatio = GX2_TEX_ANISO_RATIO_2_TO_1;
            break;
        case TextureFilterMode::Aniso4x:
            magFilter = GX2_TEX_XY_FILTER_MODE_LINEAR;
            minFilter = GX2_TEX_XY_FILTER_MODE_LINEAR;
            anisoRatio = GX2_TEX_ANISO_RATIO_4_TO_1;
            break;
        case TextureFilterMode::Aniso8x:
            magFilter = GX2_TEX_XY_FILTER_MODE_LINEAR;
            minFilter = GX2_TEX_XY_FILTER_MODE_LINEAR;
            anisoRatio = GX2_TEX_ANISO_RATIO_8_TO_1;
            break;
        case TextureFilterMode::Aniso16x:
            magFilter = GX2_TEX_XY_FILTER_MODE_LINEAR;
            minFilter = GX2_TEX_XY_FILTER_MODE_LINEAR;
            anisoRatio = GX2_TEX_ANISO_RATIO_16_TO_1;
            break;
    }

    GX2TexClampMode clampMode = GX2_TEX_CLAMP_MODE_WRAP;
    switch (m_activeWrapMode) {
        case TextureWrapMode::Wrap:
            clampMode = GX2_TEX_CLAMP_MODE_WRAP;
            break;
        case TextureWrapMode::Clamp:
            clampMode = GX2_TEX_CLAMP_MODE_CLAMP;
            break;
        case TextureWrapMode::Mirror:
            clampMode = GX2_TEX_CLAMP_MODE_MIRROR;
            break;
    }

    GX2TexMipFilterMode mipFilter = GX2_TEX_MIP_FILTER_MODE_LINEAR;
    float maxLod = 12.0f;
    switch (m_activeMipFilterMode) {
        case TextureMipFilterMode::None:
            mipFilter = GX2_TEX_MIP_FILTER_MODE_NONE;
            maxLod = 0.0f;
            break;
        case TextureMipFilterMode::Point:
            mipFilter = GX2_TEX_MIP_FILTER_MODE_POINT;
            maxLod = 12.0f;
            break;
        case TextureMipFilterMode::Linear:
            mipFilter = GX2_TEX_MIP_FILTER_MODE_LINEAR;
            maxLod = 12.0f;
            break;
    }

    GX2InitSamplerClamping(&m_meshSampler, clampMode, clampMode, GX2_TEX_CLAMP_MODE_WRAP);
    GX2InitSamplerXYFilter(&m_meshSampler, magFilter, minFilter, anisoRatio);
    GX2InitSamplerZMFilter(&m_meshSampler, GX2_TEX_Z_FILTER_MODE_NONE, mipFilter);
    GX2InitSamplerLOD(&m_meshSampler, 0.0f, maxLod, 0.0f);

    // Sampler de seguridad para texturas de 1 solo nivel (mipLevels == 1 / mipmaps == nullptr):
    // En AMD Latte R700, pedir MIP_FILTER_LINEAR o maxLod > 0 con MIP_ADDRESS == NULL cuelga la unidad TA/TD.
    GX2InitSamplerClamping(&m_meshNoMipSampler, clampMode, clampMode, GX2_TEX_CLAMP_MODE_WRAP);
    GX2InitSamplerXYFilter(&m_meshNoMipSampler, magFilter, minFilter, GX2_TEX_ANISO_RATIO_NONE);
    GX2InitSamplerZMFilter(&m_meshNoMipSampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_NONE);
    GX2InitSamplerFilterAdjust(&m_meshNoMipSampler, FALSE, GX2_TEX_MIP_PERF_MODE_DISABLE, GX2_TEX_Z_PERF_MODE_DISABLED);
    GX2InitSamplerLOD(&m_meshNoMipSampler, 0.0f, 0.0f, 0.0f);
#endif
}

} // namespace Neleo
