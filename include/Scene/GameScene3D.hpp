#pragma once

#include "Scene/IScene.hpp"
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include "Core/ResourceHandle.hpp"
#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Systems/OrbitCameraSystem.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include "Graphics/DebugDraw3D.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/surface.h>
#include <gx2/context.h>
#else
struct GX2Texture;
struct GX2ContextState;
#endif

namespace WiiU {

/**
 * @brief [Hito 119] Clase Base Reusable para Escenas y Videojuegos 3D en la Arquitectura del Motor.
 * 
 * @author Wii U Game Engine Architecture Team
 * @date Noviembre 2026 (Hito 119)
 * 
 * @par Fundamentos de Arquitectura de Motor (Jason Gregory):
 * - **Jason Gregory** (*Game Engine Architecture*, 3ª Edición, Cap. 14 "Gameplay Foundation Systems" &
 *   Cap. 14.5 "User Interface & Render Loop Orchestration").
 * - **Erich Gamma et al.** (*Design Patterns*, "Template Method Pattern").
 * - **Bjarne Stroustrup** (*The C++ Programming Language*, 4ª Ed., "RAII & Resource Ownership").
 * 
 * @par Invariantes de Silicio Garantizadas:
 * 1. **Gestión RAII de Hardware GX2:** Creación, registro y destrucción determinista del búfer de profundidad
 *    `GX2DepthBuffer` (1280x720, `D24_S8`, eDRAM @ 560 GB/s) y `GX2ContextState` alineado estrictamente a 256 bytes.
 * 2. **Orquestación de Pases de Renderizado:**
 *    - **Pase 3D:** Activa `ForwardRendererGX2::BeginFrame / EndFrame` delegando en `OnGameRender3D(renderer)`.
 *    - **Pase 2D Aislado:** Activa `UI::ScopedUIPass` configurando estados de mezcla alfa y viewport antes de invocar
 *      secuencialmente `OnGameRenderDebug3D()` (dibujo wireframe de colisiones) y `OnGameRenderHUD()` (interfaz de usuario).
 * 3. **Cero Alocaciones Dinámicas:** Telemetría integrada a 60 FPS fijos sin sobrecoste de heap en el bucle principal.
 * 4. **Drenado Seguro de GPU:** Ejecuta `GX2DrawDone()` protegido por `ProcUIIsRunning() && ProcUIInForeground()`
 *    antes de destruir cualquier recurso de silicio.
 */
class GameScene3D : public IScene {
public:
    /**
     * @brief Constructor por defecto. Inicializa estructuras de telemetría y cámara orbital.
     */
    GameScene3D();

    /**
     * @brief Destructor virtual. Destruye automáticamente el pipeline de silicio mediante RAII.
     */
    ~GameScene3D() override;

    // Regla de los 5 (Rule of Five): Clases administradoras de silicio no son copiables ni movibles.
    GameScene3D(const GameScene3D&) = delete;
    GameScene3D& operator=(const GameScene3D&) = delete;
    GameScene3D(GameScene3D&&) = delete;
    GameScene3D& operator=(GameScene3D&&) = delete;

    /**
     * @brief Ciclo de entrada a la escena (Template Method de IScene).
     * 
     * Inicializa el pipeline de hardware GX2 (`InitGX2Pipeline`) e invoca el hook `OnGameInit()`.
     */
    void OnEnter() override;

    /**
     * @brief Ciclo de salida de la escena (Template Method de IScene).
     * 
     * Invoca el hook de limpieza derivado `OnGameExit()` y purga el pipeline de silicio (`DestroyGX2Pipeline`).
     */
    void OnExit() override;

    /**
     * @brief Actualización por frame a 60 Hz.
     * 
     * Calcula métricas de telemetría (FPS y FrameTime) y delega la lógica de juego a `OnGameUpdate(dt)`.
     * @param dt Delta time en segundos transcurridos desde el último fotograma (~0.0166s a 60 FPS).
     */
    void Update(float dt) override;

    /**
     * @brief Renderizado por frame delegando en pases de hardware.
     * 
     * Orquesta la configuración de render targets, limpia color y profundidad, prepara `DebugDraw3D`,
     * ejecuta el pase 3D (`OnGameRender3D`) y posteriormente el pase de UI/Debug (`OnGameRenderDebug3D` + `OnGameRenderHUD`).
     */
    void Render() override;

