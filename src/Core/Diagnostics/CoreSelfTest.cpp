#include "Core/Diagnostics/CoreSelfTest.hpp"
#include "Core/Diagnostics/EngineProfiler.hpp"
#include "Core/JobSystem.hpp"
#include "Core/Spatial/Frustum.hpp"
#include "Core/Spatial/StacklessBVH.hpp"
#include "Core/Math/PRNG.hpp"
#include "Core/Storage/CryptoHMAC.hpp"
#include "Core/Spatial/SpatialBVH.hpp"
#include "Core/Config/CVarRegistry.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Core/Localization/LocalizationManager.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Neleo::Diagnostics {

void CoreSelfTest::RunAll(EngineSelfTestReport& outReport) {
    // 1. PRNG PCG32: determinismo bit a bit + rangos acotados
    {
        PRNG a(0x123456789ABCDEF0ULL, 0x0F0F0F0FULL);
        PRNG b(0x123456789ABCDEF0ULL, 0x0F0F0F0FULL);
        bool ok = true;
        for (int i = 0; i < 16; ++i) {
            if (a.NextUInt32() != b.NextUInt32()) {
                ok = false;
                break;
            }
            const int32_t r = a.NextIntRange(-5, 5);
            const int32_t r2 = b.NextIntRange(-5, 5);
            if (r != r2 || r < -5 || r > 5) {
                ok = false;
                break;
            }
        }
        outReport.Record("CORE", "PRNG_PCG32", ok,
                         ok ? "DETERMINISMO PCG32 OK" : "FALLO DETERMINISMO PRNG");
    }

    // 2. CryptoHMAC: vector canónico FIPS 180-4 SHA-256("abc") + HMAC + ConstantTimeEqual
    {
        static constexpr char kMsg[] = "abc";
        static constexpr CryptoHMAC::Digest kExpectedSha256Abc = {
            0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
            0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
            0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
            0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
        };
        const CryptoHMAC::Digest sha = CryptoHMAC::ComputeSha256(kMsg, 3u);
        const CryptoHMAC::Digest h1 = CryptoHMAC::ComputeHmacSha256("wiiu-key", kMsg, 3u);
        const CryptoHMAC::Digest h2 = CryptoHMAC::ComputeHmacSha256("wiiu-key", kMsg, 3u);
        const CryptoHMAC::Digest h3 = CryptoHMAC::ComputeHmacSha256("other-key", kMsg, 3u);
        const bool ok = CryptoHMAC::ConstantTimeEqual(sha, kExpectedSha256Abc) &&
                        CryptoHMAC::ConstantTimeEqual(h1, h2) &&
                        !CryptoHMAC::ConstantTimeEqual(h1, h3);
        outReport.Record("CORE", "CRYPTO_HMAC_SHA256", ok,
                         ok ? "FIPS SHA256+HMAC OK" : "FALLO DIGEST CRYPTO");
    }

    // 3. SpatialAABB + SpatialBVH: intersección, Build SAH, QueryAABB y RayCast
    {
        SpatialAABB boxA(Point3D{-1.0f, -1.0f, -1.0f}, Point3D{1.0f, 1.0f, 1.0f});
        SpatialAABB boxB(Point3D{0.5f, 0.5f, 0.5f}, Point3D{2.0f, 2.0f, 2.0f});
        SpatialAABB boxC(Point3D{5.0f, 5.0f, 5.0f}, Point3D{6.0f, 6.0f, 6.0f});

        BVHPrimitive prims[2]{};
        prims[0].bounds = boxA;
        prims[0].centroid = boxA.Center();
        prims[0].userId = 10u;
        prims[1].bounds = boxC;
        prims[1].centroid = boxC.Center();
        prims[1].userId = 20u;

        SpatialBVH bvh;
        bvh.Build(prims, 2u);

        uint32_t hits[4]{};
        size_t hitCount = 0u;
        bvh.QueryAABB(boxB, hits, 4u, hitCount);

        BVHRayHit rayHit{};
        bvh.RayCast(Point3D{0.0f, 0.0f, 5.0f}, Point3D{0.0f, 0.0f, -1.0f}, 20.0f, rayHit);

        const bool ok = boxA.IntersectsAABB(boxB) &&
                        !boxA.IntersectsAABB(boxC) &&
                        (std::fabs(boxA.SurfaceArea() - 24.0f) < 0.01f) &&
                        (hitCount == 1u) && (hits[0] == 10u) &&
                        rayHit.hit && (rayHit.userId == 10u);
        outReport.Record("CORE", "SPATIAL_AABB_BVH", ok,
                         ok ? "BVH SAH/QUERY/RAY OK" : "FALLO SPATIAL BVH");
    }

    // 4. EngineConfig + CVarRegistry aislado (0 slots ocupados en el singleton vivo)
    {
        static CVarRegistry s_isolatedCvars;
        s_isolatedCvars.Clear();
        auto* entry = s_isolatedCvars.RegisterInt("qa.selftest_val", 42, 0, 100, "SelfTest CVar");
        const bool setOk = (entry != nullptr) && s_isolatedCvars.SetInt("qa.selftest_val", 77);
        const int32_t readBack = s_isolatedCvars.GetInt("qa.selftest_val", -1);
        const bool clampOk = s_isolatedCvars.SetInt("qa.selftest_val", 999) &&
                             (s_isolatedCvars.GetInt("qa.selftest_val", -1) == 100);
        const bool resetOk = s_isolatedCvars.Reset("qa.selftest_val") &&
                             (s_isolatedCvars.GetInt("qa.selftest_val", -1) == 42);
        const auto& cfg = EngineConfig::Instance();
        const bool cfgOk = (cfg.network.tcpPort > 0u) && (cfg.network.udpPort > 0u) &&
                           (cfg.graphics.GetFilterName() != nullptr);
        const bool ok = setOk && (readBack == 77) && clampOk && resetOk && cfgOk;
        outReport.Record("CORE", "ENGINE_CONFIG_CVAR", ok,
                         ok ? "CVAR AISLADO + CONFIG OK" : "FALLO CVAR/CONFIG");
    }

    // 5. ViewFrustum (extracción Gribb-Hartmann + culling esfera/AABB + 8 vértices finitos)
    {
        const Matrix4x4 view = Matrix4x4::LookAt(Point3D{0.0f, 0.0f, 10.0f}, Point3D{0.0f, 0.0f, 0.0f}, Point3D{0.0f, 1.0f, 0.0f});
        const Matrix4x4 proj = Matrix4x4::Perspective(1.04719755f, 1280.0f / 720.0f, 0.5f, 50.0f);
        ViewFrustum frustum;
        frustum.ExtractFromViewProj(proj * view);

        const bool inFront  = frustum.TestSphere(Point3D{0.0f, 0.0f, 0.0f}, 1.0f);
        const bool behind   = frustum.TestSphere(Point3D{0.0f, 0.0f, 25.0f}, 1.0f);
        const bool farAway  = frustum.TestSphere(Point3D{0.0f, 0.0f, -80.0f}, 1.0f);
        const bool aabbIn   = frustum.TestAABB(Point3D{-1.0f, -1.0f, -1.0f}, Point3D{1.0f, 1.0f, 1.0f});
        const bool aabbOut  = frustum.TestAABB(Point3D{40.0f, 40.0f, 0.0f}, Point3D{42.0f, 42.0f, 2.0f});

        Point3D corners[8]{};
        ViewFrustum::ComputeCorners(Point3D{0.0f, 0.0f, 10.0f}, Point3D{0.0f, 0.0f, -1.0f},
                                    Point3D{1.0f, 0.0f, 0.0f}, Point3D{0.0f, 1.0f, 0.0f},
                                    1.04719755f, 1280.0f / 720.0f, 0.5f, 50.0f, corners);
        bool cornersFinite = true;
        for (int i = 0; i < 8; ++i) {
            if (!std::isfinite(corners[i].x) || !std::isfinite(corners[i].y) || !std::isfinite(corners[i].z)) {
                cornersFinite = false;
            }
        }
        const bool ok = inFront && !behind && !farAway && aabbIn && !aabbOut && cornersFinite;
        outReport.Record("CORE", "FRUSTUM_CULLING_MATH", ok,
                         ok ? "6 PLANOS + SPHERE/AABB + CORNERS OK" : "FALLO FRUSTUM CULLING");
    }

    // 6. StacklessBVHBuilder (12 esferas -> <= 24 nodos Pre-Order, cobertura total de hojas, skipIndex válido)
    {
        std::array<StacklessBVHBuilder::SpherePrim, 12> spheres{};
        for (uint32_t i = 0u; i < 12u; ++i) {
            const float fi = static_cast<float>(i);
            spheres[i] = {Point3D{fi * 1.5f - 8.0f, std::sin(fi), std::cos(fi) * 2.0f}, 0.6f, {1.0f, 1.0f, 1.0f, 0.5f}};
        }
        StacklessBVHBuilder::BuildResult bvh{};
        StacklessBVHBuilder::Build(spheres.data(), 12u, bvh);

        uint32_t coveredPrims = 0u;
        bool nodesValid = (bvh.primCount == 12u) && (bvh.nodeCount > 0u) && (bvh.nodeCount <= StacklessBVHBuilder::MAX_NODES);
        for (uint32_t n = 0u; n < bvh.nodeCount && nodesValid; ++n) {
            const float payload = bvh.packedNodes[n * 8u + 3u];
            const float pCount  = bvh.packedNodes[n * 8u + 7u];
            if (pCount > 0.5f) {
                const uint32_t pc = static_cast<uint32_t>(pCount + 0.5f);
                const uint32_t fp = static_cast<uint32_t>(payload + 0.5f);
                if (fp + pc > bvh.primCount) nodesValid = false;
                coveredPrims += pc;
            } else {
                const uint32_t skip = static_cast<uint32_t>(payload + 0.5f);
                if (skip <= n || skip > bvh.nodeCount) nodesValid = false;
            }
        }
        const bool ok = nodesValid && (coveredPrims == 12u);
        outReport.Record("CORE", "STACKLESS_BVH_BUILDER", ok,
                         ok ? "BVH 12 PRIMS PRE-ORDER OK" : "FALLO STACKLESS BVH");
    }

    // 7. [OPT-461] EngineProfiler CPU por zona Zero-Alloc (RAII + BeginFrame + EMA)
    {
        auto& prof = EngineProfiler::Instance();
        prof.ResetAll();
        {
            NELEO_PROFILE_SCOPE(ProfileZone::Render3D);
            volatile uint32_t acc = 0u;
            for (uint32_t i = 0u; i < 1024u; ++i) {
                acc += i;
            }
            (void)acc;
        }
        prof.AddZoneTicks(ProfileZone::QueueFlush, 100u);
        const uint64_t ticksBeforeFlush = prof.GetCurrentTicks(ProfileZone::QueueFlush);
        prof.BeginFrame();
        const uint64_t ticksAfterFlush = prof.GetCurrentTicks(ProfileZone::QueueFlush);
        const bool ok = (ticksBeforeFlush == 100u) &&
                        (ticksAfterFlush == 0u) &&
                        (prof.GetFrameIndex() == 1u) &&
                        (prof.GetLastUs(ProfileZone::Render3D) >= 0.0f) &&
                        (prof.GetSmoothedUs(ProfileZone::QueueFlush) >= 0.0f);
        prof.ResetAll();
        outReport.Record("CORE", "ENGINE_PROFILER_ZONES", ok,
                         ok ? "CPU PROFILER ZERO-ALLOC OK" : "FALLO ENGINE PROFILER");
    }

    // 8. [OPT-466] JobSystem Multi-Core SMP + ParallelFor Zero-Alloc
    {
        auto& js = JobSystem::Instance();
        const bool wasInit = js.IsInitialized();
        if (!wasInit) {
            js.Init();
        }
        const bool integrityOk = JobSystem::VerifyJobSystemIntegrity();
        const bool workersOk = js.GetWorkerCount() >= 1u;

        constexpr uint32_t kN = 1024u;
        std::array<uint32_t, kN> data{};
        js.ParallelFor(
            kN,
            64u,
            [](uint32_t startIdx, uint32_t endIdx, void* userData) {
                auto* arr = static_cast<uint32_t*>(userData);
                for (uint32_t i = startIdx; i < endIdx; ++i) {
                    arr[i] = i * 3u + 1u;
                }
            },
            data.data());

        bool parallelOk = true;
        for (uint32_t i = 0u; i < kN; ++i) {
            if (data[i] != (i * 3u + 1u)) {
                parallelOk = false;
                break;
            }
        }
        if (!wasInit) {
            js.Shutdown();
        }

        const bool ok = integrityOk && workersOk && parallelOk;
        outReport.Record("CORE", "JOB_SYSTEM_PARALLEL_FOR", ok,
                         ok ? "SMP MULTI-CORE PARALLEL_FOR OK" : "FALLO JOB SYSTEM PARALLEL_FOR");
    }

    // 9. LocalizationTable + LocalizationManager aislado (0 side-effects sobre observadores vivos)
    {
        static LocalizationTable s_isolatedTable;
        const size_t parsed = s_isolatedTable.ParseFromMemory(
            "# comment\n"
            "test.hello=HOLA\n"
            "test.world=MUNDO\n"
        );
        const bool tableOk = (parsed == 2u) &&
                             (s_isolatedTable.Find(HashLocKeyLiteral("test.hello")) == "HOLA") &&
                             (s_isolatedTable.Find(HashLocKeyLiteral("test.world")) == "MUNDO") &&
                             s_isolatedTable.Find(HashLocKeyLiteral("test.missing")).empty();
        s_isolatedTable.Clear();

        const char* locReason = "FALLO LOCALIZATION I18N";
        const bool mgrOk = LocalizationManager::VerifyIntegrity(&locReason);
        const bool ok = tableOk && mgrOk;
        outReport.Record("CORE", "LOCALIZATION_I18N", ok,
                         ok ? "I18N ES/EN/FALLBACK/OBS OK" : (tableOk ? locReason : "FAIL:TABLE_PARSE"));
    }
}

} // namespace Neleo::Diagnostics
