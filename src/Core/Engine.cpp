#include "Core/Engine.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Core/Storage/StorageManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneLoading.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/GX2StateHelper.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/ResourceMonitor.hpp"
#include "Core/ECS/ECSWorld.hpp"
#include "Core/JobSystem.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/swap.h>
#include <gx2/event.h>
#include <proc_ui/procui.h>
extern "C" void* SDL_GetVideoDevice(void);
#endif

namespace WiiU {

Engine::Engine()
    : m_window(DEFAULT_WINDOW_TITLE, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT) {}

Engine::~Engine() {
    Shutdown();
}

bool Engine::Init(std::unique_ptr<IScene> initialScene) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) < 0) {
        return false;
    }

    if (!m_window.Init()) {
        return false;
    }

    // Autodiagnóstico de arquitectura de memoria determinista (ResourcePool, SceneArena, FrameArena)
    if (!MemoryManager::Instance().VerifyMemoryIntegrity()) {
        return false;
    }

    // Autodiagnóstico de arquitectura ECS C++20 orientada a datos (Entity, ComponentPool, Sparse-Set, Views)
    if (!ECSWorld::VerifyECSIntegrity()) {
        return false;
    }

    // Inicialización y autodiagnóstico del Job System multihilo en Core 2 (OPT-106 / OPT-97)
    JobSystem::Instance().Init();
    if (!JobSystem::VerifyJobSystemIntegrity()) {
        return false;
    }

    // Autodiagnóstico del pipeline de modelos 3D y formato binario .wmesh (OPT-293)
    if (!StaticMesh::VerifyMeshIntegrity()) {
        return false;
    }

    // Autodiagnóstico del toolkit de interfaz estandarizado y alineación de píxeles (OPT-312)
    if (!UI::UIToolkit::VerifyUIToolkitIntegrity()) {
        return false;
    }

    InputManager::Instance().Init();

    // [Hito 125 - OPT-396 - Jason Gregory Cap. 5.2] Capa de Abstracción de Almacenamiento (SAL)
    StorageManager::Initialize();

    // [Hito 124 - Jason Gregory Cap. 5.3] Carga y aplicación de configuración global del motor
    EngineConfig::Get().LoadFromFile();
    EngineConfig::Get().ApplyToSubsystems();

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
    Uint64 perfFreq = SDL_GetPerformanceFrequency();
    Uint64 lastPerfCounter = SDL_GetPerformanceCounter();

    float dtHistory[4] = { 0.016667f, 0.016667f, 0.016667f, 0.016667f };
    int dtIdx = 0;

    while (m_running) {
        Uint64 currentPerfCounter = SDL_GetPerformanceCounter();
        float rawDt = static_cast<float>(currentPerfCounter - lastPerfCounter) / static_cast<float>(perfFreq);
        lastPerfCounter = currentPerfCounter;

        if (rawDt > 0.05f) rawDt = 0.05f;
        if (rawDt < 0.0001f) rawDt = 0.0001f;

        // Filtro de Media Móvil (4 cuadros) para suavizar micro-variaciones de jitter en dt
        dtHistory[dtIdx] = rawDt;
        dtIdx = (dtIdx + 1) & 3;
        float smoothedDt = (dtHistory[0] + dtHistory[1] + dtHistory[2] + dtHistory[3]) * 0.25f;

        InputManager::Instance().Update(m_running, smoothedDt);
#if defined(__WIIU__) || defined(__wiiu__)
        if (ProcUIInShutdown() || !ProcUIIsRunning()) {
            m_running = false;
        }
#endif
        if (!m_running) {
            // No actualices ni presentes otro frame después de que SDL/Cafe OS solicite salir.
            break;
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
        InputManager::Instance().ConsumeResourceMonitorRequest();
    }
    ResourceMonitor::Instance().Update(dt);
    SceneManager::Instance().Update(dt);
}

void Engine::Render() {
    const bool isTransitioning = SceneManager::Instance().IsTransitioning();
    IScene* currentScene = SceneManager::Instance().GetCurrentScene();
    const bool isPureGX2 = (!isTransitioning && currentScene != nullptr && currentScene->IsPureGX2());
    SDL_Renderer* renderer = m_window.GetRenderer();
    const bool fpsLimited = SceneManager::Instance().IsFPSLimitEnabled();

#if defined(__WIIU__) || defined(__wiiu__)
    GX2SetSwapInterval(fpsLimited ? 1 : 0);
#endif
    if (renderer) {
        SDL_RenderSetVSync(renderer, fpsLimited ? 1 : 0);
    }

    // En escenas 100% GX2 nativas, se evita el Clear de SDL para que el silicio Latte
    // controle el borrado y renderizado directamente en el búfer de color sin conflictos.
    // Si transicionamos desde una escena GX2 nativa hacia una escena SDL2, restauramos de inmediato
    // el GX2ContextState y los shaders de SDL2 para evitar corrupción visual en los menús.
    if (!isPureGX2) {
        if (m_wasPureGX2 && renderer) {
            RestoreSDL2GX2State(renderer);
            m_wasPureGX2 = false;
        }
        m_window.Clear(ColorRGBA::Black());
    } else {
        m_wasPureGX2 = true;
    }

    SceneManager::Instance().Render(renderer);

    // Renderizar Monitor de Recursos OSD si está visible
    if (ResourceMonitor::Instance().IsVisible()) {
        ResourceMonitor::Instance().Render(renderer, isPureGX2);
    }

    m_window.Present();
}

void Engine::Shutdown() {
#if defined(__WIIU__) || defined(__wiiu__)
    // Compatibilidad con el backend Wii U de SDL: evita que VideoQuit relance el menú
    // o entre en un bucle esperando SYSLaunchMenu durante el cierre de Cafe OS.
    void* videoDevice = SDL_GetVideoDevice();
    if (videoDevice) {
        char* videoDeviceBytes = reinterpret_cast<char*>(videoDevice);
        char* driverdata = *reinterpret_cast<char**>(videoDeviceBytes + 1092);
        if (driverdata) {
            *reinterpret_cast<int*>(driverdata + 12) = 1; // exitingProcUI = 1
        }
    }
#endif

    // Destruye primero la escena activa para unir sus workers antes de apagar audio y video.
    RumbleManager::Instance().StopAll(); // [AUDITORÍA SILICIO]: Apagado inmediato de actuadores hápticos
    SceneManager::Instance().Clear();
    AudioManager::Instance().Shutdown();
    JobSystem::Instance().Shutdown();
    StorageManager::Shutdown();

#if defined(__WIIU__) || defined(__wiiu__)
    // Solo drenar GPU si la aplicación sigue activa en primer plano (posee acceso al silicio Latte).
    // Si el usuario solicitó salir vía Botón Home -> Cerrar Programa, Cafe OS ya envió la app
    // a segundo plano (PROCUI_STATUS_IN_BACKGROUND / PROCUI_STATUS_EXITING); llamar a GX2DrawDone()
    // en segundo plano bloquearía indefinidamente el hilo del procesador Espresso.
    if (ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown()) {
        GX2DrawDone();
    }
#endif

    m_window.Shutdown();
    SDL_Quit();
}

} // namespace WiiU
