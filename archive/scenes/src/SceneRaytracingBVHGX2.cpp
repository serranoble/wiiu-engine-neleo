#include "Scene/SceneRaytracingBVHGX2.hpp"
#include "Scene/SceneManager.hpp"
#include "Core/JobSystem.hpp"
#include "Core/StackString.hpp"
#include "Input/InputManager.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include "Graphics/RaytracingBVHShaderBinary.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace WiiU {

namespace {

// Botones táctiles en la franja inferior de 1280x720 (alineados a píxel entero)
constexpr UI::UIRect BTN_RES     {   14.0f, 654.0f, 182.0f, 46.0f };
constexpr UI::UIRect BTN_BVH     {  206.0f, 654.0f, 160.0f, 46.0f };
constexpr UI::UIRect BTN_HEATMAP {  376.0f, 654.0f, 176.0f, 46.0f };
constexpr UI::UIRect BTN_SHADOWS {  562.0f, 654.0f, 190.0f, 46.0f };
constexpr UI::UIRect BTN_WIRE    {  762.0f, 654.0f, 176.0f, 46.0f };
constexpr UI::UIRect BTN_PAUSE   {  948.0f, 654.0f, 148.0f, 46.0f };
constexpr UI::UIRect BTN_EXIT    { 1106.0f, 654.0f, 160.0f, 46.0f };

} // namespace

void SceneRaytracingBVHGX2::BVHParallelJobTask(void* userData) {
    auto* params = static_cast<BVHParallelJobParams*>(userData);
    if (!params || !params->self) return;

    SceneRaytracingBVHGX2* self = params->self;
    if (params->animateSpheres) {
        self->AdvanceSpheres();
        self->RebuildBVHAndUniforms();
    }
    self->m_visibleEntities = CullingSystem::UpdateWithBVH(self->m_ecsWorld, self->m_bvh, params->camera);
}

void SceneRaytracingBVHGX2::LoadAsync() {
    // Ejecutado en Core 2 durante TransitionPhase::LoadingScene mientras Core 1 dibuja la barra a 60 FPS
    (void)m_fullscreenShader.PreloadBinary("shaders/raytracing_bvh.gsh");
    m_bvh.Reserve(SPHERE_COUNT);
    InitSpheres();
    RebuildBVHAndUniforms();
    m_asyncPrepared = true;
}

void SceneRaytracingBVHGX2::OnGameInit() {
    SetClearColor({12, 16, 28, 255});
    SetCameraProjection(CAMERA_FOV_DEG, 0.20f, 200.0f);

    m_orbitCamera.Initialize();
    m_orbitCamera.SetTarget(Point3D(0.0f, 1.35f, 0.0f));
    m_orbitCamera.SetAngles(22.0f, 18.0f);
    m_orbitCamera.SetDistance(9.2f);
    m_orbitCamera.SetLeftStickZoomEnabled(false);

    if (!m_asyncPrepared) {
        m_bvh.Reserve(SPHERE_COUNT);
        InitSpheres();
        RebuildBVHAndUniforms();
    }

    m_animTime = 0.0f;
    m_bvhEnabled = true;
    m_heatmapEnabled = false;
    m_showBvhWireframe = true;
    m_paused = false;
    m_shadowMode = 2;
    m_resIndex = 2;
    m_prevTouchDown = false;
    m_exitRequested = false;

    m_sunDir = m_lightPos.Normalized();
    m_orbitCamera.Update(m_ecsWorld, 0.0f);
    UpdateCameraBasis();
    ComputeAccelerationBounds();

    if (m_fullscreenShader.Initialize("shaders/raytracing_bvh.gsh",
                                      RAYTRACING_BVH_GSH_DATA,
                                      RAYTRACING_BVH_GSH_SIZE)) {
        (void)m_fullscreenShader.EnableOffscreenScaler(1280u, 720u);
        auto resolveUniform = [this](const char* name, int32_t defaultOffset) noexcept -> int32_t {
            const int32_t reflected = m_fullscreenShader.GetPixelUniformOffset(name);
            return (reflected >= 0) ? reflected : defaultOffset;
        };

        m_uniforms.camPos            = resolveUniform("uCamPos", 0);
        m_uniforms.camForward        = resolveUniform("uCamForward", 4);
        m_uniforms.camRight          = resolveUniform("uCamRight", 8);
        m_uniforms.camUp             = resolveUniform("uCamUp", 12);
        m_uniforms.lightPos          = resolveUniform("uLightPos", 16);
        m_uniforms.renderFlags       = resolveUniform("uRenderFlags", 20);
        m_uniforms.sunDirAndNodes    = resolveUniform("uSunDirAndNodes", 24);
        m_uniforms.screenBounds      = resolveUniform("uScreenBounds", 28);
        m_uniforms.shadowFloorBounds = resolveUniform("uShadowFloorBounds", 32);
        m_uniforms.spheres           = resolveUniform("uSpheres", 36);
        m_uniforms.sphereColors      = resolveUniform("uSphereColors", 84);
        m_uniforms.bvhNodes          = resolveUniform("uBvhNodes", 132);
    }
}

