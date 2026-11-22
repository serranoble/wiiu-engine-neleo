#include "Core/Engine.hpp"
#include "Core/ApplicationLifecycle.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Core/Config/CVarRegistry.hpp"
#include "Core/Storage/StorageManager.hpp"
#include "Core/Storage/VirtualFileSystem.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneLoading.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Audio/AudioEngine3D.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/ResourceMonitor.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/ShadowMapRendererGX2.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/RHI/RHIPipelineState.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/RHI/RHIRenderPass.hpp"
#include "Graphics/RHI/DisplayCompositor.hpp"
#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/AudioEmitterComponent.hpp"
#include "Core/ECS/Components/KinematicCharacterComponent.hpp"
#include "Core/ECS/Systems/AudioSpatialSystem.hpp"
#include "Core/ECS/Systems/KinematicCharacterSystem.hpp"
#include "Core/JobSystem.hpp"
#include "Net/NetSocket.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/event.h>
#include <proc_ui/procui.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/systeminfo.h>
#else
#include <SDL2/SDL.h>
#endif

namespace WiiU {

namespace {
uint32_t s_engineConfigObserverId = 0;

bool VerifyCrossSubsystemIntegrity() {
    // Pruebas de integracion entre subsistemas (Audio, RHI, KCC) alojadas en engine_framework para mantener DAG aciclico.
    ECSWorld testWorld;
    Entity e = testWorld.CreateEntity();
    auto& tf = testWorld.EmplaceComponent<TransformComponent>(e, Point3D{5.0f, 10.0f, 15.0f});
    auto& ac = testWorld.EmplaceComponent<AudioEmitterComponent>(e);
    (void)tf;
    AudioSpatialSystem::Update(testWorld);
    if (ac.emitter.position.x != 5.0f || ac.emitter.position.y != 10.0f || ac.emitter.position.z != 15.0f) {
        return false;
    }

    auto& rhiCache = RHIPipelineCache::Instance();
    rhiCache.Invalidate();
    rhiCache.ResetStats();
    RHIPipelineState stateA(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, true, 0);
    rhiCache.BindState(stateA);
    rhiCache.BindState(stateA);
    if (rhiCache.GetStateChanges() != 1 || rhiCache.GetRedundantBypasses() != 1) {
        rhiCache.Invalidate();
        return false;
    }
    rhiCache.Invalidate();

    RHIPipelineState invalidDepth(true, true, 8, BlendMode::Opaque);
    if (invalidDepth.Validate() != RHIValidationResult::InvalidDepthFunc) {
        return false;
    }

    RHIRenderPass::ResetStats();
    RHIRenderPassDesc passDesc{};
    passDesc.name = "SelfTest";
    {
        ScopedRenderPass passScope(passDesc);
        if (!RHIRenderPass::IsInsidePass() || RHIRenderPass::GetPassCount() != 1) {
            return false;
        }
    }
    if (RHIRenderPass::IsInsidePass()) {
        return false;
    }

    auto& comp = DisplayCompositor::Instance();
    comp.SetMode(CompositorMode::TVOnly);
    comp.CycleMode();
    if (comp.GetMode() != CompositorMode::DRCOnly) return false;
    comp.SetMode(CompositorMode::TVOnly);

    Entity player = testWorld.CreateEntity();
    auto& pTrans = testWorld.EmplaceComponent<TransformComponent>(player, Point3D{0.0f, 10.0f, 0.0f});
    auto& pKcc = testWorld.EmplaceComponent<KinematicCharacterComponent>(player, 0.35f, 1.75f, 4.0f, 7.5f, 5.8f, 16.0f);
    KinematicCharacterSystem::Update(testWorld, 0.02f);
    if (pKcc.velocity.y >= 0.0f || pKcc.isGrounded) return false;

    const float g0 = KinematicCharacterSystem::GetTerrainHeight(0.0f, 0.0f);
    pTrans.position = Point3D{0.0f, g0 + 0.875f, 0.0f};
    pKcc.velocity = Point3D{0.0f, 0.0f, 0.0f};
    KinematicCharacterSystem::Update(testWorld, 0.02f);
    if (!pKcc.isGrounded || pKcc.velocity.y != 0.0f) return false;

    return true;
}
} // namespace

Engine::Engine()
    : m_window(DEFAULT_WINDOW_TITLE, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT) {}

Engine::~Engine() {
    Shutdown();
}

bool Engine::Init(std::unique_ptr<IScene> initialScene) {
    if (m_initialized) {
        return true;
    }
#if !defined(__WIIU__) && !defined(__wiiu__)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) < 0) {
        return false;
    }
#endif
    m_initialized = true;

