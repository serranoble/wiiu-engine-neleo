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
        Neleo::Window::HasForeground() && !Neleo::Window::IsExitingProcUI()) {
        GX2DrawDone();
    }
#endif
}

} // namespace

namespace Neleo {

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
    m_pDepthBuffer = static_cast<GX2DepthBuffer*>(Window::GetActiveNativeDepthBuffer());
    if (!m_nativeContextState || !m_pColorBuffer) {
        return;
    }

    // -------------------------------------------------------------------------
    // CONFIGURACIÓN DETERMINISTA DE CONTEXTO GPU LATTE GX2 (eDRAM / MEM1 @ 560 GB/s)
    // -------------------------------------------------------------------------
    GX2SetContextState(m_nativeContextState);
    GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    if (m_pDepthBuffer) {
        GX2SetDepthBuffer(m_pDepthBuffer);
    }
    // Viewport y Scissor canónicos para escaneo de 720p (1280x720)
    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);

    // Borrado de búferes de hardware (Color eDRAM y Z-Buffer FLOAT_R32 + HiZ en MEM1)
    float r = static_cast<float>(m_clearColor.r) / 255.0f;
    float g = static_cast<float>(m_clearColor.g) / 255.0f;
    float b = static_cast<float>(m_clearColor.b) / 255.0f;
    float a = static_cast<float>(m_clearColor.a) / 255.0f;
    GX2ClearColor(m_pColorBuffer, r, g, b, a);
    if (m_pDepthBuffer) {
        GX2ClearDepthStencilEx(m_pDepthBuffer, 1.0f, 0u, GX2_CLEAR_FLAGS_DEPTH);
    }

    // Restaurar contexto, targets, viewport/scissor y Color Control tras GX2Clear*
    // (en silicio real gx2.rpl cambia al contexto de borrado y desactiva escritura de color).
    GX2SetContextState(m_nativeContextState);
    GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    if (m_pDepthBuffer) {
        GX2SetDepthBuffer(m_pDepthBuffer);
    }
    const RHIFullscreenShader::ScopedActiveFrameTargets scopedTargets(
        m_pColorBuffer, m_pDepthBuffer);
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
    const bool rendererOk = ForwardRendererGX2::Instance().Initialize();

    // 2. Reutilizar el DepthBuffer principal en eDRAM/MEM1 (560 GB/s + HiZ) gestionado por WindowWiiU
    //    (evita el cuello de botella DDR3/MEM2 de 12.8 GB/s y sobrevive automáticamente a ProcUI/HOME).
    m_pDepthBuffer = static_cast<GX2DepthBuffer*>(Window::GetActiveNativeDepthBuffer());

    // 3. Crear Context State con alineación estricta de 256 bytes
    if (!m_nativeContextState) {
        m_nativeContextState = static_cast<GX2ContextState*>(
            MemoryManager::Instance().AllocAligned(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState),
                                                  MemoryTag::ContextState, "GameScene3D_ContextState")
        );
        if (m_nativeContextState) {
            GX2SetupContextStateEx(m_nativeContextState, TRUE);
        }
    }

    return rendererOk && (m_nativeContextState != nullptr);
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

    m_pColorBuffer = nullptr;
    m_pDepthBuffer = nullptr;

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

} // namespace Neleo