void SceneRaytracingBVHGX2::OnGameExit() {
    m_fullscreenShader.Shutdown();
    m_ecsWorld.Clear();
    m_asyncPrepared = false;
}

void SceneRaytracingBVHGX2::InitSpheres() {
    struct PaletteSpec {
        float x, y, z, r;
        float speed, amp, phase;
        float cr, cg, cb, refl;
    };

    static constexpr PaletteSpec SPECS[SPHERE_COUNT] = {
        {  0.00f, 1.35f,  0.00f, 0.95f, 0.9f, 0.25f, 0.0f, 0.92f, 0.94f, 0.98f, 0.85f }, // Cromada central
        { -2.40f, 1.15f, -1.20f, 0.72f, 1.3f, 0.55f, 0.7f, 0.96f, 0.75f, 0.22f, 0.65f }, // Oro
        {  2.40f, 1.25f, -1.10f, 0.70f, 1.1f, 0.50f, 1.4f, 0.92f, 0.20f, 0.28f, 0.45f }, // Rubí
        { -1.55f, 0.90f,  2.10f, 0.65f, 1.5f, 0.45f, 2.1f, 0.18f, 0.84f, 0.46f, 0.40f }, // Esmeralda
        {  1.75f, 1.05f,  2.05f, 0.68f, 1.2f, 0.48f, 2.8f, 0.20f, 0.54f, 0.96f, 0.55f }, // Zafiro
        { -3.80f, 1.65f,  0.90f, 0.58f, 1.7f, 0.65f, 3.5f, 0.88f, 0.45f, 0.92f, 0.50f }, // Amatista
        {  3.85f, 1.70f,  0.80f, 0.58f, 1.6f, 0.60f, 4.2f, 0.22f, 0.88f, 0.86f, 0.60f }, // Cian
        {  0.00f, 2.45f, -2.85f, 0.76f, 1.0f, 0.40f, 4.9f, 0.95f, 0.52f, 0.16f, 0.35f }, // Ámbar
        { -1.20f, 2.25f, -0.45f, 0.48f, 2.0f, 0.55f, 5.4f, 0.96f, 0.96f, 0.96f, 0.78f }, // Perla espejo
        {  1.25f, 2.15f, -0.50f, 0.46f, 1.9f, 0.55f, 0.4f, 0.16f, 0.20f, 0.26f, 0.88f }, // Obsidiana espejo
        { -2.95f, 0.75f, -2.90f, 0.54f, 1.4f, 0.35f, 1.9f, 0.65f, 0.94f, 0.24f, 0.32f }, // Lima
        {  2.95f, 0.75f, -2.80f, 0.54f, 1.4f, 0.35f, 3.3f, 0.98f, 0.36f, 0.66f, 0.38f }  // Coral
    };

    m_ecsWorld.Clear();
    for (size_t i = 0; i < SPHERE_COUNT; ++i) {
        const auto& s = SPECS[i];
        SphereInstance& inst = m_spheres[i];
        inst.baseCenter = Point3D(s.x, s.y, s.z);
        inst.currentCenter = inst.baseCenter;
        inst.radius = s.r;
        inst.speed = s.speed;
        inst.orbitAmp = s.amp;
        inst.phase = s.phase;
        inst.colorR = s.cr;
        inst.colorG = s.cg;
        inst.colorB = s.cb;
        inst.reflectivity = s.refl;

        Entity ent = m_ecsWorld.CreateEntity();
        m_sphereEntities[i] = ent;

        auto& tf = m_ecsWorld.AddComponent<TransformComponent>(ent);
        tf.position = inst.currentCenter;
        tf.scale = Point3D(1.0f, 1.0f, 1.0f);
        tf.worldMatrix = Matrix4x4::Translation(inst.currentCenter.x, inst.currentCenter.y, inst.currentCenter.z);

        auto& mesh = m_ecsWorld.AddComponent<MeshComponent>(ent);
        mesh.isVisible = true;

        auto& bv = m_ecsWorld.AddComponent<BoundingVolumeComponent>(ent);
        bv.center = Point3D(0.0f, 0.0f, 0.0f);
        bv.radius = s.r;
        bv.enableFrustumCulling = true;
    }
}

