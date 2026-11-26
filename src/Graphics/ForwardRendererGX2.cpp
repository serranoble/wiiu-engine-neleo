#include "Graphics/ForwardRendererGX2.hpp"
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
#include "Graphics/PBRLiteShaderBinary.hpp"
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
    (void)InitPBRLiteShader();

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
    const bool hasGpuResources =
        m_skyShaderLoaded || m_meshShaderLoaded || m_skelShaderLoaded ||
        m_instShaderLoaded || m_depthPreShaderLoaded || m_pbrShaderLoaded || m_pbrLiteShaderLoaded || m_instanceBufferCreated ||
        m_skyVs || m_meshVs || m_skelVs || m_instVs || m_depthPreVs || m_pbrVs || m_pbrLiteVs;
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
    if (m_depthPreFsProgram) { MemoryManager::Instance().FreeAligned(m_depthPreFsProgram); m_depthPreFsProgram = nullptr; }
    m_depthPreShaderLoaded = false;
    m_inDepthPrePass = false;

    freeShaderPtrs(m_pbrVs, m_pbrVsProgram, m_pbrPs, m_pbrPsProgram, m_pbrFsProgram);
    m_pbrDiffuseSlot = 0;
    m_pbrSkyboxSlot = -1;
    m_pbrShadowSlot = 1;
    m_pbrShaderLoaded = false;

    freeShaderPtrs(m_pbrLiteVs, m_pbrLiteVsProgram, m_pbrLitePs, m_pbrLitePsProgram, m_pbrLiteFsProgram);
    m_pbrLiteDiffuseSlot = 0;
    m_pbrLiteSkyboxSlot = -1;
    m_pbrLiteShaderLoaded = false;
    m_boundShaderKind = BoundShaderKind::None;
#endif
    m_initialized = false;
}

void ForwardRendererGX2::BeginFrame(const Point3D& camPos, const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) {
#if defined(__WIIU__) || defined(__wiiu__)
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    // Sincronizar caché RHI tras Window::Clear() o pases 2D externos que mutan estado GX2
    RHIPipelineCache::Instance().Invalidate();
    m_boundShaderKind = BoundShaderKind::None;
    // Rotar slice del Multi-Frame Ring Buffer de instancing (0 carreras CPU/GPU entre frames en vuelo)
    m_instanceFrameSlice = (m_instanceFrameSlice + 1u) % MAX_FRAMES_IN_FLIGHT;
    m_instanceWriteCursor = 0;
#endif
    m_camPos = camPos;
    m_viewMatrix = viewMatrix;
    m_projMatrix = projMatrix;
    m_vpMatrix = projMatrix * viewMatrix;
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

void ForwardRendererGX2::EndFrame() {
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
    if (vsHdrSize == 0 || vsProgSize == 0) {
        return false;
    }

    auto cleanupDepthPartial = [this]() {
        if (m_depthPreVs)        { void* p = m_depthPreVs; MemoryManager::Instance().Free(p); m_depthPreVs = nullptr; }
        if (m_depthPreVsProgram) { MemoryManager::Instance().FreeAligned(m_depthPreVsProgram); m_depthPreVsProgram = nullptr; }
        if (m_depthPreFsProgram) { MemoryManager::Instance().FreeAligned(m_depthPreFsProgram); m_depthPreFsProgram = nullptr; }
    };

    m_depthPreVs        = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "EarlyZ_VS_Hdr"));
    m_depthPreVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "EarlyZ_VS_Prog");

    if (!m_depthPreVs || !m_depthPreVsProgram ||
        !GFDGetVertexShader(m_depthPreVs, m_depthPreVsProgram, 0, shaderData.data())) {
        cleanupDepthPartial();
        return false;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_depthPreVsProgram, vsProgSize);

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

