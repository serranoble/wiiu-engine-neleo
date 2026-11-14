#include "Scene/GameScene3D.hpp"
#include "Graphics/GX2StateHelper.hpp"
#include "Core/MemoryManager.hpp"
#include <cstring>

#if defined(__WIIU__) || defined(__wiiu__)
#include <coreinit/core.h>
#include <proc_ui/procui.h>
#include <gx2/mem.h>
#include <gx2/utils.h>
#include <gx2/registers.h>
#include <gx2/event.h>
#include <gx2/state.h>
#include <gx2/draw.h>
#include <gx2/clear.h>
#include <gx2r/surface.h>
#endif

namespace {

void DrainGPUQueueIfForeground() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIIsRunning() && ProcUIInForeground()) {
        GX2DrawDone();
    }
#endif
}

} // namespace

namespace WiiU {

GameScene3D::GameScene3D() = default;

GameScene3D::~GameScene3D() {
    // Destrucción determinista RAII: libera memoria en eDRAM y MEM1
    DestroyGX2Pipeline();
}

void GameScene3D::OnEnter() {
    // 1. Inicializar la tubería de hardware Latte GX2 (Z-Buffer + ContextState)
    InitGX2Pipeline();
    // 2. Invocar la inicialización específica del juego derivado
    OnGameInit();
}

void GameScene3D::OnExit() {
    // 1. Drenar la cola de GPU antes de destruir mallas/búferes en escenas derivadas
    DrainGPUQueueIfForeground();
    // 2. Invocar la limpieza específica del juego derivado
    OnGameExit();
    // 3. Liberar recursos de bajo nivel de silicio de forma segura
    DestroyGX2Pipeline();
}

void GameScene3D::Update(float dt) {
    // -------------------------------------------------------------------------
    // TELEMETRÍA DE RENDIMIENTO: Filtrado temporal de FPS y FrameTime
    // Fuente: Jason Gregory (Game Engine Architecture 3rd Ed., Cap. 7.5.3)
    // -------------------------------------------------------------------------
    m_fpsTimer += dt;
    ++m_frameCount;
    if (m_fpsTimer >= 0.5f) {
        m_fps = static_cast<float>(m_frameCount) / m_fpsTimer;
        m_frameTimeMs = (m_fps > 0.0f) ? (1000.0f / m_fps) : 16.6f;
        m_frameCount = 0;
        m_fpsTimer = 0.0f;
    }

    // Invocar actualización de lógica de juego polimórfica
    OnGameUpdate(dt);
}

Matrix4x4 GameScene3D::GetProjectionMatrix() const noexcept {
    // Matriz de proyección en perspectiva calculada en radianes
    return Matrix4x4::Perspective(m_fovYRad, m_aspectRatio, m_nearPlane, m_farPlane);
}

Matrix4x4 GameScene3D::GetViewProjectionMatrix() const noexcept {
    return GetProjectionMatrix() * GetViewMatrix();
}

void GameScene3D::Render(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    m_pColorBuffer = GetSDL2ColorBuffer(renderer);
    if (!m_nativeContextState || !m_pColorBuffer) {
        return;
    }

    // -------------------------------------------------------------------------
    // CONFIGURACIÓN DETERMINISTA DE CONTEXTO GPU LATTE GX2
    // -------------------------------------------------------------------------
    GX2SetContextState(m_nativeContextState);
    GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    if (m_depthBufferCreated) {
        GX2SetDepthBuffer(&m_depthBuffer);
    }
    // Viewport y Scissor canónicos para escaneo de 720p (1280x720)
    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);