    if (!m_window.Init()) {
        Shutdown();
        return false;
    }

    // Autodiagnóstico de arquitectura de memoria determinista (ResourcePool, SceneArena, FrameArena)
    if (!MemoryManager::Instance().VerifyMemoryIntegrity()) {
        Shutdown();
        return false;
    }

    // Inicialización y autodiagnóstico del Job System multihilo en Core 2 ANTES de VerifyECSIntegrity()
    JobSystem::Instance().Init();
    if (!JobSystem::VerifyJobSystemIntegrity()) {
        Shutdown();
        return false;
    }

    // Autodiagnóstico de arquitectura ECS C++20 orientada a datos (engine_core puro)
    if (!ECSWorld::VerifyECSIntegrity()) {
        Shutdown();
        return false;
    }

    // Autodiagnóstico de integración entre subsistemas (Audio, RHI, KCC)
    if (!VerifyCrossSubsystemIntegrity()) {
        Shutdown();
        return false;
    }
#if defined(__WIIU__) || defined(__wiiu__)
    // Inicializar el pipeline 2D GX2 nativo después de las pruebas RHI de VerifyCrossSubsystemIntegrity()
    if (!Render2DGX2::Instance().Init()) {
        Shutdown();
        return false;
    }
#endif

    // Autodiagnóstico del pipeline de modelos 3D y formato binario .wmesh (OPT-293)
    if (!StaticMesh::VerifyMeshIntegrity()) {
        Shutdown();
        return false;
    }

    // Autodiagnóstico del toolkit de interfaz estandarizado y alineación de píxeles (OPT-312)
    if (!UI::UIToolkit::VerifyUIToolkitIntegrity()) {
        Shutdown();
        return false;
    }

    // Inicialización y autodiagnóstico del subsistema de redes (Jason Gregory Cap. 20 - OPT-443)
    if (!NetPlatform::Initialize() || !NetPlatform::VerifyNetIntegrity()) {
        Shutdown();
        return false;
    }

    InputManager::Instance().Init();
    // Inyección DIP de callbacks de audio (rompe ciclos engine_input/engine_physics <-> engine_audio)
    InputManager::Instance().SetActionSoundCallback([](void*) {
        AudioManager::Instance().PlayClick();
    }, nullptr);
    KinematicCharacterSystem::SetFootstepCallback([](const Point3D& pos, void*) {
        AudioEngine3D::Instance().PlaySound3D(
            AudioEngine3D::Instance().GetFootstepClip(),
            AudioEmitter3D{pos, Point3D{0.0f, 0.0f, 0.0f}, 1.0f, 15.0f, 1.0f, 0.40f, 1.0f, 0.0f, false, 70}
        );
    }, nullptr);

    // [Hito 125 - OPT-396 - Jason Gregory Cap. 5.2] Capa de Abstracción de Almacenamiento (SAL) + VFS + CVars + ALSM
    StorageManager::Initialize();
    VirtualFileSystem::Instance().Initialize();
    CVarRegistry::Instance().RegisterDefaultEngineCVars();
    ApplicationLifecycle::Instance().Initialize();

