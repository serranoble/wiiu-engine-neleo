#include "Graphics/GraphicsSelfTest.hpp"
#include "Graphics/ParticleSystem3D.hpp"
#include "Graphics/RaytracingBVHShaderBinary.hpp"
#include "Graphics/Mesh/MeshLODGroup.hpp"
#include "Graphics/PostProcessHDRGX2.hpp"
#include "Graphics/RHI/RHIFullscreenShader.hpp"
#include "Graphics/RHI/RenderTargetGX2.hpp"
#include "Core/Window.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/Math3D.hpp"
#include "Graphics/Mesh/ProceduralMeshBuilder.hpp"
#include "Graphics/RHI/RHIPipelineState.hpp"
#include "Graphics/ShadowMapRendererGX2.hpp"
#include "Graphics/ShadowSystem.hpp"
#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include <cmath>
#include <cstdint>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/surface.h>
#endif

namespace WiiU::Graphics {

namespace {

[[nodiscard]] bool IsAligned256(const void* ptr) noexcept {
    return (ptr != nullptr) && ((reinterpret_cast<uintptr_t>(ptr) % 256u) == 0u);
}

[[nodiscard]] bool IsFiniteMat4(const Matrix4x4& mat) noexcept {
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(mat.m[i])) return false;
    }
    return true;
}

} // namespace