void SceneRaytracingBVHGX2::AdvanceSpheres() noexcept {
    for (size_t i = 0; i < SPHERE_COUNT; ++i) {
        SphereInstance& s = m_spheres[i];
        const float t = m_animTime * s.speed + s.phase;
        s.currentCenter.x = s.baseCenter.x + std::sin(t * 0.85f) * s.orbitAmp;
        s.currentCenter.y = s.baseCenter.y + std::fabs(std::sin(t)) * (s.orbitAmp * 0.9f);
        s.currentCenter.z = s.baseCenter.z + std::cos(t * 0.70f) * s.orbitAmp;
    }
}

void SceneRaytracingBVHGX2::RebuildBVHAndUniforms() {
    std::array<BVHPrimitive, SPHERE_COUNT> prims{};

    for (size_t i = 0; i < SPHERE_COUNT; ++i) {
        const SphereInstance& s = m_spheres[i];
        const float r = s.radius;
        prims[i].bounds = SpatialAABB(
            Point3D(s.currentCenter.x - r, s.currentCenter.y - r, s.currentCenter.z - r),
            Point3D(s.currentCenter.x + r, s.currentCenter.y + r, s.currentCenter.z + r)
        );
        prims[i].centroid = s.currentCenter;
        prims[i].userId = m_sphereEntities[i].id;

        if (auto* tf = m_ecsWorld.GetComponent<TransformComponent>(m_sphereEntities[i])) {
            tf->position = s.currentCenter;
            tf->worldMatrix = Matrix4x4::Translation(s.currentCenter.x, s.currentCenter.y, s.currentCenter.z);
        }
    }

    m_bvh.Build(prims.data(), SPHERE_COUNT);

    // Empaquetado Pre-Order Stackless para la GPU AMD Latte
    m_gpuBvhNodes.fill(0.0f);
    m_packedNodeCount = m_bvh.PackNodesForGPU(m_gpuBvhNodes.data(), MAX_GPU_BVH_NODES);

    // Ordenar esferas según m_bvh.GetPrimIndices() para acceso directo O(1) en hoja GLSL
    const auto& order = m_bvh.GetPrimIndices();
    for (size_t k = 0; k < SPHERE_COUNT; ++k) {
        const uint32_t srcIdx = (k < order.size()) ? order[k] : static_cast<uint32_t>(k);
        const SphereInstance& s = m_spheres[(srcIdx < SPHERE_COUNT) ? srcIdx : k];
        m_gpuSpheres[k * 4 + 0] = s.currentCenter.x;
        m_gpuSpheres[k * 4 + 1] = s.currentCenter.y;
        m_gpuSpheres[k * 4 + 2] = s.currentCenter.z;
        m_gpuSpheres[k * 4 + 3] = s.radius;

        m_gpuSphereColors[k * 4 + 0] = s.colorR;
        m_gpuSphereColors[k * 4 + 1] = s.colorG;
        m_gpuSphereColors[k * 4 + 2] = s.colorB;
        m_gpuSphereColors[k * 4 + 3] = s.reflectivity;
    }
}

