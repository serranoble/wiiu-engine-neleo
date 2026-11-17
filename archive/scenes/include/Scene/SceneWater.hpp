#pragma once

#include "Scene/IScene.hpp"
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include "Graphics/UIComponents.hpp"
#include "Audio/AudioEngine3D.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <cstdint>
#include <thread>
#include <atomic>
#if defined(__WIIU__) || defined(__wiiu__)
#include <coreinit/event.h>
#include <whb/gfx.h>
#include <gx2/draw.h>
#include <gx2/shaders.h>
#include <gx2/clear.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/mem.h>
#include <gx2r/buffer.h>
#include <gx2r/draw.h>
#else
#include <mutex>
#include <condition_variable>
#endif

namespace WiiU {

/**
 * @brief Estilos de cuerpos de agua inspirados en The Legend of Zelda: Breath of the Wild.
 */
enum class WaterPreset {
    LAKE_HYLIA,      // Lago Hylia: Azul zafiro real, orillas turquesa cristalino, reflejos dorados
    LANAYRU_SPRING,  // Manantial de Lanayru: Tonos esmeralda y menta mística, ondulación sedosa
    LURELIN_OCEAN    // Mar de Ona / Aldea Ona: Océano tropical profundo con olas enérgicas
};

/**
 * @brief Intensidad y energía del oleaje trocoidal de Gerstner.
 */
enum class WaveIntensity {
    CALM,       // Lago en calma (brisa ligera, ondas sutiles)
    MODERATE,   // Oleaje estándar de Breath of the Wild
    STORMY      // Tempestad con olas altas, crestas afiladas y espuma abundante
};

/**
 * @brief Modo de visualización de la malla 3D.
 */
enum class WaterRenderMode {
    SOLID_BOTW,           // Superficie cel-shaded nativa con Shaders CafeGLSL en GPU Latte
    SMP_CPU_ESPRESSO,     // Simulación multihilo pura en CPU Espresso (Cores 1 + 2)
    WIREFRAME_MESH,       // Inspección técnica de la malla 3D poligonal en tiempo real
    SOLID_PLUS_WIREFRAME  // Superficie sólida con aristas poligonales superpuestas
};

/**
 * @brief Hora del día atmosférica al estilo Hyrule.
 */
enum class TimeOfDay {
    NOON,    // Mediodía radiante: sol brillante cenital, agua turquesa luminosa
    SUNSET,  // Atardecer dorado: cielo ámbar y púrpura, reflejos dorados rasantes
    NIGHT    // Noche de Hyrule: cielo azul medianoche con luna y reflejos plateados
};

/**
 * @brief Parámetros individuales para una onda trocoidal de Gerstner en 3D.
 */
struct GerstnerWaveParams {
    float dirX{1.0f};      // Vector de dirección horizontal X (normalizado)
    float dirZ{0.0f};      // Vector de dirección horizontal Z (normalizado)
    float amplitude{0.15f};// Altura base de cresta (A)
    float wavelength{4.0f};// Longitud de onda espacial (L)
    float speed{1.2f};     // Velocidad de propagación de fase (c)
    float steepness{0.65f};// Agudeza de cresta (Q: 0 = seno suave, 1 = cresta trocoidal afilada)
};

/**
 * @brief Invariantes precalculadas por onda para evaluación multi-core ultra-eficiente.
 */
struct PrecomputedWave {
    float dirX{1.0f};
    float dirZ{0.0f};
    float k{1.0f};
    float phaseTime{0.0f};
    float amp{0.1f};
    float qAmp{0.1f};
    float kAmp{0.1f};
    float qkAmp{0.1f};
};

/**
 * @brief Contexto de parámetros de simulación compartido de forma segura e inmutable entre Core 1 y Core 2.
 */
struct WaterSimParams {
    PrecomputedWave waves[4];
    size_t numWaves{0};
    float maxAmp{0.01f};
    Point3D halfDir{0.0f, 1.0f, 0.0f};
    Point3D camPos{0.0f, 0.0f, 0.0f};
    Point3D camForward{0.0f, 0.0f, 1.0f};
    Point3D camRight{1.0f, 0.0f, 0.0f};
    Point3D camUp{0.0f, 1.0f, 0.0f};
};

/**
 * @brief Demo 7: Simulación de Agua 3D con Técnicas de The Legend of Zelda: Breath of the Wild.
 * 
 * Implementa:
 * 1. Malla geométrica 3D regular con batching acelerado por hardware en 1 sola llamada de dibujo (Single Draw Call).
 * 2. 4 Ondas trocoidales de Gerstner en paralelo (desplazamiento horizontal X,Z y vertical Y con normales analíticas).
 * 3. Coloración cel-shaded por elevación y profundidad inspirada en el Lago Hylia de BotW.
 * 4. Espuma procedural en crestas afiladas (Crest Foam).
 * 5. Factor de Fresnel de Schlick para transición transparencia/reflejo de cielo.
 * 6. Destellos especulares solares de Blinn-Phong en las facetas del agua.
 * 7. Objeto náutico flotante con hidrodinámica de Arquímedes en tiempo real.
 * 8. Cámara orbital 3D interactiva (Touch / GamePad Sticks) y selector de iluminación diurna/nocturna.
 * 9. Multiprocesamiento simétrico (SMP) aprovechando Core 1 y Core 2 del procesador Espresso.
 */
class SceneWater : public IScene {
public:
    SceneWater();
    ~SceneWater() override;

