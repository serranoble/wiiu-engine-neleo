#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/Mesh/MeshBinaryFormat.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/AssetManager.hpp"
#include "Graphics/SkyboxShaderBinary.hpp"
#include "Graphics/AdvancedMeshShaderBinary.hpp"
#include "Graphics/AdvancedSkelShaderBinary.hpp"
#include <cstring>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/draw.h>
#include <gx2/registers.h>
#include <gx2/mem.h>
#include <gx2/state.h>
#include <gx2/clear.h>
#include <gx2/enum.h>
#include <gx2/utils.h>
#include <gfd.h>
#include <coreinit/thread.h>
#endif

namespace WiiU {

ForwardRendererGX2& ForwardRendererGX2::Instance() noexcept {
    static ForwardRendererGX2 s_instance;
    return s_instance;
}

bool ForwardRendererGX2::Initialize() {
    if (m_initialized) return true;

#if defined(__WIIU__) || defined(__wiiu__)
    if (!InitSkyShader()) return false;
    if (!InitMeshShader()) return false;
    if (!InitSkinningShader()) return false;

    // Inicializar Samplers GX2
    GX2InitSampler(&m_skySampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerXYFilter(&m_skySampler, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_ANISO_RATIO_NONE);
    GX2InitSamplerZMFilter(&m_skySampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_LINEAR);

    GX2InitSampler(&m_meshSampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerXYFilter(&m_meshSampler, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_ANISO_RATIO_4_TO_1);
    GX2InitSamplerZMFilter(&m_meshSampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_LINEAR);
    GX2InitSamplerLOD(&m_meshSampler, 0.0f, 10.0f, 0.0f);

    GX2InitSampler(&m_cubeSampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerXYFilter(&m_cubeSampler, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_ANISO_RATIO_NONE);
    GX2InitSamplerZMFilter(&m_cubeSampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_LINEAR);
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
#endif

    m_initialized = true;
    return true;
}

void ForwardRendererGX2::Shutdown() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_skyShaderLoaded) {
        if (m_skyVs) { void* raw = static_cast<void*>(m_skyVs); MemoryManager::Instance().Free(raw); m_skyVs = nullptr; }
        if (m_skyVsProgram) { MemoryManager::Instance().FreeAligned(m_skyVsProgram); m_skyVsProgram = nullptr; }
        if (m_skyPs) { void* raw = static_cast<void*>(m_skyPs); MemoryManager::Instance().Free(raw); m_skyPs = nullptr; }
        if (m_skyPsProgram) { MemoryManager::Instance().FreeAligned(m_skyPsProgram); m_skyPsProgram = nullptr; }
        if (m_skyFsProgram) { MemoryManager::Instance().FreeAligned(m_skyFsProgram); m_skyFsProgram = nullptr; }
        m_skyShaderLoaded = false;
    }

    if (m_meshShaderLoaded) {
        if (m_meshVs) { void* raw = static_cast<void*>(m_meshVs); MemoryManager::Instance().Free(raw); m_meshVs = nullptr; }
        if (m_meshVsProgram) { MemoryManager::Instance().FreeAligned(m_meshVsProgram); m_meshVsProgram = nullptr; }
        if (m_meshPs) { void* raw = static_cast<void*>(m_meshPs); MemoryManager::Instance().Free(raw); m_meshPs = nullptr; }
        if (m_meshPsProgram) { MemoryManager::Instance().FreeAligned(m_meshPsProgram); m_meshPsProgram = nullptr; }
        if (m_meshFsProgram) { MemoryManager::Instance().FreeAligned(m_meshFsProgram); m_meshFsProgram = nullptr; }
        m_meshShaderLoaded = false;
    }

    if (m_skelShaderLoaded) {
        if (m_skelVs) { void* raw = static_cast<void*>(m_skelVs); MemoryManager::Instance().Free(raw); m_skelVs = nullptr; }
        if (m_skelVsProgram) { MemoryManager::Instance().FreeAligned(m_skelVsProgram); m_skelVsProgram = nullptr; }
        if (m_skelPs) { void* raw = static_cast<void*>(m_skelPs); MemoryManager::Instance().Free(raw); m_skelPs = nullptr; }
        if (m_skelPsProgram) { MemoryManager::Instance().FreeAligned(m_skelPsProgram); m_skelPsProgram = nullptr; }
        if (m_skelFsProgram) { MemoryManager::Instance().FreeAligned(m_skelFsProgram); m_skelFsProgram = nullptr; }
        m_skelShaderLoaded = false;
    }
#endif
    m_initialized = false;
}

void ForwardRendererGX2::BeginFrame(const Point3D& camPos, const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) {
    m_camPos = camPos;
    m_viewMatrix = viewMatrix;
    m_projMatrix = projMatrix;
    m_vpMatrix = projMatrix * viewMatrix;
    m_drawCallsCount = 0;
    m_trianglesCount = 0;
}

void ForwardRendererGX2::RenderSkydome(StaticMesh& skydomeMesh, GX2Texture* skyTexture) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!skydomeMesh.IsLoaded() || !skyTexture || !m_skyShaderLoaded) return;