void SceneRaytracingBVHGX2::UpdateCameraBasis() noexcept {
    m_camPos = m_orbitCamera.GetPosition();
    const Point3D camTarget = m_orbitCamera.GetTarget();
    m_camForward = (camTarget - m_camPos).Normalized();
    const Point3D worldUp = (std::fabs(m_camForward.y) > 0.999f)
                          ? Point3D(0.0f, 0.0f, 1.0f)
                          : Point3D(0.0f, 1.0f, 0.0f);
    m_camRight = Point3D::Cross(m_camForward, worldUp).Normalized();
    m_camUp    = Point3D::Cross(m_camRight, m_camForward).Normalized();
}

void SceneRaytracingBVHGX2::ComputeAccelerationBounds() noexcept {
    // 1. Pre-culling de pantalla (Screen-Space NDC Bounding Box de las 12 esferas)
    const float tanHalfFovY = std::tan(m_fovYRad * 0.5f);
    const float tanHalfFovX = tanHalfFovY * m_aspectRatio;

    float ndcMinX =  1.0f;
    float ndcMinY =  1.0f;
    float ndcMaxX = -1.0f;
    float ndcMaxY = -1.0f;
    bool fullScreenFallback = false;

    for (const SphereInstance& s : m_spheres) {
        const Point3D rel = s.currentCenter - m_camPos;
        const float cx = Point3D::Dot(rel, m_camRight);
        const float cy = Point3D::Dot(rel, m_camUp);
        const float cz = Point3D::Dot(rel, m_camForward);
        const float padR = s.radius * 1.15f + 0.08f;

        if (cz - padR <= 0.35f) {
            fullScreenFallback = true;
            break;
        }

        // Proyección conservadora de los 8 extremos del cubo envolvente en espacio cámara
        for (int corner = 0; corner < 8; ++corner) {
            const float vx = cx + ((corner & 1) ? padR : -padR);
            const float vy = cy + ((corner & 2) ? padR : -padR);
            const float vz = cz + ((corner & 4) ? padR : -padR);
            const float nx = vx / (vz * tanHalfFovX);
            const float ny = vy / (vz * tanHalfFovY);
            ndcMinX = std::min(ndcMinX, nx);
            ndcMinY = std::min(ndcMinY, ny);
            ndcMaxX = std::max(ndcMaxX, nx);
            ndcMaxY = std::max(ndcMaxY, ny);
        }
    }

    if (fullScreenFallback || ndcMaxX < -1.0f || ndcMinX > 1.0f || ndcMaxY < -1.0f || ndcMinY > 1.0f) {
        m_gpuScreenBounds = fullScreenFallback
            ? std::array<float, 4>{ -1.0f, -1.0f,  1.0f,  1.0f }
            : std::array<float, 4>{  2.0f,  2.0f, -2.0f, -2.0f }; // Todo fuera de pantalla
    } else {
        m_gpuScreenBounds = {
            std::clamp(ndcMinX, -1.0f, 1.0f),
            std::clamp(ndcMinY, -1.0f, 1.0f),
            std::clamp(ndcMaxX, -1.0f, 1.0f),
            std::clamp(ndcMaxY, -1.0f, 1.0f)
        };
    }

    // 2. Proyección de sombra sobre el plano de suelo y = -1.25 para podar traceShadow en suelo
    if (m_bvh.IsEmpty()) {
        m_gpuShadowFloorBounds = { -25.0f, -25.0f, 25.0f, 25.0f };
        return;
    }

    const SpatialAABB& root = m_bvh.GetNodes()[0].bounds;
    const float shadowPad = 1.5f * m_lightRadius + 0.65f;
    const Point3D bMin(root.min.x - shadowPad, root.min.y - shadowPad, root.min.z - shadowPad);
    const Point3D bMax(root.max.x + shadowPad, root.max.y + shadowPad, root.max.z + shadowPad);

    float minX =  1e30f;
    float minZ =  1e30f;
    float maxX = -1e30f;
    float maxZ = -1e30f;
    bool shadowFallback = false;

    for (int corner = 0; corner < 8; ++corner) {
        const float vx = (corner & 1) ? bMax.x : bMin.x;
        const float vy = (corner & 2) ? bMax.y : bMin.y;
        const float vz = (corner & 4) ? bMax.z : bMin.z;
        const float dy = vy - m_lightPos.y;
        if (dy >= -0.05f) {
            shadowFallback = true;
            break;
        }
        const float tFloor = (-1.25f - m_lightPos.y) / dy;
        const float fx = m_lightPos.x + tFloor * (vx - m_lightPos.x);
        const float fz = m_lightPos.z + tFloor * (vz - m_lightPos.z);
        minX = std::min(minX, fx);
        minZ = std::min(minZ, fz);
        maxX = std::max(maxX, fx);
        maxZ = std::max(maxZ, fz);
    }

    m_gpuShadowFloorBounds = shadowFallback
        ? std::array<float, 4>{ -45.0f, -45.0f, 45.0f, 45.0f }
        : std::array<float, 4>{ minX, minZ, maxX, maxZ };
}

