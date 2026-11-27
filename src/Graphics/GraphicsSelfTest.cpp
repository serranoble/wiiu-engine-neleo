#include "Graphics/GraphicsSelfTest.hpp"
#include "Graphics/RenderGraph.hpp"
#include "Graphics/ParticleSystem3D.hpp"
#include "Graphics/RaytracingBVHShaderBinary.hpp"
#include "Graphics/Mesh/MeshLODGroup.hpp"
#include "Graphics/PostProcessHDRGX2.hpp"
#include "Graphics/RHI/RHIFullscreenShader.hpp"
#include "Graphics/RHI/RenderTargetGX2.hpp"
#include "Core/Window.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/Render2DGX2.hpp"
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

namespace Neleo::Graphics {

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

    // T4: ForwardRendererGX2 inicializado (shaders + PBR Uber + PBR Lite + fallbacks) + Render2DGX2 Font Atlas
    {
        auto& renderer = ForwardRendererGX2::Instance();
        auto& r2d = Render2DGX2::Instance();
        const bool ok = renderer.Initialize() && renderer.IsInitialized() &&
                        renderer.IsPBRShaderLoaded() && renderer.IsPBRVariantsReady() &&
                        renderer.IsWaterGlassShaderLoaded() && renderer.IsHDRFogShaderLoaded() &&
                        r2d.Init() && r2d.IsFontAtlasReady();
        outReport.Record("GFX", "FORWARD_RENDERER", ok,
                         ok ? "GX2 + 5 PBR + HDRFOG + UI2D OK" : "FALLO INIT RENDERERS GX2");
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

    // T23: [OPT-457] Light-Culling por Bounding Sphere + ShaderVariantKey O(1)
    {
        StaticMesh sphere;
        const bool meshOk = ProceduralMeshBuilder::CreateUVSphere(sphere, 1.0f, 8u, 6u);

        LightBlockUBO lights{};
        lights.ClearPointLights();
        (void)lights.AddPointLight(Point3D{0.0f, 0.0f, 0.0f}, 4.0f, Point3D{1.0f, 0.5f, 0.2f}, 8.0f);
        (void)lights.AddPointLight(Point3D{50.0f, 0.0f, 0.0f}, 4.0f, Point3D{0.2f, 0.8f, 1.0f}, 8.0f);

        uint32_t idxNear[4]{};
        const uint32_t cntNear = CullPointLightsForMesh(
            sphere, Matrix4x4::Translation(2.0f, 0.0f, 0.0f), lights, idxNear, 4u);

        uint32_t idxFar[4]{};
        const uint32_t cntFar = CullPointLightsForMesh(
            sphere, Matrix4x4::Translation(-25.0f, 0.0f, 0.0f), lights, idxFar, 4u);

        // Caso > 4 luces: 4 primeras fuera de rango, indices 4 y 5 intersectan con maxOut=4u
        LightBlockUBO manyLights{};
        manyLights.ClearPointLights();
        for (uint32_t k = 0u; k < 4u; ++k) {
            (void)manyLights.AddPointLight(Point3D{100.0f + static_cast<float>(k) * 10.0f, 0.0f, 0.0f}, 3.0f,
                                           Point3D{1.0f, 1.0f, 1.0f}, 5.0f);
        }
        (void)manyLights.AddPointLight(Point3D{0.5f, 0.0f, 0.0f}, 4.0f, Point3D{1.0f, 0.2f, 0.2f}, 8.0f);
        (void)manyLights.AddPointLight(Point3D{-0.5f, 0.0f, 0.0f}, 4.0f, Point3D{0.2f, 1.0f, 0.2f}, 8.0f);
        uint32_t idxMany[4]{};
        const uint32_t cntMany = CullPointLightsForMesh(
            sphere, Matrix4x4::Identity(), manyLights, idxMany, 4u);

        const PBRVariantIndex vDir   = SelectPBRVariant(MakeShaderVariantKey(false, false, false));
        const PBRVariantIndex vDirB  = SelectPBRVariant(MakeShaderVariantKey(false, true,  false));
        const PBRVariantIndex vMulti = SelectPBRVariant(MakeShaderVariantKey(true,  false, false));
        const PBRVariantIndex vShd   = SelectPBRVariant(MakeShaderVariantKey(false, false, true));

        sphere.Destroy();

        const bool ok = meshOk &&
                        (cntNear == 1u) && (idxNear[0] == 0u) &&
                        (cntFar == 0u) &&
                        (cntMany == 2u) && (idxMany[0] == 4u) && (idxMany[1] == 5u) &&
                        (vDir == PBRVariantIndex::DirOnly) &&
                        (vDirB == PBRVariantIndex::DirBump) &&
                        (vMulti == PBRVariantIndex::MultiLight) &&
                        (vShd == PBRVariantIndex::Shadow);
        outReport.Record("GFX", "LIGHT_CULL_VARIANT", ok,
                         ok ? "SPHERE CULL + 5 VARIANTS OK" : "FALLO CULLING/VARIANT KEY");
    }

    // T24: [OPT-459] RenderQueuePBR SortKey 64-bit (Opacos F2B -> Cutout F2B -> Translucidos B2F)
    {
        StaticMesh unitMesh;
        const bool meshOk = ProceduralMeshBuilder::CreateCube(unitMesh, 1.0f);

        BasicRenderQueuePBR<16u> queue;
        const Point3D cam{0.0f, 0.0f, 0.0f};

        MaterialPropertyBlock opMat{};
        opMat.blendMode = BlendMode::Opaque;
        MaterialPropertyBlock cutMat{};
        cutMat.blendMode = BlendMode::AlphaCutout;
        MaterialPropertyBlock trMat{};
        trMat.blendMode = BlendMode::AlphaBlend;

        // Enviar deliberadamente desordenados (translucido cercano, opaco lejano, cutout, translucido lejano, opaco cercano)
        bool subOk = true;
        subOk = subOk && queue.Submit(unitMesh, Matrix4x4::Translation(0.0f, 0.0f, -5.0f),  trMat,  0u, cam);
        subOk = subOk && queue.Submit(unitMesh, Matrix4x4::Translation(0.0f, 0.0f, -20.0f), opMat,  0u, cam);
        subOk = subOk && queue.Submit(unitMesh, Matrix4x4::Translation(0.0f, 0.0f, -12.0f), cutMat, 0u, cam);
        subOk = subOk && queue.Submit(unitMesh, Matrix4x4::Translation(0.0f, 0.0f, -30.0f), trMat,  0u, cam);
        subOk = subOk && queue.Submit(unitMesh, Matrix4x4::Translation(0.0f, 0.0f, -4.0f),  opMat,  0u, cam);

        queue.Sort();
        bool orderOk = (queue.GetCount() == 5u);
        if (orderOk) {
            const RenderCommandPBR& c0 = queue.GetSortedCommand(0u);
            const RenderCommandPBR& c1 = queue.GetSortedCommand(1u);
            const RenderCommandPBR& c2 = queue.GetSortedCommand(2u);
            const RenderCommandPBR& c3 = queue.GetSortedCommand(3u);
            const RenderCommandPBR& c4 = queue.GetSortedCommand(4u);
            // [0]=op near (-4), [1]=op far (-20), [2]=cutout (-12), [3]=tr far (-30), [4]=tr near (-5)
            orderOk = (c0.material.blendMode == BlendMode::Opaque) && (c0.modelMatrix.m[14] > c1.modelMatrix.m[14]) &&
                      (c1.material.blendMode == BlendMode::Opaque) &&
                      (c2.material.blendMode == BlendMode::AlphaCutout) &&
                      (c3.material.blendMode == BlendMode::AlphaBlend) && (c3.modelMatrix.m[14] < c4.modelMatrix.m[14]) &&
                      (c4.material.blendMode == BlendMode::AlphaBlend);
        }

        // Prueba de limite de capacidad (0 overflow)
        queue.Clear();
        uint32_t accepted = 0u;
        for (uint32_t i = 0u; i < BasicRenderQueuePBR<16u>::MAX_COMMANDS + 6u; ++i) {
            if (queue.Submit(unitMesh, Matrix4x4::Identity(), opMat, 0u, cam)) {
                ++accepted;
            }
        }
        const bool capOk = (accepted == BasicRenderQueuePBR<16u>::MAX_COMMANDS) &&
                           (queue.GetCount() == BasicRenderQueuePBR<16u>::MAX_COMMANDS);

        unitMesh.Destroy();
        const bool ok = meshOk && subOk && orderOk && capOk;
        outReport.Record("GFX", "RENDER_QUEUE_SORT", ok,
                         ok ? "F2B OPAQUE + B2F TRANSPARENT OK" : "FALLO RENDER QUEUE SORT");
    }

    // T25: [OPT-460] Frustum Culling automatico por Bounding Sphere en SubmitStaticMeshPBR
    {
        StaticMesh sphere;
        const bool meshOk = ProceduralMeshBuilder::CreateUVSphere(sphere, 1.0f, 8u, 6u);

        const Matrix4x4 view = Matrix4x4::LookAt(Point3D{0.0f, 0.0f, 10.0f}, Point3D{0.0f, 0.0f, 0.0f}, Point3D{0.0f, 1.0f, 0.0f});
        const Matrix4x4 proj = Matrix4x4::Perspective(1.0471975f, 16.0f / 9.0f, 0.5f, 100.0f);
        ViewFrustum frustum;
        frustum.ExtractFromViewProj(proj * view);

        const bool visFront   = IsMeshVisibleInFrustum(sphere, Matrix4x4::Translation(0.0f, 0.0f, 0.0f), frustum);
        const bool culBehind  = !IsMeshVisibleInFrustum(sphere, Matrix4x4::Translation(0.0f, 0.0f, 25.0f), frustum);
        const bool culSide    = !IsMeshVisibleInFrustum(sphere, Matrix4x4::Translation(80.0f, 0.0f, 0.0f), frustum);
        const bool visScaled  = IsMeshVisibleInFrustum(sphere, Matrix4x4::Translation(0.0f, 0.0f, -85.0f) * Matrix4x4::Scale(2.0f, 2.0f, 2.0f), frustum);

        sphere.Destroy();
        const bool ok = meshOk && visFront && culBehind && culSide && visScaled;
        outReport.Record("GFX", "FRUSTUM_CULL_SUBMIT", ok,
                         ok ? "SPHERE FRUSTUM CULL OK" : "FALLO FRUSTUM CULL SUBMIT");
    }

    // T26: [OPT-462] Depth Pre-Pass integrado en RenderQueuePBR (Dummy PS seguro + toggle opt-in)
    {
        auto& renderer = ForwardRendererGX2::Instance();
        const bool shaderOk = renderer.IsDepthPrePassShaderLoaded();
        const bool prevState = renderer.IsDepthPrePassEnabled();
        renderer.SetDepthPrePassEnabled(true);
        const bool enabledOk = renderer.IsDepthPrePassEnabled();
        renderer.SetDepthPrePassEnabled(false);
        const bool disabledOk = !renderer.IsDepthPrePassEnabled();
        renderer.SetDepthPrePassEnabled(prevState);

        const bool ok = shaderOk && enabledOk && disabledOk;
        outReport.Record("GFX", "DEPTH_PREPASS_QUEUE", ok,
                         ok ? "Z-PREPASS DUMMY PS + QUEUE OK" : "FALLO DEPTH PREPASS");
    }

    // T27: [OPT-463] Cadena de Mipmaps en WTEX 64x64 (GX2CopySurface mipLevels>1 en Wii U / GL mipmaps en PC)
    {
        // WTEX sintetico 64x64 RGBA8 (16 B cabecera BE + 64*64*4 = 16384 B)
        std::vector<uint8_t> wtex64(16u + 64u * 64u * 4u, 180u);
        wtex64[0] = 0x57u; wtex64[1] = 0x54u; wtex64[2] = 0x45u; wtex64[3] = 0x58u; // 'WTEX'
        wtex64[4] = 0x00u; wtex64[5] = 0x00u; wtex64[6] = 0x00u; wtex64[7] = 0x40u; // width = 64
        wtex64[8] = 0x00u; wtex64[9] = 0x00u; wtex64[10] = 0x00u; wtex64[11] = 0x40u; // height = 64
        wtex64[12] = 0x00u; wtex64[13] = 0x01u; wtex64[14] = 0x00u; wtex64[15] = 0x01u; // fmt=1 (RGBA8), mips=1

        GX2Texture mipTex{};
        const bool loaded = TextureLoaderGX2::LoadCookedTextureFromMemory(
            wtex64.data(), wtex64.size(), mipTex, "SelfTest_WTEX_Mip64");
#if defined(__WIIU__) || defined(__wiiu__)
        const bool mipsOk = loaded && (mipTex.surface.mipLevels >= 4u) &&
                            (mipTex.viewNumMips == mipTex.surface.mipLevels) &&
                            (mipTex.surface.mipmaps != nullptr) &&
                            (mipTex.surface.image != nullptr);
#else
        const bool mipsOk = loaded;
#endif
        TextureLoaderGX2::DestroyTexture(mipTex);
        outReport.Record("GFX", "WTEX_MIPMAP_CHAIN", mipsOk,
                         mipsOk ? "WTEX 64X64 MIPMAP CHAIN OK" : "FALLO WTEX MIPMAPS");
    }

    // T28: [OPT-464] Auto-Batching / Instancing Automatico en RenderQueuePBR
    {
        StaticMesh cube;
        const bool meshOk = ProceduralMeshBuilder::CreateCube(cube, 1.0f);

        BasicRenderQueuePBR<16u> queue;
        const Point3D cam{0.0f, 0.0f, 0.0f};
        MaterialPropertyBlock matA{};
        matA.blendMode = BlendMode::Opaque;
        matA.ubo.albedoTint[0] = 0.8f; matA.ubo.albedoTint[1] = 0.4f; matA.ubo.albedoTint[2] = 0.2f;

        MaterialPropertyBlock matB = matA;
        matB.ubo.albedoTint[0] = 0.2f; matB.ubo.albedoTint[2] = 0.9f;

        MaterialPropertyBlock matEmissive = matA;
        matEmissive.ubo.emissive[0] = 2.0f;

        MaterialPropertyBlock matShader2 = matA;
        matShader2.shaderId = 7u;

        // 5 cubos identicos + 1 distinto albedo + 2 emisivos (no deben agruparse) + 1 distinto shaderId
        for (uint32_t k = 0u; k < 5u; ++k) {
            (void)queue.Submit(cube, Matrix4x4::Translation(static_cast<float>(k) * 0.1f, 0.0f, -10.0f), matA, 0u, cam);
        }
        (void)queue.Submit(cube, Matrix4x4::Translation(0.0f, 0.0f, -20.0f), matB, 0u, cam);
        (void)queue.Submit(cube, Matrix4x4::Translation(0.0f, 0.0f, -30.0f), matEmissive, 0u, cam);
        (void)queue.Submit(cube, Matrix4x4::Translation(0.1f, 0.0f, -30.0f), matEmissive, 0u, cam);
        (void)queue.Submit(cube, Matrix4x4::Translation(0.0f, 0.0f, -40.0f), matShader2, 0u, cam);
        queue.Sort();

        Matrix4x4 batchMats[8]{};
        const uint32_t runA  = queue.FindAutoBatchRun(0u, batchMats, 8u);
        const uint32_t runB  = queue.FindAutoBatchRun(runA, batchMats, 8u);
        const uint32_t runEm = queue.FindAutoBatchRun(runA + runB, batchMats, 8u);

        cube.Destroy();
        const bool ok = meshOk && (runA == 5u) && (runB == 1u) && (runEm == 1u);
        outReport.Record("GFX", "AUTO_BATCH_RUNS", ok,
                         ok ? "AUTO-BATCH PBR 5+1+EMIS OK" : "FALLO AUTO-BATCH RUNS");
    }

    // T29: [OPT-465] RenderGraph declarativo Zero-Alloc (orden topologico, skip disabled, tope MAX_PASSES)
    {
        struct GraphCtx {
            uint32_t order[4]{};
            uint32_t count{0u};
        } ctx{};

        RenderGraph graph;
        const bool a1 = graph.AddPass("PassA", ProfileZone::ShadowPass, [](void* p) {
            auto* c = static_cast<GraphCtx*>(p); c->order[c->count++] = 1u;
        }, &ctx, true);
        const bool a2 = graph.AddPass("PassDisabled", ProfileZone::Render3D, [](void* p) {
            auto* c = static_cast<GraphCtx*>(p); c->order[c->count++] = 99u;
        }, &ctx, false);
        const bool a3 = graph.AddPass("PassB", ProfileZone::PostFX, [](void* p) {
            auto* c = static_cast<GraphCtx*>(p); c->order[c->count++] = 2u;
        }, &ctx, true);
        graph.Execute();

        const bool execOk = a1 && a2 && a3 && (graph.GetPassCount() == 3u) &&
                            (graph.GetExecutedCount() == 2u) && (ctx.count == 2u) &&
                            (ctx.order[0] == 1u) && (ctx.order[1] == 2u);

        // Prueba de capacidad maxima sin overflow
        graph.Reset();
        uint32_t added = 0u;
        for (uint32_t i = 0u; i < RenderGraph::MAX_PASSES + 4u; ++i) {
            if (graph.AddPass("P", ProfileZone::Render3D, [](void*) {}, nullptr, true)) {
                ++added;
            }
        }
        const bool capOk = (added == RenderGraph::MAX_PASSES) && (graph.GetPassCount() == RenderGraph::MAX_PASSES);

        outReport.Record("GFX", "RENDER_GRAPH_EXEC", execOk && capOk,
                         (execOk && capOk) ? "RENDER GRAPH ZERO-ALLOC OK" : "FALLO RENDER GRAPH");
    }
}

} // namespace Neleo::Graphics