    // [Hito 124 - Jason Gregory Cap. 5.3] Suscripción pasiva de subsistemas a EngineConfig (DIP desacoplado)
    if (s_engineConfigObserverId == 0) {
        s_engineConfigObserverId = EngineConfig::Instance().RegisterChangeCallback([](const EngineConfig& cfg, void*) {
            // 1. Sincronizar Audio (Maestro, BGM y SFX)
            auto& audioMgr = AudioManager::Instance();
            audioMgr.SetMasterVolume(cfg.audio.masterVolume);
            audioMgr.SetMusicEnabled(cfg.audio.bgmEnabled);
            audioMgr.SetBGMVolume(cfg.audio.bgmVolume);
            audioMgr.SetSFXEnabled(cfg.audio.sfxEnabled);
            audioMgr.SetSFXVolume(cfg.audio.sfxVolume);

            // 2. Sincronizar SceneManager (VSync / Límite FPS)
            auto& sceneMgr = SceneManager::Instance();
            sceneMgr.SetFPSLimitEnabled(cfg.graphics.vsyncEnabled);

            // 3. Sincronizar OSD Telemetría
            ResourceMonitor::Instance().SetVisible(cfg.graphics.showOSDTelemetry);

            // 4. Sincronizar Filtrado de Texturas en Silicio GX2
            ForwardRendererGX2::Instance().SetTextureFilterMode(cfg.graphics.textureFilter);
        }, nullptr);
    }

    // Carga y aplicación de configuración global del motor
    EngineConfig::Instance().LoadFromFile();
    EngineConfig::Instance().ApplyToSubsystems();

    // Cargar la escena inicial personalizada o la pantalla de carga interactiva por defecto
    if (initialScene) {
        SceneManager::Instance().ChangeScene(std::move(initialScene));
    } else {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneLoading>());
    }

    m_running = true;
    return true;
}

void Engine::Run() {
#if defined(__WIIU__) || defined(__wiiu__)
    const uint64_t rawFreq = static_cast<uint64_t>(OSTimerClockSpeed);
    const uint64_t perfFreq = (rawFreq > 0) ? rawFreq : 1ULL;
    uint64_t lastPerfCounter = static_cast<uint64_t>(OSGetSystemTime());
#else
    Uint64 perfFreq = SDL_GetPerformanceFrequency();
    if (perfFreq == 0) perfFreq = 1;
    Uint64 lastPerfCounter = SDL_GetPerformanceCounter();
#endif

    float dtHistory[4] = { 0.016667f, 0.016667f, 0.016667f, 0.016667f };
    int dtIdx = 0;

    while (m_running) {
        // [OPT-43] Liberacion O(1) del arena efimera de cuadro al inicio de cada iteracion
        MemoryManager::Instance().GetFrameArena().Reset();

#if defined(__WIIU__) || defined(__wiiu__)
        uint64_t currentPerfCounter = static_cast<uint64_t>(OSGetSystemTime());
#else
        Uint64 currentPerfCounter = SDL_GetPerformanceCounter();
#endif
        float rawDt = static_cast<float>(currentPerfCounter - lastPerfCounter) / static_cast<float>(perfFreq);
        lastPerfCounter = currentPerfCounter;

        if (rawDt > 0.05f) rawDt = 0.05f;
        if (rawDt < 0.0001f) rawDt = 0.0001f;

        // Filtro de Media Móvil (4 cuadros) para suavizar micro-variaciones de jitter en dt
        dtHistory[dtIdx] = rawDt;
        dtIdx = (dtIdx + 1) & 3;
        float smoothedDt = (dtHistory[0] + dtHistory[1] + dtHistory[2] + dtHistory[3]) * 0.25f;

        InputManager::Instance().Update(m_running, smoothedDt);
        ApplicationLifecycle::Instance().PollPlatformState(!m_running || InputManager::Instance().IsExitRequested());
        if (!ApplicationLifecycle::Instance().ShouldContinueRunning()) {
            m_running = false;
        }
        if (!m_running) {
            // No actualices ni presentes otro frame después de que la plataforma/Cafe OS solicite salir.
            break;
        }

        if (ApplicationLifecycle::Instance().IsFrameAbortRequested()) {
            // Cuando la aplicación está en segundo plano (Menú HOME activo), ceder CPU pasivamente
#if defined(__WIIU__) || defined(__wiiu__)
            OSSleepTicks(OSMillisecondsToTicks(16));
#else
            SDL_Delay(16);
#endif
            continue;
        }

        ResourceMonitor::Instance().BeginFrame();

        Update(smoothedDt);

        ResourceMonitor::Instance().MarkCPUEnd();

        Render();

        ResourceMonitor::Instance().MarkGPUEnd();
    }
}