    // Borrado de búferes de hardware (Color eDRAM y Z-Buffer D24_S8)
    float r = static_cast<float>(m_clearColor.r) / 255.0f;
    float g = static_cast<float>(m_clearColor.g) / 255.0f;
    float b = static_cast<float>(m_clearColor.b) / 255.0f;
    float a = static_cast<float>(m_clearColor.a) / 255.0f;
    GX2ClearColor(m_pColorBuffer, r, g, b, a);
    if (m_depthBufferCreated) {
        GX2ClearDepthStencilEx(&m_depthBuffer, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
    }

    // Preparación de matrices de cámara y actualización del estado de DebugDraw3D
    Matrix4x4 viewMatrix = GetViewMatrix();
    Matrix4x4 projMatrix = GetProjectionMatrix();
    Point3D camPos = m_orbitCamera.GetPosition();

    DebugDraw3D::SetMatrices(viewMatrix, projMatrix, 1280.0f, 720.0f, m_nearPlane);

    // -------------------------------------------------------------------------
    // FASE 1: PASE 3D PRINCIPAL CON FORWARD RENDERER GX2
    // Z-Buffer activo, shaders de iluminación Half-Lambert y Skinning LBS
    // -------------------------------------------------------------------------
    auto& fwdRenderer = ForwardRendererGX2::Instance();
    fwdRenderer.BeginFrame(camPos, viewMatrix, projMatrix);
    OnGameRender3D(fwdRenderer);
    fwdRenderer.EndFrame();

    // -------------------------------------------------------------------------
    // FASE 2: PASE 2D AISLADO MEDIANTE SCOPED UI PASS
    // Desactiva Z-Test y Z-Write, activa mezcla alfa y orquesta Debug 3D + HUD
    // Fuente: Jason Gregory (Cap. 14.5.3 "Render State Hygiene")
    // -------------------------------------------------------------------------
    {
        UI::ScopedUIPass uiPass(m_nativeContextState, m_pColorBuffer);
        OnGameRenderDebug3D();
        OnGameRenderHUD();
    }
#else
    (void)renderer;
#endif
}

bool GameScene3D::InitGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    // 1. Inicializar shaders, samplers y estado de ForwardRendererGX2
    bool rendererOk = ForwardRendererGX2::Instance().Initialize();

    // 2. Crear Búfer de Profundidad nativo en eDRAM (1280x720, D24_S8)
    if (!m_depthBufferCreated) {
        std::memset(&m_depthBuffer, 0, sizeof(GX2DepthBuffer));
        m_depthBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        m_depthBuffer.surface.width = 1280;
        m_depthBuffer.surface.height = 720;
        m_depthBuffer.surface.depth = 1;
        m_depthBuffer.surface.mipLevels = 1;
        m_depthBuffer.surface.format = GX2_SURFACE_FORMAT_FLOAT_D24_S8;
        m_depthBuffer.surface.use = GX2_SURFACE_USE_DEPTH_BUFFER;
        m_depthBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
        m_depthBuffer.viewNumSlices = 1; // Invariante de Latte: descarta rebanadas residuales
        m_depthBuffer.depthClear = 1.0f;
        GX2CalcSurfaceSizeAndAlignment(&m_depthBuffer.surface);

        if (GX2RCreateSurface(&m_depthBuffer.surface, static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_DEPTH_BUFFER | GX2R_RESOURCE_USAGE_GPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ
        ))) {
            GX2InitDepthBufferRegs(&m_depthBuffer);
            GX2InitDepthBufferHiZEnable(&m_depthBuffer, FALSE); // Prevenir lectura no inicializada de HiZ
            m_depthBufferCreated = true;
        }
    }

    // 3. Crear Context State con alineación estricta de 256 bytes en MEM1
    if (!m_nativeContextState) {
        m_nativeContextState = static_cast<GX2ContextState*>(
            MemoryManager::Instance().AllocAligned(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState),
                                                  MemoryTag::ContextState, "GameScene3D_ContextState")
        );
        if (m_nativeContextState) {
            GX2SetupContextStateEx(m_nativeContextState, TRUE);
        }
    }

    return rendererOk && m_depthBufferCreated && (m_nativeContextState != nullptr);
#else
    return true;
#endif
}

void GameScene3D::DestroyGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    // Guarda de micronúcleo de Cafe OS: solo drenar si el proceso está activo en primer plano
    DrainGPUQueueIfForeground();

    // Liberación automática RAII de todas las texturas y CubeMap de la escena (GPU ya drenada)
    ReleaseSceneTextures();

    // Destrucción determinista de superficie de profundidad
    if (m_depthBufferCreated) {
        GX2RDestroySurfaceEx(&m_depthBuffer.surface, GX2R_RESOURCE_BIND_NONE);
        m_depthBufferCreated = false;
    }

    // Liberación del Context State alineado
    if (m_nativeContextState) {
        void* rawCtx = static_cast<void*>(m_nativeContextState);
        MemoryManager::Instance().FreeAligned(rawCtx);
        m_nativeContextState = nullptr;
    }
