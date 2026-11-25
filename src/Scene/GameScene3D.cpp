#include "Scene/GameScene3D.hpp"
#include "Core/Window.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/RHI/RHIFullscreenShader.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Core/MemoryManager.hpp"
#include <cstring>

#if defined(__WIIU__) || defined(__wiiu__)
#include <proc_ui/procui.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/event.h>
#include <gx2/state.h>
#include <gx2/clear.h>
#include <gx2r/surface.h>
#else
#include "Graphics/GL/GLFunctions.hpp"
#endif

namespace {

void DrainGPUQueueIfForeground() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown() &&
        WiiU::Window::HasForeground() && !WiiU::Window::IsExitingProcUI()) {
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

void GameScene3D::Render() {
#if defined(__WIIU__) || defined(__wiiu__)
    m_pColorBuffer = static_cast<GX2ColorBuffer*>(Window::GetActiveNativeColorBuffer());
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
        const GX2ClearFlags clearFlags = (m_depthBuffer.hiZPtr != nullptr)
                                         ? GX2_CLEAR_FLAGS_BOTH
                                         : GX2_CLEAR_FLAGS_DEPTH;
        GX2ClearDepthStencilEx(&m_depthBuffer, 1.0f, 0, clearFlags);
    }

    // Restaurar contexto, targets, viewport/scissor y Color Control tras GX2Clear*
    // (en silicio real gx2.rpl cambia al contexto de borrado y desactiva escritura de color).
    GX2SetContextState(m_nativeContextState);
    GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    if (m_depthBufferCreated) {
        GX2SetDepthBuffer(&m_depthBuffer);
    }
    const RHIFullscreenShader::ScopedActiveFrameTargets scopedTargets(
        m_pColorBuffer, m_depthBufferCreated ? &m_depthBuffer : nullptr);
    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, TRUE);
    RHIPipelineCache::Instance().Invalidate();

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
    if (Graphics::GL::IsLoaded()) {
        const auto& gl = Graphics::GL::GetFunctions();
        Graphics::GL::GLint vp[4]{0, 0, 1280, 720};
        if (gl.GetIntegerv) {
            gl.GetIntegerv(Graphics::GL::GL_VIEWPORT_VAL, vp);
        }
        constexpr float kInv255 = 1.0f / 255.0f;
        gl.Enable(Graphics::GL::GL_SCISSOR_TEST_VAL);
        gl.Scissor(vp[0], vp[1], vp[2], vp[3]);
        gl.ClearColor(static_cast<float>(m_clearColor.r) * kInv255,
                      static_cast<float>(m_clearColor.g) * kInv255,
                      static_cast<float>(m_clearColor.b) * kInv255,
                      static_cast<float>(m_clearColor.a) * kInv255);
        gl.Clear(Graphics::GL::GL_COLOR_BUFFER_BIT_VAL | Graphics::GL::GL_DEPTH_BUFFER_BIT_VAL);
        gl.Disable(Graphics::GL::GL_SCISSOR_TEST_VAL);
    }

    const Matrix4x4 viewMatrix = GetViewMatrix();
    const Matrix4x4 projMatrix = GetProjectionMatrix();
    const Point3D camPos = m_orbitCamera.GetPosition();

    DebugDraw3D::SetMatrices(viewMatrix, projMatrix, 1280.0f, 720.0f, m_nearPlane);

    auto& fwdRenderer = ForwardRendererGX2::Instance();
    fwdRenderer.BeginFrame(camPos, viewMatrix, projMatrix);
    OnGameRender3D(fwdRenderer);
    fwdRenderer.EndFrame();

    Render2D::Begin();
    OnGameRenderDebug3D();
    OnGameRenderHUD();
    Render2D::End();
#endif
}