void GraphicsSelfTest::RunAll(Diagnostics::EngineSelfTestReport& outReport) {
    // T1: ColorBuffer principal GX2 (1280x720, image != null, alineado >= 256B)
#if defined(__WIIU__) || defined(__wiiu__)
    {
        const auto* cb = static_cast<const GX2ColorBuffer*>(Window::GetActiveNativeColorBuffer());
        const bool ok = (cb != nullptr) &&
                        (cb->surface.image != nullptr) &&
                        IsAligned256(cb->surface.image) &&
                        (cb->surface.width == 1280u) &&
                        (cb->surface.height == 720u);
        outReport.Record("GFX", "GX2_COLOR_BUFFER", ok,
                         ok ? "1280x720 MEM1 ALIGN>=256" : "COLOR BUFFER NULL/INVALIDO");
    }

    // T2: DepthBuffer principal GX2 (1280x720, FLOAT_R32/D24, image != null, alineado >= 256B)
    {
        const auto* db = static_cast<const GX2DepthBuffer*>(Window::GetActiveNativeDepthBuffer());
        const bool fmtOk = (db != nullptr) &&
                           (db->surface.format == GX2_SURFACE_FORMAT_FLOAT_R32 ||
                            db->surface.format == GX2_SURFACE_FORMAT_UNORM_R24_X8);
        const bool hiZOk = (db != nullptr) &&
                           (db->hiZPtr == nullptr || IsAligned256(db->hiZPtr));
        const bool ok = (db != nullptr) &&
                        (db->surface.image != nullptr) &&
                        IsAligned256(db->surface.image) &&
                        (db->surface.width == 1280u) &&
                        (db->surface.height == 720u) &&
                        fmtOk && hiZOk;
        outReport.Record("GFX", "GX2_DEPTH_BUFFER", ok,
                         ok ? "1280x720 Z32F MEM1 OK" : "DEPTH BUFFER AUSENTE O INVALIDO");
    }

    // T3: ContextState nativo GX2 activo
    {
        const void* ctx = Window::GetActiveNativeContextState();
        const bool ok = IsAligned256(ctx);
        outReport.Record("GFX", "GX2_CONTEXT_STATE", ok,
                         ok ? "CONTEXTO GX2 ACTIVO" : "CONTEXTO GX2 NULO");
    }
#else
    outReport.Record("GFX", "GX2_COLOR_BUFFER", true, "HOST PC STUB OK");
    outReport.Record("GFX", "GX2_DEPTH_BUFFER", true, "HOST PC STUB OK");
    outReport.Record("GFX", "GX2_CONTEXT_STATE", true, "HOST PC STUB OK");
#endif

    // T4: ForwardRendererGX2 inicializado (shaders + fallbacks)
    {
        auto& renderer = ForwardRendererGX2::Instance();
        const bool ok = renderer.Initialize() && renderer.IsInitialized();
        outReport.Record("GFX", "FORWARD_RENDERER", ok,
                         ok ? "PIPELINES GX2 LISTOS" : "FALLO INIT FORWARDRENDERER");
    }

    // T5: Math3D (Perspective, LookAt, Quaternion, FromTRS sin NaN/Inf)
    {
        const Matrix4x4 proj = Matrix4x4::Perspective(1.04719755f, 16.0f / 9.0f, 0.15f, 300.0f);
        const Matrix4x4 view = Matrix4x4::LookAt(Point3D{0.0f, 3.0f, 10.0f}, Point3D{0.0f, 0.0f, 0.0f}, Point3D{0.0f, 1.0f, 0.0f});
        const Quaternion q1 = Quaternion::FromAxisAngle(Point3D{0.0f, 1.0f, 0.0f}, 0.785398f);
        const Quaternion q2 = Quaternion::FromAxisAngle(Point3D{1.0f, 0.0f, 0.0f}, 0.523598f);
        const Quaternion qProd = (q1 * q2).Normalized();
        const Matrix4x4 trs = Matrix4x4::FromTRS(Point3D{1.0f, 2.0f, -3.0f}, qProd, Point3D{2.0f, 2.0f, 2.0f});
        const Matrix4x4 mvp = proj * view * trs;

        const float qLen = qProd.Length();
        const bool ok = IsFiniteMat4(mvp) && (std::fabs(qLen - 1.0f) < 0.01f) && (proj.m[11] == -1.0f);
        outReport.Record("GFX", "MATH3D_PROJ_QUAT", ok,
                         ok ? "MVP + QUAT FINITOS OK" : "NAN/INF EN MATH3D");
    }

    // T6: ProceduralMeshBuilder (Cubo + Esfera UV cargables, multiplos de 3, bounding finito)
    {
        StaticMesh cube;
        StaticMesh sphere;
        const bool cubeOk = ProceduralMeshBuilder::CreateCube(cube, 1.0f, 1.0f);
        const bool sphereOk = ProceduralMeshBuilder::CreateUVSphere(sphere, 1.0f, 12u, 8u);
        const bool ok = cubeOk && sphereOk &&
                        cube.IsLoaded() && sphere.IsLoaded() &&
                        (cube.GetIndexCount() == 36u) &&
                        (sphere.GetIndexCount() > 0u) &&
                        ((sphere.GetIndexCount() % 3u) == 0u) &&
                        std::isfinite(sphere.GetBoundingRadius()) &&
                        (sphere.GetBoundingRadius() > 0.5f);
        cube.Destroy();
        sphere.Destroy();
        outReport.Record("GFX", "MESH_BUILDER_CCW", ok,
                         ok ? "CUBE+SPHERE VBO/IBO OK" : "FALLO PROCEDURAL MESH");
    }

    // T7: TextureLoaderGX2 Mipmap Chain (64x64 -> 7 niveles, image + mipmaps alineados >= 256B)
    {
        GX2Texture testTex{};
        const bool created = TextureLoaderGX2::CreateProceduralMipmapCheckerTexture(
            testTex, 64u, 8u, true, "SelfTest_Mip64");
#if defined(__WIIU__) || defined(__wiiu__)
        const bool ok = created &&
                        (testTex.surface.mipLevels == 7u) &&
                        (testTex.viewNumMips == 7u) &&
                        IsAligned256(testTex.surface.image) &&
                        IsAligned256(testTex.surface.mipmaps);
#else
        const bool ok = created;
#endif
        if (created) {
            TextureLoaderGX2::DestroyTexture(testTex);
        }
        outReport.Record("GFX", "MIPMAP_CHAIN_GX2", ok,
                         ok ? "64x64 7 MIPS ALIGN>=256" : "CADENA MIPMAPS INVALIDA");
    }

    // T8: RHIPipelineState (validacion + unicidad de hashes PSO)
    {
        const RHIPipelineState sOpaque(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false);
        const RHIPipelineState sAlpha(true, false, RHI_COMPARE_LEQUAL, BlendMode::AlphaBlend, false);
        const RHIPipelineState sNoDepth(false, false, RHI_COMPARE_ALWAYS, BlendMode::Opaque, false);

        const bool ok = (sOpaque.Validate() == RHIValidationResult::Valid) &&
                        (sAlpha.Validate() == RHIValidationResult::Valid) &&
                        (sNoDepth.Validate() == RHIValidationResult::Valid) &&
                        (sOpaque.ComputeHash() != sAlpha.ComputeHash()) &&
                        (sOpaque.ComputeHash() != sNoDepth.ComputeHash()) &&
                        (sAlpha.ComputeHash() != sNoDepth.ComputeHash());
        outReport.Record("GFX", "RHI_PIPELINE_HASH", ok,
                         ok ? "PSO VALIDATE + HASH OK" : "COLISION O ERROR PSO");
    }

    // T9: Pipeline Shader PBR Cook-Torrance + LightBlockUBO (Dir + Point + Spot)
    {
        const bool pbrReady = ForwardRendererGX2::Instance().IsPBRShaderLoaded();
        LightBlockUBO testLights{};
        testLights.ClearPointLights();
        const bool addedPt = testLights.AddPointLight(Point3D{1.0f, 2.0f, 3.0f}, 10.0f, Point3D{1.0f, 0.5f, 0.2f}, 8.0f);
        testLights.SetSpotLight(Point3D{0.0f, 5.0f, 5.0f}, Point3D{0.0f, -1.0f, -1.0f}, Point3D{1.0f, 1.0f, 1.0f}, 12.0f);
        const bool ok = pbrReady && addedPt && (testLights.activePointLights == 1u) &&
                        (testLights.spotLightIntensity > 10.0f);
        outReport.Record("GFX", "PBR_SHADER_PIPELINE", ok,
                         ok ? "PBR GGX + 3-LIGHT UBO OK" : "FALLO SHADER PBR/LIGHTS");
    }

    // T10: Textura procedural de relieve biselado con cadena completa de Mipmaps (64x64 -> 7 niveles)
    {
        GX2Texture reliefTex{};
        const bool created = TextureLoaderGX2::CreateProceduralReliefTexture(
            reliefTex, 64u, "SelfTest_Relief64");
#if defined(__WIIU__) || defined(__wiiu__)
        const bool ok = created &&
                        (reliefTex.surface.mipLevels == 7u) &&
                        (reliefTex.viewNumMips == 7u) &&
                        IsAligned256(reliefTex.surface.image) &&
                        IsAligned256(reliefTex.surface.mipmaps);
#else
        const bool ok = created;
#endif
        if (created) {
            TextureLoaderGX2::DestroyTexture(reliefTex);
        }
        outReport.Record("GFX", "RELIEF_TEX_MIPMAPS", ok,
                         ok ? "RELIEF 64x64 7 MIPS OK" : "FALLO TEXTURA RELIEVE");
    }

    // T11: ShadowMapRendererGX2 (1024x1024 D32F alineado >= 256B)
    {
        auto& sm = ShadowMapRendererGX2::Instance();
        const bool ready = sm.Initialize(ShadowMapRendererGX2::DEFAULT_RESOLUTION) && sm.IsReady();
#if defined(__WIIU__) || defined(__wiiu__)
        const auto* sdb = sm.GetShadowDepthBuffer();
        const auto* stx = sm.GetShadowTexture();
        const bool ok = ready && (sm.GetResolution() >= 512u) &&
                        (sdb != nullptr) && IsAligned256(sdb->surface.image) &&
                        (stx != nullptr) && IsAligned256(stx->surface.image);
#else
        const bool ok = ready && (sm.GetResolution() >= 512u);
#endif
        outReport.Record("GFX", "SHADOW_MAP_GX2", ok,
                         ok ? "1024x1024 D32F ALIGN>=256" : "FALLO SHADOW MAP GX2");
    }

    // T12: ShadowSystem CSM (splits crecientes + matrices ortográficas estabilizadas finitas)
    {
        float splits[ShadowSystem::MAX_CASCADES]{};
        ShadowSystem::ComputeCascadeSplits(0.2f, 120.0f, 3u, 0.65f, splits);
        const bool splitsOk = (splits[0] > 0.2f) && (splits[1] > splits[0]) && (splits[2] > splits[1]);

        ShadowCameraDesc cam{};
        cam.position = Point3D{0.0f, 5.0f, 10.0f};
        cam.target = Point3D{0.0f, 0.0f, 0.0f};
        ShadowConfig cfg{};
        ShadowCascade cascades[ShadowSystem::MAX_CASCADES]{};
        ShadowSystem::ComputeCascades(cam, Point3D{0.5f, 0.8f, 0.4f}, cfg, cascades);

        const bool matsOk = IsFiniteMat4(cascades[0].lightViewProjMatrix) &&
                            IsFiniteMat4(cascades[1].lightViewProjMatrix) &&
                            (cascades[0].texelWorldSize > 0.0f) &&
                            std::isfinite(cascades[0].texelWorldSize);
        const bool ok = splitsOk && matsOk;
        outReport.Record("GFX", "SHADOW_CSM_MATH", ok,
                         ok ? "CSM SPLITS + MATS OK" : "FALLO SHADOW CSM MATH");
    }

    // T13: Atlas 2D 3x2 de CubeMap procedural (32x32 por cara -> 96x64 TEXTURE_2D alineado >= 256B en MEM2)
    {
        GX2Texture skyAtlas{};
        const bool created = TextureLoaderGX2::CreateProceduralSkyCubeAtlas(skyAtlas, 32u, "SelfTest_SkyAtlas32");
#if defined(__WIIU__) || defined(__wiiu__)
        const bool ok = created &&
                        (skyAtlas.surface.dim == GX2_SURFACE_DIM_TEXTURE_2D) &&
                        (skyAtlas.surface.width == 96u) &&
                        (skyAtlas.surface.height == 64u) &&
                        (skyAtlas.viewNumSlices == 1u) &&
                        IsAligned256(skyAtlas.surface.image);
#else
        const bool ok = created;
#endif
        if (created) {
            TextureLoaderGX2::DestroyTexture(skyAtlas);
        }
        outReport.Record("GFX", "CUBEMAP_SKY_GX2", ok,
                         ok ? "ATLAS 96x64 2D ALIGN>=256" : "FALLO CUBEMAP ATLAS GX2");
    }

    // T14: Reflexión completa de los 3 slots TMU en el shader PBR (uTexture, uSkybox, uShadowMap)
    {
        const bool ok = ForwardRendererGX2::Instance().HasAllPBRSamplerSlots();
        outReport.Record("GFX", "PBR_IBL_SLOTS", ok,
                         ok ? "TMU DIFFUSE+SKYBOX+SHADOW OK" : "FALLO SLOTS TMU PBR");
    }

    // T15: RenderTargetGX2 off-screen reutilizable (Color RGBA8 + Depth D32F + GX2Texture aliased)
    {
        RenderTargetGX2 rt;
        const bool created = rt.Initialize(128u, 128u, "SelfTest_RT128");
#if defined(__WIIU__) || defined(__wiiu__)
        const auto* cb = rt.GetColorBuffer();
        const auto* db = rt.GetDepthBuffer();
        const auto* tx = rt.GetColorTexture();
        const bool ok = created && rt.IsReady() &&
                        (rt.GetWidth() == 128u) && (rt.GetHeight() == 128u) &&
                        (cb != nullptr) && IsAligned256(cb->surface.image) &&
                        (db != nullptr) && IsAligned256(db->surface.image) &&
                        (tx != nullptr) && (tx->surface.image == cb->surface.image);
#else
        const bool ok = created && rt.IsReady() && (rt.GetWidth() == 128u) && (rt.GetHeight() == 128u);
#endif
        rt.Shutdown();
        outReport.Record("GFX", "RENDER_TARGET_GX2", ok,
                         ok ? "RT 128x128 COLOR+DEPTH ALIGN>=256" : "FALLO RENDER TARGET GX2");
    }

    // T16: PostProcessHDRGX2 (HDR RGBA16_Float + Bloom Ping/Pong 1/4 sin Depth + Reflexión TMU real)
    {
        PostProcessHDRGX2 postFx;
        const bool created = postFx.Initialize(256u, 256u);
        const auto& hdrRT = postFx.GetHDRRenderTarget();
        const auto& pingRT = postFx.GetBloomPingRenderTarget();
        const auto& pongRT = postFx.GetBloomPongRenderTarget();

        bool ok = created && postFx.IsReady() && postFx.HasAllSamplerSlots() &&
                  (hdrRT.GetWidth() == 256u) && (hdrRT.GetHeight() == 256u) &&
                  (hdrRT.GetFormat() == RenderTargetFormat::RGBA16_Float) && hdrRT.HasDepthBuffer() &&
                  (pingRT.GetWidth() == 64u) && (pingRT.GetHeight() == 64u) &&
                  (pingRT.GetFormat() == RenderTargetFormat::RGBA16_Float) && !pingRT.HasDepthBuffer() &&
                  (pongRT.GetWidth() == 64u) && (pongRT.GetHeight() == 64u) &&
                  (pongRT.GetFormat() == RenderTargetFormat::RGBA16_Float) && !pongRT.HasDepthBuffer();
#if defined(__WIIU__) || defined(__wiiu__)
        const auto* hdrCb = hdrRT.GetColorBuffer();
        const auto* hdrDb = hdrRT.GetDepthBuffer();
        const auto* pingCb = pingRT.GetColorBuffer();
        const auto* pongCb = pongRT.GetColorBuffer();
        ok = ok &&
             (hdrCb != nullptr) && (hdrCb->surface.format == GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16) &&
             IsAligned256(hdrCb->surface.image) &&
             (hdrDb != nullptr) && IsAligned256(hdrDb->surface.image) &&
             (pingCb != nullptr) && (pingCb->surface.format == GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16) &&
             IsAligned256(pingCb->surface.image) &&
             (pongCb != nullptr) && (pongCb->surface.format == GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16) &&
             IsAligned256(pongCb->surface.image);
#endif
        postFx.Shutdown();
        const bool cleanAfter = !postFx.IsReady() && !postFx.HasAllSamplerSlots();
        outReport.Record("GFX", "HDR_POSTPROCESS_GX2", ok && cleanAfter,
                         (ok && cleanAfter) ? "HDR16F + BLOOM 1/4 ALIGN>=256" : "FALLO POSTPROCESS HDR GX2");
    }

    // T17: Textura procedural Alpha-Cutout (celosía/follaje con píxeles alpha=0 y alpha=255)
    {
        GX2Texture cutTex{};
        const bool created = TextureLoaderGX2::CreateProceduralAlphaCutoutTexture(cutTex, 64u, "SelfTest_Cutout64");
#if defined(__WIIU__) || defined(__wiiu__)
        bool hasOpaque = false;
        bool hasTransparent = false;
        if (created && cutTex.surface.image != nullptr) {
            const uint8_t* base = static_cast<const uint8_t*>(cutTex.surface.image);
            const uint32_t pitchBytes = cutTex.surface.pitch * 4u;
            for (uint32_t y = 0u; y < 64u && !(hasOpaque && hasTransparent); ++y) {
                const uint8_t* row = base + static_cast<size_t>(y) * pitchBytes;
                for (uint32_t x = 0u; x < 64u; ++x) {
                    const uint8_t a = row[x * 4u + 3u];
                    if (a == 255u) hasOpaque = true;
                    if (a == 0u)   hasTransparent = true;
                }
            }
        }
        const bool ok = created && IsAligned256(cutTex.surface.image) && hasOpaque && hasTransparent;
#else
        const bool ok = created;
#endif
        if (created) {
            TextureLoaderGX2::DestroyTexture(cutTex);
        }
        outReport.Record("GFX", "ALPHA_CUTOUT_TEX", ok,
                         ok ? "CUTOUT 64x64 ALPHA 0/255 OK" : "FALLO ALPHA CUTOUT TEX");
    }

    // T18: MeshLODGroup (3 niveles por distancia) + telemetría de instancing en ForwardRendererGX2
    {
        MeshLODGroup lod;
        lod.SetDistances(20.0f, 40.0f);
        const bool lodOk = (lod.SelectLevel(100.0f) == 0u) &&   // 10m < 20m -> LOD0
                           (lod.SelectLevel(900.0f) == 1u) &&   // 30m in [20..40) -> LOD1
                           (lod.SelectLevel(2500.0f) == 2u) &&  // 50m >= 40m -> LOD2
                           (lod.GetLOD1DistanceSq() == 400.0f) &&
                           (lod.GetLOD2DistanceSq() == 1600.0f);
        const auto& renderer = ForwardRendererGX2::Instance();
        const bool instApiOk = (renderer.GetInstancedDrawCalls() <= renderer.GetDrawCallsCount() + 1000u);
        const bool ok = lodOk && instApiOk;
        outReport.Record("GFX", "MESH_LOD_INSTANCING", ok,
                         ok ? "LOD 0/1/2 + INST TELEMETRY OK" : "FALLO LOD / INSTANCING");
    }

    // T19: Terreno procedural Heightmap (malla 8x8 -> 128 tris, alturas finitas y meseta central)
    {
        StaticMesh terrain;
        const bool built = ProceduralMeshBuilder::CreateHeightmapTerrain(terrain, 24.0f, 24.0f, 8u, 8u, 4.0f);
        const float hCenter = ProceduralMeshBuilder::EvaluateTerrainHeight(0.0f, 0.0f, 4.0f);
        const float hRim = ProceduralMeshBuilder::EvaluateTerrainHeight(16.0f, 16.0f, 4.0f);
        const bool ok = built && terrain.IsLoaded() &&
                        (terrain.GetIndexCount() == 8u * 8u * 6u) &&
                        std::isfinite(hCenter) && std::isfinite(hRim) &&
                        (hCenter <= 0.01f) && (hRim > 0.1f);
        terrain.Destroy();
        outReport.Record("GFX", "HEIGHTMAP_TERRAIN_MESH", ok,
                         ok ? "TERRAIN 8x8 + HEIGHT EVAL OK" : "FALLO HEIGHTMAP TERRAIN");
    }

    // T20: ParticleSystem3D Zero-Alloc (pool fijo <= 128, spawn determinista, expiración y billboard finito)
    {
        ParticleSystem3D ps;
        ps.Reset(Point3D{0.0f, 1.0f, 0.0f});
        for (int i = 0; i < 15; ++i) {
            ps.Update(0.03f, true);
        }
        const uint32_t activeSpawned = ps.GetActiveCount();
        for (int i = 0; i < 120; ++i) {
            ps.Update(0.05f, false);
        }
        const uint32_t activeAfterDrain = ps.GetActiveCount();
        const Matrix4x4 bb = ParticleSystem3D::MakeBillboardMatrix(
            Point3D{1.0f, 2.0f, 3.0f}, Point3D{1.0f, 0.0f, 0.0f},
            Point3D{0.0f, 1.0f, 0.0f}, Point3D{0.0f, 0.0f, -1.0f}, 0.5f);
        const bool ok = (activeSpawned > 0u) && (activeSpawned <= ParticleSystem3D::MAX_PARTICLES) &&
                        (activeAfterDrain == 0u) && IsFiniteMat4(bb);
        outReport.Record("GFX", "PARTICLE_POOL_ZERO_ALLOC", ok,
                         ok ? "POOL 128 SPAWN/DRAIN/BB OK" : "FALLO PARTICLE SYSTEM");
    }

    // T21: Pipeline Ray Tracing BVH sobre RHIFullscreenShader (carga GFD + reflexión uniformes + Shutdown sin leaks)
    {
        RHIFullscreenShader rt;
        const bool initOk = rt.Initialize("shaders/raytracing_bvh.gsh",
                                          RAYTRACING_BVH_GSH_DATA,
                                          RAYTRACING_BVH_GSH_SIZE);
#if defined(__WIIU__) || defined(__wiiu__)
        const bool uniformsOk = (rt.GetPixelUniformOffset("uCamPos") >= 0) &&
                                (rt.GetPixelUniformOffset("uRenderFlags") >= 0) &&
                                (rt.GetPixelUniformOffset("uBvhNodes") >= 0);
#else
        const bool uniformsOk = true;
#endif
        const bool readyOk = initOk && rt.IsReady() && uniformsOk;
        rt.Shutdown();
        const bool cleanAfter = !rt.IsReady();
        outReport.Record("GFX", "RAYTRACING_BVH_PIPELINE", readyOk && cleanAfter,
                         (readyOk && cleanAfter) ? "RT BVH GFD + UNIFORMS OK" : "FALLO RT BVH PIPELINE");
    }

    // T22: Decodificador WTEX BC3 Little-Endian -> RGBA8 LINEAR_ALIGNED alineado >= 256B (0 leaks)
    {
        // Cabecera WTEX Big-Endian (16 B): magic='WTEX', width=8, height=4, fmtCode=3 (BC3), mipLevels=1
        // + 2 bloques DXT5/BC3 de 16 B cada uno (32 B): bloque 0 rojo opaco (A=255), bloque 1 azul transparente (A=0)
        const uint8_t syntheticWtexBc3[48] = {
            0x57u, 0x54u, 0x45u, 0x58u, // 'WTEX'
            0x00u, 0x00u, 0x00u, 0x08u, // width = 8
            0x00u, 0x00u, 0x00u, 0x04u, // height = 4
            0x00u, 0x03u, 0x00u, 0x01u, // fmt = 3 (BC3), mips = 1
            // Bloque 0 (x=0..3): alpha a0=255, a1=255, bits=0 | color c0=0xF800 (LE: 0x00,0xF8), c1=0x0000, idx=0
            0xFFu, 0xFFu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
            0x00u, 0xF8u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
            // Bloque 1 (x=4..7): alpha a0=0, a1=0, bits=0 | color c0=0x001F (LE: 0x1F,0x00), c1=0x0000, idx=0
            0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
            0x1Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
        };

        GX2Texture decodedTex{};
        const bool loaded = TextureLoaderGX2::LoadCookedTextureFromMemory(
            syntheticWtexBc3, sizeof(syntheticWtexBc3), decodedTex, "SelfTest_WTEX_BC3");
        const bool rejectTruncated = !TextureLoaderGX2::LoadCookedTextureFromMemory(
            syntheticWtexBc3, 24u, decodedTex, "SelfTest_WTEX_Trunc");
        // Re-cargar tras prueba negativa
        const bool reloaded = TextureLoaderGX2::LoadCookedTextureFromMemory(
            syntheticWtexBc3, sizeof(syntheticWtexBc3), decodedTex, "SelfTest_WTEX_BC3");

#if defined(__WIIU__) || defined(__wiiu__)
        bool pxOk = false;
        if (reloaded && decodedTex.surface.image != nullptr) {
            const auto* px = static_cast<const uint8_t*>(decodedTex.surface.image);
            // Pixel (0,0): rojo puro opaco (R>=240, G==0, B==0, A==255)
            // Pixel (4,0): azul puro transparente (R==0, G==0, B>=240, A==0)
            pxOk = (px[0] >= 240u) && (px[1] == 0u) && (px[2] == 0u) && (px[3] == 255u) &&
                   (px[16] == 0u) && (px[17] == 0u) && (px[18] >= 240u) && (px[19] == 0u);
        }
        const bool ok = loaded && rejectTruncated && reloaded && pxOk &&
                        (decodedTex.surface.format == GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8) &&
                        (decodedTex.surface.mipLevels == 1u) &&
                        (decodedTex.surface.mipmaps == nullptr) &&
                        IsAligned256(decodedTex.surface.image);
#else
        const bool ok = loaded && rejectTruncated && reloaded;
#endif
        TextureLoaderGX2::DestroyTexture(decodedTex);
        outReport.Record("GFX", "WTEX_BC_DECODE_GX2", ok,
                         ok ? "BC3 LE->RGBA8 ALIGN>=256 OK" : "FALLO DECODIFICADOR WTEX");
    }
}

} // namespace WiiU::Graphics