    GX2SetFetchShader(&m_skyFs);
    GX2SetVertexShader(m_skyVs);
    GX2SetPixelShader(m_skyPs);

    // Cancelar traslación de la cámara para que el domo permanezca en el infinito óptico
    Matrix4x4 skyView = m_viewMatrix;
    skyView.m[12] = 0.0f;
    skyView.m[13] = 0.0f;
    skyView.m[14] = 0.0f;
    Matrix4x4 skyVP = m_projMatrix * skyView;

    alignas(256) float skyUniforms[16];
    std::memcpy(skyUniforms, skyVP.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 16, skyUniforms);

    GX2SetPixelTexture(skyTexture, 0);
    GX2SetPixelSampler(&m_skySampler, 0);

    // Depth Test ON, Depth Write OFF (Z=W=1.0)
    RHIPipelineCache::Instance().BindState(RHIPipelineState(true, false, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false));

    if (skydomeMesh.GetVBO() && skydomeMesh.GetIBO()) {
        GX2RSetAttributeBuffer(const_cast<GX2RBuffer*>(skydomeMesh.GetVBO()), 0, skydomeMesh.GetHeader().vertexStride, 0);
        GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, const_cast<GX2RBuffer*>(skydomeMesh.GetIBO()), 
                        GX2_INDEX_TYPE_U16, skydomeMesh.GetIndexCount(), 0, 0, 1);
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

    GX2SetFetchShader(&m_meshFs);
    GX2SetVertexShader(m_meshVs);
    GX2SetPixelShader(m_meshPs);

    Matrix4x4 mvp = m_vpMatrix * modelMatrix;
    alignas(256) float vsUniforms[32];
    std::memcpy(&vsUniforms[0], mvp.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], modelMatrix.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 32, vsUniforms);

    alignas(256) float psParams[16];
    psParams[0] = m_camPos.x; psParams[1] = m_camPos.y; psParams[2] = m_camPos.z; psParams[3] = 0.0f; // uCamPosTime
    psParams[4] = 0.60f; psParams[5] = 0.55f; psParams[6] = 0.58f; psParams[7] = 1.0f;              // uLightDir
    psParams[8] = 1.0f; psParams[9] = 0.96f; psParams[10] = 0.88f; psParams[11] = 32.0f;           // uLightColor + shininess
    psParams[12] = reflectionIntensity;                                                              // uMaterialParams.x
    psParams[13] = normalMapEnabled ? 1.0f : 0.0f;                                                   // uMaterialParams.y
    psParams[14] = bumpScale;                                                                        // uMaterialParams.z
    psParams[15] = ambientIntensity;                                                                 // uMaterialParams.w
    GX2SetPixelUniformReg(0, 16, psParams);

    const GX2Texture* activeDiffuseTex = diffuseTexture ? diffuseTexture : (m_defaultTexCreated ? &m_defaultNeutralTex : nullptr);
    if (activeDiffuseTex) {
        GX2SetPixelTexture(const_cast<GX2Texture*>(activeDiffuseTex), 0);
        GX2SetPixelSampler(&m_meshSampler, 0);
    }
    if (envCubeMap && m_cubeSamplerCreated) {
        GX2SetPixelTexture(envCubeMap, 1);
        GX2SetPixelSampler(&m_cubeSampler, 1);
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
                                          float ambientIntensity) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_meshShaderLoaded || indexCount == 0) return;

    GX2SetFetchShader(&m_meshFs);
    GX2SetVertexShader(m_meshVs);
    GX2SetPixelShader(m_meshPs);

    Matrix4x4 mvp = m_vpMatrix * modelMatrix;
    alignas(256) float vsUniforms[32];
    std::memcpy(&vsUniforms[0], mvp.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], modelMatrix.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 32, vsUniforms);

    alignas(256) float psParams[16];
    psParams[0] = m_camPos.x; psParams[1] = m_camPos.y; psParams[2] = m_camPos.z; psParams[3] = 0.0f;
    psParams[4] = 0.60f; psParams[5] = 0.55f; psParams[6] = 0.58f; psParams[7] = 1.0f;
    psParams[8] = 1.0f; psParams[9] = 0.96f; psParams[10] = 0.88f; psParams[11] = 32.0f;
    psParams[12] = reflectionIntensity;
    psParams[13] = normalMapEnabled ? 1.0f : 0.0f;
    psParams[14] = bumpScale;
    psParams[15] = ambientIntensity;
    GX2SetPixelUniformReg(0, 16, psParams);

    if (diffuseTexture) {
        GX2SetPixelTexture(diffuseTexture, 0);
        GX2SetPixelSampler(&m_meshSampler, 0);
    }
    if (envCubeMap && m_cubeSamplerCreated) {
        GX2SetPixelTexture(envCubeMap, 1);
        GX2SetPixelSampler(&m_cubeSampler, 1);
    }

    RHIPipelineCache::Instance().BindState(RHIPipelineState(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false));

    GX2RSetAttributeBuffer(&vbo, 0, vertexStride, 0);
    GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &ibo, GX2_INDEX_TYPE_U16, indexCount, 0, 0, 1);
    m_drawCallsCount += 1;
    m_trianglesCount += indexCount / 3;