bool GameScene3D::InitGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    // 1. Inicializar shaders, samplers y estado de ForwardRendererGX2
    bool rendererOk = ForwardRendererGX2::Instance().Initialize();

    // 2. Crear Búfer de Profundidad (1280x720, D24_S8) vía GX2R (reside en MEM2 para sobrevivir transiciones ProcUI/HOME)
    if (!m_depthBufferCreated) {
        std::memset(&m_depthBuffer, 0, sizeof(GX2DepthBuffer));
        m_depthBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        m_depthBuffer.surface.width = 1280;
        m_depthBuffer.surface.height = 720;
        m_depthBuffer.surface.depth = 1;
        m_depthBuffer.surface.mipLevels = 1;
        m_depthBuffer.surface.format = GX2_SURFACE_FORMAT_FLOAT_D24_S8;
        m_depthBuffer.surface.aa = GX2_AA_MODE1X;
        m_depthBuffer.surface.use = GX2_SURFACE_USE_DEPTH_BUFFER;
        m_depthBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
        m_depthBuffer.viewNumSlices = 1; // Invariante de Latte: descarta rebanadas residuales
        m_depthBuffer.depthClear = 1.0f;
        GX2CalcSurfaceSizeAndAlignment(&m_depthBuffer.surface);

        if (GX2RCreateSurface(&m_depthBuffer.surface, static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_DEPTH_BUFFER | GX2R_RESOURCE_USAGE_GPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ
        ))) {
            // [OPT-21 / OPT-39 / OPT-212 / OPT-267] Asignar superficie Hi-Z alineada y respetar orden Cafe OS:
            // GX2InitDepthBufferRegs PRIMERO, GX2InitDepthBufferHiZEnable DESPUES (evita sobrescritura de db_depth_info)
            uint32_t hiZSize = 0;
            uint32_t hiZAlign = 0;
            GX2CalcDepthBufferHiZInfo(&m_depthBuffer, &hiZSize, &hiZAlign);
            bool hiZEnabled = false;
            if (hiZSize > 0) {
                const size_t safeAlign = (hiZAlign < 256u) ? 256u : static_cast<size_t>(hiZAlign);
                m_hiZBuffer = MemoryManager::Instance().AllocAligned(
                    safeAlign, hiZSize, MemoryTag::SurfaceDepth, "GameScene3D_HiZ");
                if (m_hiZBuffer) {
                    std::memset(m_hiZBuffer, 0, hiZSize);
                    m_depthBuffer.hiZPtr = m_hiZBuffer;
                    m_depthBuffer.hiZSize = hiZSize;
                    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, m_hiZBuffer, hiZSize);
                    hiZEnabled = true;
                }
            }
            GX2InitDepthBufferRegs(&m_depthBuffer);
            GX2InitDepthBufferHiZEnable(&m_depthBuffer, hiZEnabled ? TRUE : FALSE);
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
    return ForwardRendererGX2::Instance().Initialize();
#endif
}

void GameScene3D::DestroyGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    // Limpiar registro global de targets antes de liberar las superficies GPU
    RHIFullscreenShader::ClearActiveFrameTargets();

    // Guarda de micronúcleo de Cafe OS: solo drenar si el proceso está activo en primer plano
    DrainGPUQueueIfForeground();

    // Liberación automática RAII de todas las texturas y CubeMap de la escena (GPU ya drenada)
    ReleaseSceneTextures();

    // Destrucción determinista de superficie de profundidad y Hi-Z
    if (m_hiZBuffer) {
        MemoryManager::Instance().FreeAligned(m_hiZBuffer);
        m_hiZBuffer = nullptr;
        m_depthBuffer.hiZPtr = nullptr;
        m_depthBuffer.hiZSize = 0;
    }
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
#else
    ReleaseSceneTextures();
#endif
}

ResourceHandle<GX2Texture> GameScene3D::LoadSceneTexture(std::string_view filepath, const char* debugLabel) {
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
}

ResourceHandle<GX2Texture> GameScene3D::LoadSceneTextureFromMemory(const uint8_t* data, size_t size, const char* debugLabel) {
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
}

ResourceHandle<GX2Texture> GameScene3D::CreateSceneSolidTexture(uint32_t rgba8Color, const char* debugLabel) {
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
}

bool GameScene3D::LoadSceneCubeMap(std::string_view filepath, const char* debugLabel) {
    if (m_sceneCubeMapCreated) {
        DrainGPUQueueIfForeground();
        Graphics::TextureLoaderGX2::DestroyTexture(m_sceneCubeMap);
        m_sceneCubeMapCreated = false;
    }
    if (Graphics::TextureLoaderGX2::LoadCookedCubeMapFromFile(filepath, m_sceneCubeMap, debugLabel)) {
        m_sceneCubeMapCreated = true;
        return true;
    }
    return false;
}

bool GameScene3D::LoadSceneCubeMapFromMemory(const uint8_t* data, size_t size, const char* debugLabel) {
    if (m_sceneCubeMapCreated) {
        DrainGPUQueueIfForeground();
        Graphics::TextureLoaderGX2::DestroyTexture(m_sceneCubeMap);
        m_sceneCubeMapCreated = false;
    }
    if (Graphics::TextureLoaderGX2::LoadCookedCubeMapFromMemory(data, size, m_sceneCubeMap, debugLabel)) {
        m_sceneCubeMapCreated = true;
        return true;
    }
    return false;
}

GX2Texture* GameScene3D::GetSceneTexture(ResourceHandle<GX2Texture> handle) noexcept {
    return m_sceneTexturePool.Get(handle);
}

GX2Texture* GameScene3D::GetSceneCubeMap() noexcept {
    return m_sceneCubeMapCreated ? &m_sceneCubeMap : nullptr;
}

void GameScene3D::ReleaseSceneTextures() noexcept {
    m_sceneTexturePool.ForEachActive([](GX2Texture& tex) {
        Graphics::TextureLoaderGX2::DestroyTexture(tex);
    });
    m_sceneTexturePool.Clear();
    if (m_sceneCubeMapCreated) {
        Graphics::TextureLoaderGX2::DestroyTexture(m_sceneCubeMap);
        m_sceneCubeMapCreated = false;
    }
}

} // namespace WiiU