void SceneRaytracingBVHGX2::CycleShadowMode() noexcept {
    m_shadowMode = (m_shadowMode + 1) % 3;
}

void SceneRaytracingBVHGX2::CycleResolution(int delta) noexcept {
    constexpr int count = static_cast<int>(RHIFullscreenShader::STANDARD_16X9_PRESET_COUNT);
    m_resIndex = (m_resIndex + delta % count + count) % count;
}

void SceneRaytracingBVHGX2::HandleControls(float dt) {
    auto& input = InputManager::Instance();
    const VirtualGamepad& pad = input.GetGamepad(0);

    if (pad.WasJustPressed(VirtualButton::B) || pad.WasJustPressed(VirtualButton::Plus) || input.IsBackRequested()) {
        m_exitRequested = true;
        return;
    }
    if (pad.WasJustPressed(VirtualButton::ZL) || pad.WasJustPressed(VirtualButton::DPadLeft)) {
        CycleResolution(-1);
    }
    if (pad.WasJustPressed(VirtualButton::ZR) || pad.WasJustPressed(VirtualButton::DPadRight)) {
        CycleResolution(1);
    }
    if (pad.WasJustPressed(VirtualButton::X)) {
        m_bvhEnabled = !m_bvhEnabled;
    }
    if (pad.WasJustPressed(VirtualButton::Y)) {
        m_heatmapEnabled = !m_heatmapEnabled;
    }
    if (pad.WasJustPressed(VirtualButton::A)) {
        CycleShadowMode();
    }
    if (pad.WasJustPressed(VirtualButton::R)) {
        m_showBvhWireframe = !m_showBvhWireframe;
    }
    if (pad.WasJustPressed(VirtualButton::L)) {
        m_paused = !m_paused;
    }

    if (std::fabs(pad.leftStick.y) > 0.18f) {
        m_orbitCamera.AdjustZoom(-pad.leftStick.y * 6.0f * dt);
    }

    const TouchState& touch = input.GetTouchState();
    if (touch.active && !m_prevTouchDown) {
        if (BTN_RES.Contains(touch.x, touch.y)) {
            CycleResolution(1);
        } else if (BTN_BVH.Contains(touch.x, touch.y)) {
            m_bvhEnabled = !m_bvhEnabled;
        } else if (BTN_HEATMAP.Contains(touch.x, touch.y)) {
            m_heatmapEnabled = !m_heatmapEnabled;
        } else if (BTN_SHADOWS.Contains(touch.x, touch.y)) {
            CycleShadowMode();
        } else if (BTN_WIRE.Contains(touch.x, touch.y)) {
            m_showBvhWireframe = !m_showBvhWireframe;
        } else if (BTN_PAUSE.Contains(touch.x, touch.y)) {
            m_paused = !m_paused;
        } else if (BTN_EXIT.Contains(touch.x, touch.y)) {
            m_exitRequested = true;
        }
    }
    m_prevTouchDown = touch.active;
}