bool ForwardRendererGX2::InitPBRShader() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_pbrShaderLoaded) return true;

    const uint8_t* raw = PBR_MESH_GX2_GSH_DATA;
    if (!raw || PBR_MESH_GX2_GSH_SIZE == 0u) {
        return false;
    }

    const uint32_t vsHdrSize = GFDGetVertexShaderHeaderSize(0, raw);
    const uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, raw);
    const uint32_t psHdrSize = GFDGetPixelShaderHeaderSize(0, raw);
    const uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, raw);
    if (vsHdrSize == 0u || vsProgSize == 0u || psHdrSize == 0u || psProgSize == 0u) {
        return false;
    }

    m_pbrVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "PBR_VS_Hdr"));
    m_pbrVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "PBR_VS_Prog");
    m_pbrPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHdrSize, MemoryTag::ShaderProgram, "PBR_PS_Hdr"));
    m_pbrPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "PBR_PS_Prog");

    if (m_pbrVs && m_pbrVsProgram && m_pbrPs && m_pbrPsProgram &&
        GFDGetVertexShader(m_pbrVs, m_pbrVsProgram, 0, raw) &&
        GFDGetPixelShader(m_pbrPs, m_pbrPsProgram, 0, raw)) {
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_pbrVsProgram, vsProgSize);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_pbrPsProgram, psProgSize);

        GX2AttribStream attrs[4]{};
        uint32_t numAttrs = 0u;
        if (m_pbrVs->attribVarCount > 0u && m_pbrVs->attribVars != nullptr) {
            for (uint32_t i = 0; i < m_pbrVs->attribVarCount && numAttrs < 4u; ++i) {
                const auto& var = m_pbrVs->attribVars[i];
                if (!var.name) continue;
                if (std::strcmp(var.name, "aPos") == 0) {
                    attrs[numAttrs].location = var.location;
                    attrs[numAttrs].buffer = 0u;
                    attrs[numAttrs].offset = offsetof(MeshVertex, pos);
                    attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                    attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[numAttrs].aluDivisor = 0u;
                    attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                    attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                    ++numAttrs;
                } else if (std::strcmp(var.name, "aNormal") == 0) {
                    attrs[numAttrs].location = var.location;
                    attrs[numAttrs].buffer = 0u;
                    attrs[numAttrs].offset = offsetof(MeshVertex, normal);
                    attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                    attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[numAttrs].aluDivisor = 0u;
                    attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                    attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                    ++numAttrs;
                } else if (std::strcmp(var.name, "aTexCoord") == 0) {
                    attrs[numAttrs].location = var.location;
                    attrs[numAttrs].buffer = 0u;
                    attrs[numAttrs].offset = offsetof(MeshVertex, texCoord);
                    attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
                    attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[numAttrs].aluDivisor = 0u;
                    attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                    attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                    ++numAttrs;
                } else if (std::strcmp(var.name, "aColor") == 0) {
                    attrs[numAttrs].location = var.location;
                    attrs[numAttrs].buffer = 0u;
                    attrs[numAttrs].offset = offsetof(MeshVertex, color);
                    attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
                    attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[numAttrs].aluDivisor = 0u;
                    attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                    attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                    ++numAttrs;
                }
            }
        }
        if (numAttrs > 0u) {
            const uint32_t fsSize = GX2CalcFetchShaderSizeEx(numAttrs, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
            m_pbrFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "PBR_FS_Prog");
            if (m_pbrFsProgram) {
                GX2InitFetchShaderEx(&m_pbrFs, static_cast<uint8_t*>(m_pbrFsProgram), numAttrs, attrs,
                                     GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_pbrFsProgram, fsSize);

                // Reflexion dinamica de slots TMU desde samplerVars (inmune a dead-code elimination de CafeGLSL)
                m_pbrDiffuseSlot = -1;
                m_pbrSkyboxSlot  = -1;
                m_pbrShadowSlot  = -1;
                if (m_pbrPs->samplerVarCount > 0u && m_pbrPs->samplerVars != nullptr) {
                    for (uint32_t s = 0u; s < m_pbrPs->samplerVarCount; ++s) {
                        const auto& svar = m_pbrPs->samplerVars[s];
                        if (!svar.name) continue;
                        if (std::strcmp(svar.name, "uTexture") == 0) {
                            m_pbrDiffuseSlot = static_cast<int32_t>(svar.location);
                        } else if (std::strcmp(svar.name, "uSkybox") == 0) {
                            m_pbrSkyboxSlot = static_cast<int32_t>(svar.location);
                        } else if (std::strcmp(svar.name, "uShadowMap") == 0) {
                            m_pbrShadowSlot = static_cast<int32_t>(svar.location);
                        }
                    }
                }
                if (m_pbrDiffuseSlot < 0) m_pbrDiffuseSlot = 0;
                m_pbrShaderLoaded = true;
            }
        }
    }
    return m_pbrShaderLoaded;
#else
    return true;
#endif
}