#endif
}

ResourceHandle<GX2Texture> GameScene3D::LoadSceneTexture(std::string_view filepath, const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    ResourceHandle<GX2Texture> handle = m_sceneTexturePool.Emplace();
    GX2Texture* tex = m_sceneTexturePool.Get(handle);
    if (!tex) {
        return ResourceHandle<GX2Texture>{};
    }
    if (!Graphics::TextureLoaderGX2::LoadCookedTextureFromFile(filepath, *tex, debugLabel)) {
        m_sceneTexturePool.Destroy(handle);
        return ResourceHandle<GX2Texture>{};
    }
    return handle;
#else
    (void)filepath; (void)debugLabel;
    return ResourceHandle<GX2Texture>{};
#endif
}

ResourceHandle<GX2Texture> GameScene3D::LoadSceneTextureFromMemory(const uint8_t* data, size_t size, const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    ResourceHandle<GX2Texture> handle = m_sceneTexturePool.Emplace();
    GX2Texture* tex = m_sceneTexturePool.Get(handle);
    if (!tex) {
        return ResourceHandle<GX2Texture>{};
    }
    if (!Graphics::TextureLoaderGX2::LoadCookedTextureFromMemory(data, size, *tex, debugLabel)) {
        m_sceneTexturePool.Destroy(handle);
        return ResourceHandle<GX2Texture>{};
    }
    return handle;
#else
    (void)data; (void)size; (void)debugLabel;
    return ResourceHandle<GX2Texture>{};
#endif
}

ResourceHandle<GX2Texture> GameScene3D::CreateSceneSolidTexture(uint32_t rgba8Color, const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    ResourceHandle<GX2Texture> handle = m_sceneTexturePool.Emplace();
    GX2Texture* tex = m_sceneTexturePool.Get(handle);
    if (!tex) {
        return ResourceHandle<GX2Texture>{};
    }
    if (!Graphics::TextureLoaderGX2::CreateSolidColorTexture(*tex, rgba8Color, debugLabel)) {
        m_sceneTexturePool.Destroy(handle);
        return ResourceHandle<GX2Texture>{};
    }
    return handle;
#else
    (void)rgba8Color; (void)debugLabel;
    return ResourceHandle<GX2Texture>{};
#endif
}

bool GameScene3D::LoadSceneCubeMap(std::string_view filepath, const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (Graphics::TextureLoaderGX2::LoadCookedCubeMapFromFile(filepath, m_sceneCubeMap, debugLabel)) {
        m_sceneCubeMapCreated = true;
        return true;
    }
    return false;
#else
    (void)filepath; (void)debugLabel;
    return true;
#endif
}

bool GameScene3D::LoadSceneCubeMapFromMemory(const uint8_t* data, size_t size, const char* debugLabel) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (Graphics::TextureLoaderGX2::LoadCookedCubeMapFromMemory(data, size, m_sceneCubeMap, debugLabel)) {
        m_sceneCubeMapCreated = true;
        return true;
    }
    return false;
#else
    (void)data; (void)size; (void)debugLabel;
    return true;
#endif
}

GX2Texture* GameScene3D::GetSceneTexture(ResourceHandle<GX2Texture> handle) noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    return m_sceneTexturePool.Get(handle);
#else
    (void)handle;
    return nullptr;
#endif
}

GX2Texture* GameScene3D::GetSceneCubeMap() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    return m_sceneCubeMapCreated ? &m_sceneCubeMap : nullptr;
#else
    return nullptr;
#endif
}

void GameScene3D::ReleaseSceneTextures() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    m_sceneTexturePool.ForEachActive([](GX2Texture& tex) {
        Graphics::TextureLoaderGX2::DestroyTexture(tex);
    });
    m_sceneTexturePool.Clear();
    if (m_sceneCubeMapCreated) {
        Graphics::TextureLoaderGX2::DestroyTexture(m_sceneCubeMap);
        m_sceneCubeMapCreated = false;
    }
#endif
}

} // namespace WiiU