    /**
     * @brief Indica si la escena opera en modo GX2 de silicio puro sobre hardware Wii U.
     * @return true en compilaciones para Nintendo Wii U; false en fallback software.
     */
    [[nodiscard]] bool IsPureGX2() const noexcept override {
#if defined(__WIIU__) || defined(__wiiu__)
        return true;
#else
        return false;
#endif
    }

    /**
     * @brief GameScene3D ejecuta GX2ClearColor y GX2ClearDepthStencilEx directamente en __WIIU__.
     */
    [[nodiscard]] bool ClearsFramebuffer() const noexcept override {
#if defined(__WIIU__) || defined(__wiiu__)
        return true;
#else
        return false;
#endif
    }

    // =========================================================================
    // HOOKS POLIMÓRFICOS VIRTUALES PARA JUEGOS Y NIVELES DERIVADOS
    // =========================================================================

    /**
     * @brief Inicialización de la lógica del juego, carga de mallas y poblamiento del mundo ECS.
     * Invocado inmediatamente después de que el pipeline de silicio GX2 esté activo.
     */
    virtual void OnGameInit() = 0;

    /**
     * @brief Limpieza de recursos específicos del juego (mallas, texturas, pooling de entidades).
     * Invocado antes de la destrucción del pipeline de silicio.
     */
    virtual void OnGameExit() {}

    /**
     * @brief Bucle de actualización de física, cinemática, inputs y estados de juego derivados.
     * @param dt Delta time en segundos.
     */
    virtual void OnGameUpdate(float dt) = 0;

    /**
     * @brief Pase de renderizado de geometría 3D con Z-Buffer activo en silicio Latte.
     * 
     * Invocado entre `ForwardRendererGX2::BeginFrame` y `EndFrame`.
     * @param renderer Referencia al subsistema `ForwardRendererGX2`.
     */
    virtual void OnGameRender3D(ForwardRendererGX2& renderer) = 0;

    /**
     * @brief Pase de renderizado de depuración visual 3D (wireframes de colisión, triggers, rayos).
     * 
     * Invocado dentro de `UI::ScopedUIPass`. Debe utilizar los métodos estáticos de `DebugDraw3D`.
     */
    virtual void OnGameRenderDebug3D() {}

    /**
     * @brief Pase de renderizado de la interfaz de usuario 2D (paneles, botones, barras de vida, telemetría).
     * 
     * Invocado dentro de `UI::ScopedUIPass` con proyección ortográfica exacta a 1280x720.
     */
    virtual void OnGameRenderHUD() {}

    // =========================================================================
    // ACCESORES Y CONFIGURACIÓN DEL MOTOR
    // =========================================================================

    /**
     * @brief Obtiene una referencia mutable al mundo Entity Component System (ECSWorld) de la escena.
     */
    [[nodiscard]] ECSWorld& GetECSWorld() noexcept { return m_ecsWorld; }

    /**
     * @brief Obtiene una referencia de solo lectura al mundo ECSWorld de la escena.
     */
    [[nodiscard]] const ECSWorld& GetECSWorld() const noexcept { return m_ecsWorld; }

    /**
     * @brief Obtiene una referencia mutable al sistema de cámara orbital integrada.
     */
    [[nodiscard]] OrbitCameraSystem& GetOrbitCamera() noexcept { return m_orbitCamera; }

    /**
     * @brief Obtiene una referencia de solo lectura a la cámara orbital.
     */
    [[nodiscard]] const OrbitCameraSystem& GetOrbitCamera() const noexcept { return m_orbitCamera; }

    /**
     * @brief Retorna la tasa actual de fotogramas por segundo filtrada temporalmente.
     */
    [[nodiscard]] float GetFPS() const noexcept { return m_fps; }

    /**
     * @brief Retorna el tiempo de fotograma en milisegundos (~16.63 ms a 60 FPS).
     */
    [[nodiscard]] float GetFrameTimeMs() const noexcept { return m_frameTimeMs; }

    /**
     * @brief Establece el color cromático de borrado del búfer de color de la GPU (ej. color del cielo).
     * @param color Tinte RGBA normalizado en bytes (0..255).
     */
    void SetClearColor(const ColorRGBA& color) noexcept { m_clearColor = color; }

    /**
     * @brief Obtiene el color de borrado de pantalla actual.
     */
    [[nodiscard]] ColorRGBA GetClearColor() const noexcept { return m_clearColor; }