    SceneWater(const SceneWater&) = delete;
    SceneWater& operator=(const SceneWater&) = delete;
    SceneWater(SceneWater&&) = delete;
    SceneWater& operator=(SceneWater&&) = delete;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    void CyclePreset();
    void CycleWaveIntensity();
    void CycleRenderMode();
    void CycleTimeOfDay();
    void ToggleCrestFoam();
    void ResetCamera();

    [[nodiscard]] WaterPreset GetPreset() const noexcept { return m_preset; }
    [[nodiscard]] WaveIntensity GetWaveIntensity() const noexcept { return m_waveIntensity; }
    [[nodiscard]] WaterRenderMode GetRenderMode() const noexcept { return m_renderMode; }
    [[nodiscard]] TimeOfDay GetTimeOfDay() const noexcept { return m_timeOfDay; }
    [[nodiscard]] bool IsCrestFoamEnabled() const noexcept { return m_crestFoamEnabled; }

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return {"click.wav", "shaders/water.gsh", "shaders/native_gx2.gsh"};
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Demo 7: Agua 3D (Zelda: Breath of the Wild)";
    }

    [[nodiscard]] bool IsPureGX2() const noexcept override {
#if defined(__WIIU__) || defined(__wiiu__)
        return true;
#else
        return false;
#endif
    }

private:
    void InitGridGeometry();
    void UpdateWaveParameters();
    void UpdateCameraVectors();
    void UpdateButtonsLabels();

    void RenderSkyAndLandscape(SDL_Renderer* renderer);
    void RenderWaterMesh(SDL_Renderer* renderer);
    void RenderFloatingBuoy(SDL_Renderer* renderer);
    void RenderHUD(SDL_Renderer* renderer);

    // Pipeline 100% Nativo de Shaders GX2 CafeGLSL y Render2DGX2 en GPU AMD Latte
    bool InitGX2WaterPipeline();
    void DestroyGX2WaterPipeline();
    void BindColorBuffer(SDL_Renderer* renderer);
    void RenderSkyAndLandscapeGX2();
    void RenderWaterMeshGX2();
    void RenderWaterMeshGX2Native();
    void RenderWaterMeshCPUGX2();
    void RenderWaterWireframeOnlyGX2(int lodLevel);
    void RenderBuoyAndHUD_GX2();
    void RenderWaterMeshCPU(SDL_Renderer* renderer);
    void RenderWaterWireframeOnly(SDL_Renderer* renderer, int lodLevel);

    // Multiprocesamiento simétrico (SMP: Core 1 Principal + Core 2 Trabajador)
    void InitWorkerThread();
    void ShutdownWorkerThread();
    void WorkerLoop();
    void ComputeVertexSlice(size_t startIdx, size_t endIdx, const WaterSimParams& params);

    // Dimensiones de la cuadrícula de agua 3D con 3 Niveles de Detalle (3 LODs Dinámicos)
    static constexpr int GRID_SIZE_X = 41;
    static constexpr int GRID_SIZE_Z = 41;
    static constexpr int NUM_VERTICES = GRID_SIZE_X * GRID_SIZE_Z; // 1,681 vértices
    static constexpr int NUM_QUADS_LOD0 = (GRID_SIZE_X - 1) * (GRID_SIZE_Z - 1); // 40x40 = 1,600 quads
    static constexpr int NUM_INDICES_LOD0 = NUM_QUADS_LOD0 * 6; // 9,600 índices (LOD 0: Ultra cerca)
    static constexpr int NUM_QUADS_LOD1 = ((GRID_SIZE_X - 1) / 2) * ((GRID_SIZE_Z - 1) / 2); // 20x20 = 400 quads
    static constexpr int NUM_INDICES_LOD1 = NUM_QUADS_LOD1 * 6; // 2,400 índices (LOD 1: Media distancia)
    static constexpr int NUM_QUADS_LOD2 = ((GRID_SIZE_X - 1) / 4) * ((GRID_SIZE_Z - 1) / 4); // 10x10 = 100 quads
    static constexpr int NUM_INDICES_LOD2 = NUM_QUADS_LOD2 * 6; // 600 índices (LOD 2: Larga distancia)
    static constexpr float NEAR_PLANE_Z = 1.20f; // Plano cercano calibrado para prevenir división por cero y sobregiro de hardware