void SceneRaytracingBVHGX2::OnGameUpdate(float dt) {
    HandleControls(dt);
    if (m_exitRequested) {
        SceneManager::Instance().NavigateToParentMenu(true);
        return;
    }

    m_orbitCamera.Update(m_ecsWorld, dt);
    UpdateCameraBasis();

    if (!m_paused) {
        m_animTime += dt;
        m_lightPos.x = std::sin(m_animTime * 0.55f) * 5.2f;
        m_lightPos.y = 7.2f + std::sin(m_animTime * 0.9f) * 0.8f;
        m_lightPos.z = std::cos(m_animTime * 0.55f) * 4.6f;
    }

    CameraComponent camComp;
    camComp.fov = CAMERA_FOV_DEG;
    camComp.aspectRatio = m_aspectRatio;
    camComp.nearClip = m_nearPlane;
    camComp.farClip = m_farPlane;
    camComp.target = m_orbitCamera.GetTarget();
    camComp.viewMatrix = GetViewMatrix();
    camComp.projMatrix = GetProjectionMatrix();
    camComp.viewProjMatrix = GetViewProjectionMatrix();

    m_jobParams.self = this;
    m_jobParams.camera = camComp;
    m_jobParams.animateSpheres = !m_paused;

    auto& jobs = JobSystem::Instance();
    m_smpActive = jobs.IsInitialized();

    // Paralelismo asimétrico Core 1 + Core 2:
    // Core 2: cinemática, SpatialBVH SAH Pre-Order Stackless y CullingSystem::UpdateWithBVH
    // Core 1: normalización solar mientras Core 2 reconstruye el BVH
    std::atomic<uint32_t> bvhJobCounter{0};
    if (m_smpActive) {
        jobs.Dispatch(&SceneRaytracingBVHGX2::BVHParallelJobTask, &m_jobParams, &bvhJobCounter);
    } else {
        BVHParallelJobTask(&m_jobParams);
    }

    m_sunDir = m_lightPos.Normalized();

    if (m_smpActive) {
        jobs.Wait(&bvhJobCounter);
    }

    // Cálculo final de rectángulo NDC y huella de sombra en suelo para acelerar GPU
    ComputeAccelerationBounds();
}