    /**
     * @brief Configura los parámetros de la matriz de proyección en perspectiva de la cámara.
     * @param fovYDeg Campo de visión vertical en grados sexagesimales (convertido automáticamente a radianes).
     * @param nearZ Distancia del plano de corte anterior (por defecto 0.20m).
     * @param farZ Distancia del plano de corte posterior (por defecto 1000.0m).
     */
    void SetCameraProjection(float fovYDeg, float nearZ = 0.20f, float farZ = 1000.0f) noexcept {
        constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
        m_fovYRad = fovYDeg * DEG_TO_RAD;
        m_nearPlane = nearZ;
        m_farPlane = farZ;
    }

    /**
     * @brief Retorna la matriz de vista actual derivada de la cámara orbital.
     */
    [[nodiscard]] Matrix4x4 GetViewMatrix() const noexcept { return m_orbitCamera.GetViewMatrix(); }

    /**
     * @brief Retorna la matriz de proyección en perspectiva actual.
     */
    [[nodiscard]] Matrix4x4 GetProjectionMatrix() const noexcept;

    /**
     * @brief Retorna la matriz combinada View-Projection ($M = \text{Proj} \times \text{View}$).
     */
    [[nodiscard]] Matrix4x4 GetViewProjectionMatrix() const noexcept;

#if defined(__WIIU__) || defined(__wiiu__)
    /**
     * @brief Retorna el puntero nativo al objeto GX2ContextState asignado con alineación de 256 bytes.
     */
    [[nodiscard]] GX2ContextState* GetContextState() noexcept { return m_nativeContextState; }

    /**
     * @brief Retorna el búfer de color principal activo gestionado por Window::GetActiveNativeColorBuffer().
     */
    [[nodiscard]] GX2ColorBuffer* GetColorBuffer() noexcept { return m_pColorBuffer; }

    /**
     * @brief Retorna el puntero al búfer de profundidad de silicio (D24_S8) en eDRAM.
     */
    [[nodiscard]] GX2DepthBuffer* GetDepthBuffer() noexcept { return &m_depthBuffer; }
#endif

protected:
    /**
     * @brief Inicializa el contexto de hardware, la superficie de profundidad en eDRAM y ForwardRendererGX2.
     * @return true si la inicialización en silicio fue exitosa.
     */
    bool InitGX2Pipeline();

    /**
     * @brief Drena la cola de la GPU con `GX2DrawDone()` y destruye de forma segura el Depth Buffer y Context State.
     */
    void DestroyGX2Pipeline();

    // Helpers reutilizables para texturas (.wtex), CubeMaps (.wcube) y limpieza RAII
    ResourceHandle<GX2Texture> LoadSceneTexture(std::string_view filepath, const char* debugLabel = "SceneTex");
    ResourceHandle<GX2Texture> LoadSceneTextureFromMemory(const uint8_t* data, size_t size, const char* debugLabel = "SceneTex");
    ResourceHandle<GX2Texture> CreateSceneSolidTexture(uint32_t rgba8Color = 0xFFFFFFFFu, const char* debugLabel = "SceneWhiteTex");
    bool LoadSceneCubeMap(std::string_view filepath, const char* debugLabel = "SceneCubeMap");
    bool LoadSceneCubeMapFromMemory(const uint8_t* data, size_t size, const char* debugLabel = "SceneCubeMap");
    [[nodiscard]] GX2Texture* GetSceneTexture(ResourceHandle<GX2Texture> handle) noexcept;
    [[nodiscard]] GX2Texture* GetSceneCubeMap() noexcept;
    void ReleaseSceneTextures() noexcept;

    ECSWorld m_ecsWorld;
    OrbitCameraSystem m_orbitCamera;

    ColorRGBA m_clearColor{132, 191, 232, 255}; // Azul cielo por defecto
    float m_fovYRad{55.0f * (3.14159265358979323846f / 180.0f)};
    float m_aspectRatio{1280.0f / 720.0f};
    float m_nearPlane{0.20f};
    float m_farPlane{1000.0f};

    float m_fps{60.0f};
    float m_frameTimeMs{16.6f};
    float m_fpsTimer{0.0f};
    int m_frameCount{0};

#if defined(__WIIU__) || defined(__wiiu__)
    GX2DepthBuffer m_depthBuffer{};
    void* m_hiZBuffer{nullptr};
    bool m_depthBufferCreated{false};
    GX2ContextState* m_nativeContextState{nullptr};
    GX2ColorBuffer* m_pColorBuffer{nullptr};
    ResourcePool<GX2Texture, 16> m_sceneTexturePool{};
    GX2Texture m_sceneCubeMap{};
    bool m_sceneCubeMapCreated{false};
#else
    void* m_pColorBuffer{nullptr};
#endif
};

} // namespace WiiU