    // Malla estática en espacio de reposo
    struct BaseVertex {
        float x{0.0f};
        float z{0.0f};
    };
    std::vector<BaseVertex> m_baseGrid;
    std::vector<int> m_meshIndicesLOD0;
    std::vector<int> m_meshIndicesLOD1;
    std::vector<int> m_meshIndicesLOD2;

    // Búferes para la llamada de renderizado por GPU (SDL_RenderGeometry)
    std::vector<SDL_Vertex> m_renderVertices;
    std::vector<int> m_visibleIndices;
    std::vector<float> m_vertexDepthZ;

    // Parámetros de ondas de Gerstner (4 ondas en superposición armónica)
    std::vector<GerstnerWaveParams> m_waves;
    float m_waveAmplitudeMultiplier{1.0f};
    float m_waterLevel{0.0f};

    // Configuración estética y de renderizado
    WaterPreset m_preset{WaterPreset::LAKE_HYLIA};
    WaveIntensity m_waveIntensity{WaveIntensity::MODERATE};
    WaterRenderMode m_renderMode{WaterRenderMode::SOLID_BOTW};
    TimeOfDay m_timeOfDay{TimeOfDay::NOON};
    bool m_crestFoamEnabled{true};

    // Paletas de color activas
    ColorRGBA m_waterDeepColor;
    ColorRGBA m_waterShallowColor;
    ColorRGBA m_waterFoamColor;
    ColorRGBA m_skyZenithColor;
    ColorRGBA m_skyHorizonColor;
    ColorRGBA m_sunColor;
    Point3D m_sunDir{0.577f, 0.75f, -0.45f};

    // Cámara orbital 3D
    Point3D m_camPivot{0.0f, 0.0f, 0.0f};
    float m_camYaw{0.0f};        // Ángulo azimutal (radianes)
    float m_camPitch{0.48f};     // Ángulo de elevación (radianes, ~27.5°)
    float m_camDistance{14.5f};  // Distancia orbital
    Point3D m_camPos{0.0f, 6.5f, -13.0f};
    Point3D m_camForward{0.0f, -0.45f, 0.89f};
    Point3D m_camRight{1.0f, 0.0f, 0.0f};
    Point3D m_camUp{0.0f, 0.89f, 0.45f};

    // Interacción táctil de cámara
    bool m_isDraggingCamera{false};
    float m_lastTouchX{0.0f};
    float m_lastTouchY{0.0f};

    // Boya / Barril náutico flotante
    Point3D m_buoyBasePos{0.0f, 0.0f, 0.5f};
    float m_buoyRadius{0.75f};

    // Tiempo y telemetría
    float m_animTime{0.0f};
    Uint32 m_lastFrameTicks{0};
    float m_smoothedFps{60.0f};

    // Botones de interfaz interactiva
    UIButton m_backButton;
    UIButton m_modeButton;
    UIButton m_presetButton;
    UIButton m_waveButton;
    UIButton m_timeButton;
    UIButton m_foamButton;
    UIButton m_camResetButton;

    // Audio 3D espacializado para el oleaje ambiental
    SoundHandle m_waterSoundHandle{};

    // Multiprocesamiento simétrico (SMP Espresso CPU) con aislamiento L1D (alignas 32 bytes)
    std::thread m_workerThread;
    alignas(32) std::atomic<bool> m_stopWorker{false};
    alignas(32) std::atomic<int> m_targetFrame{0};
    alignas(32) std::atomic<int> m_workerDoneFrame{0};

#if defined(__WIIU__) || defined(__wiiu__)
    alignas(32) OSEvent m_jobEvent{};
    alignas(32) OSEvent m_doneEvent{};

    // Recursos de Sombreador CafeGLSL (GFD / GSH) y Buffers GPU Latte
    WHBGfxShaderGroup m_waterShaderGroup{};
    bool m_gx2ShaderLoaded{false};
    bool m_useDirectGFD{false};
    GX2FetchShader m_directFetchShader{};
    GX2VertexShader* m_directVs{nullptr};
    void* m_directVsProgram{nullptr};
    GX2PixelShader* m_directPs{nullptr};
    void* m_directPsProgram{nullptr};
    void* m_directFsProgram{nullptr};

    GX2RBuffer m_gridVertexBuffer{};
    GX2RBuffer m_indexBufferLOD0{};
    GX2RBuffer m_indexBufferLOD1{};
    GX2RBuffer m_indexBufferLOD2{};
    bool m_gx2BuffersCreated{false};
    GX2ContextState* m_nativeContextState{nullptr};
    GX2ColorBuffer* m_pColorBuffer{nullptr};
    bool m_colorBufferBound{false};
#else
    std::mutex m_syncMutex;
    std::condition_variable m_syncCv;
    bool m_jobReady{false};
#endif

    bool m_renderedWithGPU{false};
    WaterSimParams m_activeSimParams{};
};

} // namespace WiiU