void SceneRaytracingBVHGX2::OnGameRender3D(ForwardRendererGX2& /*renderer*/) {
    if (!m_fullscreenShader.IsReady()) {
        return;
    }

    const float tanHalfFovY = std::tan(m_fovYRad * 0.5f);

    const float uCamPos[4]      = { m_camPos.x, m_camPos.y, m_camPos.z, tanHalfFovY };
    const float uCamForward[4]  = { m_camForward.x, m_camForward.y, m_camForward.z, m_aspectRatio };
    const float uCamRight[4]    = { m_camRight.x, m_camRight.y, m_camRight.z, m_animTime };
    const float uCamUp[4]       = { m_camUp.x, m_camUp.y, m_camUp.z, 0.0f };
    const float uLightPos[4]    = { m_lightPos.x, m_lightPos.y, m_lightPos.z, m_lightRadius };
    const float uRenderFlags[4] = {
        m_bvhEnabled ? 1.0f : 0.0f,
        static_cast<float>(m_shadowMode),
        m_heatmapEnabled ? 1.0f : 0.0f,
        static_cast<float>(SPHERE_COUNT)
    };
    const float uSunDirAndNodes[4] = {
        m_sunDir.x, m_sunDir.y, m_sunDir.z,
        static_cast<float>(m_packedNodeCount)
    };

    const auto& res = RHIFullscreenShader::STANDARD_16X9_PRESETS[m_resIndex];
    m_fullscreenShader.BindPipeline(res);
    m_fullscreenShader.SetPixelUniform4fv(m_uniforms.camPos, 4, uCamPos);
    m_fullscreenShader.SetPixelUniform4fv(m_uniforms.camForward, 4, uCamForward);
    m_fullscreenShader.SetPixelUniform4fv(m_uniforms.camRight, 4, uCamRight);
    m_fullscreenShader.SetPixelUniform4fv(m_uniforms.camUp, 4, uCamUp);
    m_fullscreenShader.SetPixelUniform4fv(m_uniforms.lightPos, 4, uLightPos);
    m_fullscreenShader.SetPixelUniform4fv(m_uniforms.renderFlags, 4, uRenderFlags);
    m_fullscreenShader.SetPixelUniform4fv(m_uniforms.sunDirAndNodes, 4, uSunDirAndNodes);
    m_fullscreenShader.SetPixelUniform4fv(m_uniforms.screenBounds, 4, m_gpuScreenBounds.data());
    m_fullscreenShader.SetPixelUniform4fv(m_uniforms.shadowFloorBounds, 4, m_gpuShadowFloorBounds.data());
    m_fullscreenShader.SetPixelUniform4fv(m_uniforms.spheres, static_cast<uint32_t>(SPHERE_COUNT * 4), m_gpuSpheres.data());
    m_fullscreenShader.SetPixelUniform4fv(m_uniforms.sphereColors, static_cast<uint32_t>(SPHERE_COUNT * 4), m_gpuSphereColors.data());
    m_fullscreenShader.SetPixelUniform4fv(m_uniforms.bvhNodes, static_cast<uint32_t>(MAX_GPU_BVH_NODES * 8), m_gpuBvhNodes.data());
    m_fullscreenShader.DrawQuad();
}

void SceneRaytracingBVHGX2::OnGameRenderDebug3D() {
    if (m_showBvhWireframe) {
        const auto& nodes = m_bvh.GetNodes();
        for (size_t i = 0; i < nodes.size(); ++i) {
            const BVHNode& node = nodes[i];
            if (i == 0) {
                DebugDraw3D::DrawAABB(node.bounds.min, node.bounds.max, 2.5f, ColorRGBA{255, 220, 60, 210});
            } else if (node.primCount > 0) {
                DebugDraw3D::DrawAABB(node.bounds.min, node.bounds.max, 1.8f, ColorRGBA{80, 255, 140, 220});
            } else {
                DebugDraw3D::DrawAABB(node.bounds.min, node.bounds.max, 1.4f, ColorRGBA{80, 210, 255, 150});
            }
        }
    }

    DebugDraw3D::DrawSphere(m_lightPos, 0.28f, 2.0f, ColorRGBA{255, 240, 100, 230}, 12);
}