bool ForwardRendererGX2::InitPBRLiteShader() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_pbrLiteShaderLoaded) return true;

    const uint8_t* raw = PBR_LITE_GX2_GSH_DATA;
    if (!raw || PBR_LITE_GX2_GSH_SIZE == 0u) {
        return false;
    }

    const uint32_t vsHdrSize = GFDGetVertexShaderHeaderSize(0, raw);
    const uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, raw);
    const uint32_t psHdrSize = GFDGetPixelShaderHeaderSize(0, raw);
    const uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, raw);
    if (vsHdrSize == 0u || vsProgSize == 0u || psHdrSize == 0u || psProgSize == 0u) {
        return false;
    }

    m_pbrLiteVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "PBRLite_VS_Hdr"));
    m_pbrLiteVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "PBRLite_VS_Prog");
    m_pbrLitePs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHdrSize, MemoryTag::ShaderProgram, "PBRLite_PS_Hdr"));
    m_pbrLitePsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "PBRLite_PS_Prog");

    if (m_pbrLiteVs && m_pbrLiteVsProgram && m_pbrLitePs && m_pbrLitePsProgram &&
        GFDGetVertexShader(m_pbrLiteVs, m_pbrLiteVsProgram, 0, raw) &&
        GFDGetPixelShader(m_pbrLitePs, m_pbrLitePsProgram, 0, raw)) {
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_pbrLiteVsProgram, vsProgSize);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_pbrLitePsProgram, psProgSize);

        GX2AttribStream attrs[4]{};
        uint32_t numAttrs = 0u;
        if (m_pbrLiteVs->attribVarCount > 0u && m_pbrLiteVs->attribVars != nullptr) {
            for (uint32_t i = 0; i < m_pbrLiteVs->attribVarCount && numAttrs < 4u; ++i) {
                const auto& var = m_pbrLiteVs->attribVars[i];
                if (!var.name) continue;
                if (std::strcmp(var.name, "aPos") == 0) {
                    attrs[numAttrs].location = var.location;
                    attrs[numAttrs].buffer = 0u;
                    attrs[numAttrs].offset = offsetof(MeshVertex, pos);
                    attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                    attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[numAttrs].aluDivisor = 0u;
                    attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                    attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                    ++numAttrs;
                } else if (std::strcmp(var.name, "aNormal") == 0) {
                    attrs[numAttrs].location = var.location;
                    attrs[numAttrs].buffer = 0u;
                    attrs[numAttrs].offset = offsetof(MeshVertex, normal);
                    attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                    attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[numAttrs].aluDivisor = 0u;
                    attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                    attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                    ++numAttrs;
                } else if (std::strcmp(var.name, "aTexCoord") == 0) {
                    attrs[numAttrs].location = var.location;
                    attrs[numAttrs].buffer = 0u;
                    attrs[numAttrs].offset = offsetof(MeshVertex, texCoord);
                    attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
                    attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[numAttrs].aluDivisor = 0u;
                    attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                    attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                    ++numAttrs;
                } else if (std::strcmp(var.name, "aColor") == 0) {
                    attrs[numAttrs].location = var.location;
                    attrs[numAttrs].buffer = 0u;
                    attrs[numAttrs].offset = offsetof(MeshVertex, color);
                    attrs[numAttrs].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
                    attrs[numAttrs].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                    attrs[numAttrs].aluDivisor = 0u;
                    attrs[numAttrs].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                    attrs[numAttrs].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
                    ++numAttrs;
                }
            }
        }
        if (numAttrs > 0u) {
            const uint32_t fsSize = GX2CalcFetchShaderSizeEx(numAttrs, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
            m_pbrLiteFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "PBRLite_FS_Prog");
            if (m_pbrLiteFsProgram) {
                GX2InitFetchShaderEx(&m_pbrLiteFs, static_cast<uint8_t*>(m_pbrLiteFsProgram), numAttrs, attrs,
                                     GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_pbrLiteFsProgram, fsSize);

                m_pbrLiteDiffuseSlot = -1;
                m_pbrLiteSkyboxSlot = -1;
                if (m_pbrLitePs->samplerVarCount > 0u && m_pbrLitePs->samplerVars != nullptr) {
                    for (uint32_t s = 0u; s < m_pbrLitePs->samplerVarCount; ++s) {
                        const auto& svar = m_pbrLitePs->samplerVars[s];
                        if (!svar.name) continue;
                        if (std::strcmp(svar.name, "uTexture") == 0) {
                            m_pbrLiteDiffuseSlot = static_cast<int32_t>(svar.location);
                        } else if (std::strcmp(svar.name, "uSkybox") == 0) {
                            m_pbrLiteSkyboxSlot = static_cast<int32_t>(svar.location);
                        }
                    }
                }
                if (m_pbrLiteDiffuseSlot < 0) m_pbrLiteDiffuseSlot = 0;
                m_pbrLiteShaderLoaded = true;
            }
        }
    }
    return m_pbrLiteShaderLoaded;
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
    const uint32_t numPoints = (lights.activePointLights > 4u) ? 4u : lights.activePointLights;
    const bool shadowActive = (m_shadowMode > 0u) && ShadowMapRendererGX2::Instance().IsReady();
    const bool useLite = (shadingMode <= 1u) && !shadowActive && m_pbrLiteShaderLoaded;

    if (useLite) {
        if (m_boundShaderKind != BoundShaderKind::PBRLite) {
            GX2SetFetchShader(&m_pbrLiteFs);
            GX2SetVertexShader(m_pbrLiteVs);
            GX2SetPixelShader(m_pbrLitePs);
            m_boundShaderKind = BoundShaderKind::PBRLite;
        }
    } else {
        if (m_boundShaderKind != BoundShaderKind::PBRUber) {
            GX2SetFetchShader(&m_pbrFs);
            GX2SetVertexShader(m_pbrVs);
            GX2SetPixelShader(m_pbrPs);
            m_boundShaderKind = BoundShaderKind::PBRUber;
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

    const bool hasExtraLights = (numPoints > 0u) || (lights.spotLightIntensity > 0.01f);
    if (useLite && !hasExtraLights) {
        // Ruta rápida direccional: pbr_lite_gx2 no lee uPsData[6..16], ahorra 44 floats (176 B) por draw
        GX2SetPixelUniformReg(0, 24, psData);
    } else {
        // [6..13] pointLights[0..3]
        for (uint32_t i = 0u; i < numPoints; ++i) {
            std::memcpy(&psData[24u + i * 4u], lights.pointLightPosRadius[i], 4u * sizeof(float));
            std::memcpy(&psData[40u + i * 4u], lights.pointLightColorIntensity[i], 4u * sizeof(float));
        }
        // [14..16] spotLight
        if (lights.spotLightIntensity > 0.01f) {
            std::memcpy(&psData[56], lights.spotLightPosRange, 4u * sizeof(float));
            std::memcpy(&psData[60], lights.spotLightDirInner, 4u * sizeof(float));
            std::memcpy(&psData[64], lights.spotLightColorOuter, 4u * sizeof(float));
        }
        if (useLite) {
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
    const int32_t diffSlot = useLite ? m_pbrLiteDiffuseSlot : m_pbrDiffuseSlot;
    const int32_t skySlot = useLite ? m_pbrLiteSkyboxSlot : m_pbrSkyboxSlot;
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
    if (!useLite && m_pbrShadowSlot >= 0) {
        const uint32_t slot = static_cast<uint32_t>(m_pbrShadowSlot);
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
                                                   const MaterialPropertyBlock* material) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!mesh.IsLoaded() || !modelMatrices || instanceCount == 0 || !mesh.GetVBO() || !mesh.GetIBO()) return;

    if (!m_instShaderLoaded || !m_instanceBufferCreated) {
        for (uint32_t i = 0; i < instanceCount; ++i) {
            if (material) {
                RenderStaticMeshWithMaterial(mesh, modelMatrices[i], *material);
            } else {
                RenderStaticMesh(mesh, modelMatrices[i], diffuseTexture, envCubeMap);
            }
        }
        return;
    }

    const LightBlockUBO& lights = MaterialRegistry::Instance().GetActiveLightBlock();

    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    m_boundShaderKind = BoundShaderKind::None;
    GX2SetFetchShader(&m_instFs);
    GX2SetVertexShader(m_instVs);
    GX2SetPixelShader(m_instPs);

    alignas(256) float vpUniform[16];
    std::memcpy(vpUniform, m_vpMatrix.m, sizeof(vpUniform));
    GX2SetVertexUniformReg(0, 16, vpUniform);

    alignas(256) float psParams[16];
    psParams[0] = m_camPos.x; psParams[1] = m_camPos.y; psParams[2] = m_camPos.z; psParams[3] = 0.0f;
    psParams[4] = lights.dirLightDir[0]; psParams[5] = lights.dirLightDir[1]; psParams[6] = lights.dirLightDir[2]; psParams[7] = lights.dirLightDir[3];
    psParams[8] = lights.dirLightColor[0]; psParams[9] = lights.dirLightColor[1]; psParams[10] = lights.dirLightColor[2]; psParams[11] = lights.dirLightColor[3];
    psParams[12] = (envCubeMap && m_cubeSamplerCreated) ? (material ? material->ubo.materialParams[0] : 0.0f) : 0.0f;
    psParams[13] = material ? material->ubo.materialParams[1] : 1.0f;
    psParams[14] = material ? material->ubo.materialParams[2] : 2.0f;
    psParams[15] = material ? material->ubo.materialParams[3] : 0.38f;
    GX2SetPixelUniformReg(0, 16, psParams);

    const GX2Texture* activeDiffuseTex = diffuseTexture ? diffuseTexture : (m_defaultTexCreated ? &m_defaultNeutralTex : nullptr);
    BindMeshTextureAndSampler(activeDiffuseTex, 0u);

    RHIPipelineCache::Instance().BindState(RHIPipelineState(
        material ? material->depthTest : true,
        material ? material->depthWrite : true,
        RHI_COMPARE_LEQUAL,
        material ? material->blendMode : BlendMode::Opaque,
        false));

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
    if (!m_depthPreShaderLoaded) return;
    m_inDepthPrePass = true;
    // Modo Z-only a doble tasa de AMD Latte: Color Writes OFF + Null Pixel Shader + AlphaTest OFF
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
    GX2SetFetchShader(&m_depthPreFs);
    GX2SetVertexShader(m_depthPreVs);
    GX2SetPixelShader(nullptr);
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