#else
    (void)vbo; (void)ibo; (void)indexCount; (void)vertexStride; (void)modelMatrix;
    (void)diffuseTexture; (void)envCubeMap; (void)reflectionIntensity;
    (void)normalMapEnabled; (void)bumpScale; (void)ambientIntensity;
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
    if (!m_skelShaderLoaded || boneCount == 0 || !boneMatrices) return;

    GX2SetFetchShader(&m_skelFs);
    GX2SetVertexShader(m_skelVs);
    GX2SetPixelShader(m_skelPs);

    Matrix4x4 mvp = m_vpMatrix * modelMatrix;
    constexpr uint32_t TOTAL_VS_UNIFORMS = 32 + 24 * 16;
    alignas(256) float skelVsUniforms[TOTAL_VS_UNIFORMS];
    std::memcpy(&skelVsUniforms[0], mvp.m, 16 * sizeof(float));
    std::memcpy(&skelVsUniforms[16], modelMatrix.m, 16 * sizeof(float));

    uint32_t clampedBones = boneCount > 24u ? 24u : boneCount;
    for (uint32_t b = 0; b < clampedBones; ++b) {
        std::memcpy(&skelVsUniforms[32 + b * 16], boneMatrices[b].m, 16 * sizeof(float));
    }
    for (uint32_t b = clampedBones; b < 24u; ++b) {
        std::memcpy(&skelVsUniforms[32 + b * 16], Matrix4x4::Identity().m, 16 * sizeof(float));
    }
    GX2SetVertexUniformReg(0, TOTAL_VS_UNIFORMS, skelVsUniforms);

    alignas(256) float psParams[16];
    psParams[0] = m_camPos.x; psParams[1] = m_camPos.y; psParams[2] = m_camPos.z; psParams[3] = 0.0f;
    psParams[4] = 0.60f; psParams[5] = 0.55f; psParams[6] = 0.58f; psParams[7] = 1.0f;
    psParams[8] = 1.0f; psParams[9] = 0.96f; psParams[10] = 0.88f; psParams[11] = 32.0f;
    psParams[12] = reflectionIntensity;
    psParams[13] = clayMode ? 1.0f : 0.0f;
    psParams[14] = 2.0f;
    psParams[15] = 0.38f;
    GX2SetPixelUniformReg(0, 16, psParams);

    GX2Texture* activeDiffuse = diffuseTexture ? diffuseTexture
                              : (m_defaultTexCreated ? &m_defaultNeutralTex : nullptr);
    if (activeDiffuse) {
        GX2SetPixelTexture(activeDiffuse, 0);
        GX2SetPixelSampler(&m_meshSampler, 0);
    }
    if (envCubeMap && m_cubeSamplerCreated) {
        GX2SetPixelTexture(envCubeMap, 1);
        GX2SetPixelSampler(&m_cubeSampler, 1);
    }

    RHIPipelineCache::Instance().BindState(RHIPipelineState(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false));

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
    // Restaurar el pipeline de renderizado
}

#if defined(__WIIU__) || defined(__wiiu__)
bool ForwardRendererGX2::InitSkyShader() {
    if (m_skyShaderLoaded) return true;

    std::vector<uint8_t> shaderData;
    bool loaded = AssetManager::LoadBinaryFile("shaders/skydome_gx2.gsh", shaderData);
    if (!loaded || shaderData.empty()) {
        loaded = AssetManager::LoadBinaryFile("shaders/skybox_gx2.gsh", shaderData);
        if (!loaded || shaderData.empty()) {
            shaderData.assign(WiiU::SKYBOX_GX2_GSH_DATA, WiiU::SKYBOX_GX2_GSH_DATA + WiiU::SKYBOX_GX2_GSH_SIZE);
            loaded = true;
        }
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
    return m_skelShaderLoaded;
}
#endif

} // namespace WiiU