void SceneRaytracingBVHGX2::OnGameRenderHUD() {
    static constexpr UI::UIRect TOP_PANEL{16.0f, 12.0f, 1248.0f, 66.0f};
    static constexpr UI::UIRect BOTTOM_BAR{0.0f, 644.0f, 1280.0f, 76.0f};
    static constexpr ColorRGBA TOP_BG{10, 14, 24, 205};
    static constexpr ColorRGBA TOP_BORDER{70, 190, 255, 220};
    static constexpr ColorRGBA BAR_BG{8, 10, 18, 215};
    static constexpr ColorRGBA BAR_BORDER{35, 65, 105, 190};

    UI::UIToolkit::DrawPanel(TOP_PANEL, &TOP_BG, &TOP_BORDER, 2.0f);
    UI::UIToolkit::DrawTextFit(30.0f, 20.0f, 1220.0f,
                               "CAP. 11: RAY TRACING BVH STACKLESS + SMP CORE 1/2 (GX2)",
                               UI::TextScale::Normal, ColorRGBA{120, 230, 255, 255});

    const auto& res = RHIFullscreenShader::STANDARD_16X9_PRESETS[m_resIndex];
    const char* shadowLabel = (m_shadowMode == 0) ? "OFF" : ((m_shadowMode == 1) ? "HARD" : "SOFT");
    StackString<160> statsLine;
    statsLine.Format("FPS: %.1f (%.1f ms) | RES: %ux%u->720p | BVH: %s (%u) | %s | FRUSTUM: %u/12 | SOMBRAS: %s",
                     GetFPS(), GetFrameTimeMs(),
                     static_cast<unsigned>(res.width),
                     static_cast<unsigned>(res.height),
                     m_bvhEnabled ? "STACKLESS" : "BRUTA",
                     static_cast<unsigned>(m_packedNodeCount),
                     m_smpActive ? "SMP C1+C2" : "1 CORE",
                     static_cast<unsigned>(m_visibleEntities),
                     shadowLabel);
    UI::UIToolkit::DrawTextFit(30.0f, 50.0f, 1220.0f, statsLine.c_str(),
                               UI::TextScale::Small, ColorRGBA{235, 240, 250, 255});

    UI::UIToolkit::DrawPanel(BOTTOM_BAR, &BAR_BG, &BAR_BORDER, 1.0f);

    auto drawBtn = [](const UI::UIRect& r, const char* label, bool active, const ColorRGBA& activeCol) {
        static constexpr ColorRGBA TEXT_WHITE{255, 255, 255, 255};
        const ColorRGBA fill = active ? activeCol : ColorRGBA{28, 34, 48, 230};
        const ColorRGBA border = active ? ColorRGBA{240, 250, 255, 255} : ColorRGBA{110, 130, 165, 220};
        UI::UIToolkit::DrawButton(r, label, active, UI::TextScale::Small, &fill, &border, &TEXT_WHITE);
    };

    StackString<32> resBtn;
    resBtn.Format("[ZL/ZR] RES: %s", res.shortLabel);
    drawBtn(BTN_RES, resBtn.c_str(), res.width < 1280u, ColorRGBA{50, 100, 170, 235});

    drawBtn(BTN_BVH,     m_bvhEnabled ? "[X] BVH: ON" : "[X] BVH: OFF", m_bvhEnabled, ColorRGBA{30, 120, 70, 235});
    drawBtn(BTN_HEATMAP, m_heatmapEnabled ? "[Y] HEATMAP: ON" : "[Y] HEATMAP: OFF", m_heatmapEnabled, ColorRGBA{175, 75, 25, 235});

    StackString<32> shBtn;
    shBtn.Format("[A] SOMBRAS: %s", shadowLabel);
    drawBtn(BTN_SHADOWS, shBtn.c_str(), m_shadowMode > 0, ColorRGBA{45, 95, 165, 235});

    drawBtn(BTN_WIRE,  m_showBvhWireframe ? "[R] CAJAS BVH: ON" : "[R] CAJAS BVH: OFF", m_showBvhWireframe, ColorRGBA{35, 130, 145, 235});
    drawBtn(BTN_PAUSE, m_paused ? "[L] REANUDAR" : "[L] PAUSAR", m_paused, ColorRGBA{150, 110, 30, 235});
    drawBtn(BTN_EXIT,  "[B] VOLVER MENU", false, ColorRGBA{140, 40, 40, 235});
}

} // namespace WiiU