void Engine::Update(float dt) {
    if (InputManager::Instance().IsResourceMonitorRequested()) {
        ResourceMonitor::Instance().Toggle();
        EngineConfig::Instance().graphics.showOSDTelemetry = ResourceMonitor::Instance().IsVisible();
        InputManager::Instance().ConsumeResourceMonitorRequest();
    }
    ResourceMonitor::Instance().Update(dt);
    AudioManager::Instance().UpdateStreaming();
    SceneManager::Instance().Update(dt);
}

void Engine::Render() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!Window::HasForeground()) {
        return;
    }
#endif
    const bool isTransitioning = SceneManager::Instance().IsTransitioning();
    const IScene* currentScene = SceneManager::Instance().GetCurrentScene();
    const bool sceneClearsOwn = (!isTransitioning && currentScene != nullptr && currentScene->ClearsFramebuffer());
    const bool fpsLimited = SceneManager::Instance().IsFPSLimitEnabled();

    // Dirty-checking para evitar bombardear registros de silicio en cada fotograma
    const int targetSwap = fpsLimited ? 1 : 0;
    if (m_lastSwapInterval != targetSwap) {
        m_lastSwapInterval = targetSwap;
        m_window.SetSwapInterval(targetSwap);
    }

    if (!sceneClearsOwn) {
        m_window.Clear(ColorRGBA::Black());
    }

    SceneManager::Instance().Render();

    // Renderizar Monitor de Recursos OSD si está visible
    if (ResourceMonitor::Instance().IsVisible()) {
        ResourceMonitor::Instance().Render();
    }

#if defined(__WIIU__) || defined(__wiiu__)
    Render2DGX2::Instance().NextFrame();
#endif

    m_window.Present();
}

void Engine::Shutdown() {
    if (!m_initialized) {
        return;
    }
    m_initialized = false;
    m_running = false;

    // Destruye primero la escena activa para unir sus workers antes de apagar audio y video.
    InputManager::Instance().SetActionSoundCallback(nullptr, nullptr);
    KinematicCharacterSystem::SetFootstepCallback(nullptr, nullptr);
    RumbleManager::Instance().Shutdown(); // [AUDITORÍA SILICIO]: Apagado inmediato y seguro de hápticos
    if (s_engineConfigObserverId != 0) {
        EngineConfig::Instance().UnregisterChangeCallback(s_engineConfigObserverId);
        s_engineConfigObserverId = 0;
    }
    m_lastSwapInterval = -1;
    SceneManager::Instance().Clear();

#if defined(__WIIU__) || defined(__wiiu__)
    // Drenar GPU ANTES de liberar renderizadores/shaders/VRAM si la app sigue en primer plano.
    if (ProcUIIsRunning() && Window::HasForeground() && !Window::IsExitingProcUI() && !ProcUIInShutdown()) {
        GX2DrawDone();
    }
#endif

    Render2DGX2::Instance().Shutdown();
    ShadowMapRendererGX2::Instance().Shutdown();
    ForwardRendererGX2::Instance().Shutdown();
    AudioManager::Instance().Shutdown();
    NetPlatform::Shutdown();
    VirtualFileSystem::Instance().Shutdown();
    StorageManager::Shutdown();       // Drenar guardados asíncronos ANTES de detener Core 2
    JobSystem::Instance().Shutdown();
    ApplicationLifecycle::Instance().Shutdown();

    m_window.Shutdown();
    MemoryManager::Instance().AssertNoLeaks();
#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIIsRunning()) {
        ProcUIShutdown();
    }
#else
    SDL_Quit();
#endif
}

} // namespace WiiU
