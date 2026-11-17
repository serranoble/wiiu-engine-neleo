#include "Scene/SceneWater.hpp"
#include "Scene/SceneMenu.hpp"
#include "Scene/SceneMenuGX2.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include "Graphics/GX2StateHelper.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/WaterShaderBinary.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/StackString.hpp"
#include <cmath>
#include <algorithm>
#include <array>
#include <cstring>
#include <malloc.h>
#if defined(__WIIU__) || defined(__wiiu__)
#include <gfd.h>
#include <coreinit/thread.h>
#endif

namespace WiiU {

// Distancia focal para FOV vertical de 60° en pantalla 1280x720 (360 / tan(30°) = 360 * sqrt(3))
constexpr float FOCAL_FACTOR = 623.53829f;

SceneWater::SceneWater() {
    // Fila 1 de botones (Y = 10, H = 38): Retorno, Modo de renderizado, Preset visual, Intensidad de olas
    m_backButton   = UIButton(15, 10, 135, 38, "< VOLVER", 2);
    m_modeButton   = UIButton(160, 10, 420, 38, "MODO: SOLIDO (BOTW)", 2);
    m_presetButton = UIButton(590, 10, 420, 38, "PRESET: LAGO HYLIA", 2);
    m_waveButton   = UIButton(1020, 10, 245, 38, "OLEAJE: MODERADO", 2);

    // Fila 2 de botones (Y = 54, H = 38): Ciclo día/noche, Espuma de crestas, Reinicio de cámara
    m_timeButton      = UIButton(15, 54, 410, 38, "HORA: MEDIODIA (BOTW)", 2);
    m_foamButton      = UIButton(435, 54, 410, 38, "ESPUMA CRESTAS: ACTIVADA", 2);
    m_camResetButton  = UIButton(855, 54, 410, 38, "CAMARA: REINICIAR VISTA", 2);

    // Configuración estética de botones
    ColorRGBA normalFill{20, 35, 55, 230};
    ColorRGBA hoverFill{0, 180, 240, 240};
    ColorRGBA normalBorder{0, 210, 255, 255};
    ColorRGBA hoverBorder = ColorRGBA::Yellow();
    ColorRGBA textColor = ColorRGBA::White();

    m_backButton.SetColors(ColorRGBA{12, 18, 32, 255}, ColorRGBA{200, 40, 40, 240},
                           ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White());
    m_modeButton.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);
    m_presetButton.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);
    m_waveButton.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);
    m_timeButton.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);
    m_foamButton.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);
    m_camResetButton.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);

    // Inicializar geometría estática de la cuadrícula
    InitGridGeometry();

    // Configurar parámetros iniciales de simulación
    UpdateWaveParameters();
    UpdateCameraVectors();
    UpdateButtonsLabels();

    // Inicializar subproceso de cómputo simétrico SMP en Core 2
    InitWorkerThread();
}

SceneWater::~SceneWater() {
    OnExit();
}

void SceneWater::InitGridGeometry() {
    m_baseGrid.clear();
    m_baseGrid.reserve(NUM_VERTICES);

    // Cobertura espacial del lago: 14.4x14.4 unidades centradas en el origen (radio máximo de esquina = 10.18)
    // Garantiza que la cámara (órbita mínima R >= 12.97) siempre permanezca fuera del perímetro bajo cualquier yaw y oleaje
    constexpr float EXTENT_XZ = 7.2f;
    constexpr float STEP_X = (EXTENT_XZ * 2.0f) / static_cast<float>(GRID_SIZE_X - 1);
    constexpr float STEP_Z = (EXTENT_XZ * 2.0f) / static_cast<float>(GRID_SIZE_Z - 1);

    for (int gz = 0; gz < GRID_SIZE_Z; ++gz) {
        float z = -EXTENT_XZ + static_cast<float>(gz) * STEP_Z;
        for (int gx = 0; gx < GRID_SIZE_X; ++gx) {
            float x = -EXTENT_XZ + static_cast<float>(gx) * STEP_X;
            m_baseGrid.push_back(BaseVertex{x, z});
        }
    }

    // [OPT-111] Reordenamiento de índices con serpentina PTVC (Heurística de Tom Forsyth / Hugues Hoppe).
    // Recorrer filas alternando izquierda->derecha y derecha->izquierda mantiene los vértices de borde
    // calientes en la caché FIFO (16-32 entradas) de la GPU Latte, reduciendo el trabajo de vertex shading.
    auto appendSerpentineQuads = [](std::vector<int>& indices, int step) {
        int row = 0;
        for (int gz = 0; gz < GRID_SIZE_Z - 1; gz += step, ++row) {
            if ((row & 1) == 0) {
                // Fila par: izquierda -> derecha
                for (int gx = 0; gx < GRID_SIZE_X - 1; gx += step) {
                    int v0 = gz * GRID_SIZE_X + gx;
                    int v1 = v0 + step;
                    int v2 = (gz + step) * GRID_SIZE_X + gx;
                    int v3 = v2 + step;

                    indices.push_back(v0);
                    indices.push_back(v1);
                    indices.push_back(v2);

                    indices.push_back(v1);
                    indices.push_back(v3);
                    indices.push_back(v2);
                }
            } else {
                // Fila impar: derecha -> izquierda (mantiene caliente el extremo en PTVC)
                for (int gx = GRID_SIZE_X - 1 - step; gx >= 0; gx -= step) {
                    int v0 = gz * GRID_SIZE_X + gx;
                    int v1 = v0 + step;
                    int v2 = (gz + step) * GRID_SIZE_X + gx;
                    int v3 = v2 + step;

                    indices.push_back(v0);
                    indices.push_back(v1);
                    indices.push_back(v2);

                    indices.push_back(v1);
                    indices.push_back(v3);
                    indices.push_back(v2);
                }
            }
        }
    };

    // 1. Índices para LOD 0: Malla de máxima fidelidad (paso = 1, 40x40 quads = 1,600 quads, 9,600 índices)
    m_meshIndicesLOD0.clear();
    m_meshIndicesLOD0.reserve(NUM_INDICES_LOD0);
    appendSerpentineQuads(m_meshIndicesLOD0, 1);

    // 2. Índices para LOD 1: Malla intermedia adaptativa (paso = 2, 20x20 quads = 400 quads, 2,400 índices)
    m_meshIndicesLOD1.clear();
    m_meshIndicesLOD1.reserve(NUM_INDICES_LOD1);
    appendSerpentineQuads(m_meshIndicesLOD1, 2);

    // 3. Índices para LOD 2: Malla lejana de bajo consumo (paso = 4, 10x10 quads = 100 quads, 600 índices)
    m_meshIndicesLOD2.clear();
    m_meshIndicesLOD2.reserve(NUM_INDICES_LOD2);
    appendSerpentineQuads(m_meshIndicesLOD2, 4);

    m_renderVertices.resize(NUM_VERTICES);
    m_vertexDepthZ.resize(NUM_VERTICES);
    m_visibleIndices.reserve(NUM_INDICES_LOD0);
}

void SceneWater::UpdateWaveParameters() {
    // 4 Ondas trocoidales de Gerstner en superposición armónica (frecuencias desfasadas)
    m_waves.clear();
    m_waves.reserve(4);

    // Onda 0: Oleaje maestro diagonal (oleaje oceánico amplio)
    m_waves.push_back(GerstnerWaveParams{ 0.7071f,  0.7071f, 0.24f, 6.4f, 1.10f, 0.65f });
    // Onda 1: Oleaje cruzado transversal (contra-corriente)
    m_waves.push_back(GerstnerWaveParams{-0.6000f,  0.8000f, 0.14f, 3.8f, 1.35f, 0.55f });
    // Onda 2: Rizo de viento de alta frecuencia (oleaje superficial)
    m_waves.push_back(GerstnerWaveParams{ 0.8480f, -0.5300f, 0.07f, 1.9f, 1.80f, 0.45f });
    // Onda 3: Onda capilar rápida (micro-ondulaciones)
    m_waves.push_back(GerstnerWaveParams{-0.3800f, -0.9250f, 0.04f, 1.1f, 2.30f, 0.35f });

    // Multiplicador de energía de oleaje
    switch (m_waveIntensity) {
        case WaveIntensity::CALM:
            m_waveAmplitudeMultiplier = 0.40f;
            break;
        case WaveIntensity::MODERATE:
            m_waveAmplitudeMultiplier = 1.00f;
            break;
        case WaveIntensity::STORMY:
            m_waveAmplitudeMultiplier = 1.60f;
            break;
    }

    // Paletas de color por preset visual (Zelda: Breath of the Wild)
    switch (m_preset) {
        case WaterPreset::LAKE_HYLIA:
            // Lago Hylia: Azul zafiro profundo con orillas turquesa cristalinas
            m_waterDeepColor    = ColorRGBA{ 12,  52, 125, 255 };
            m_waterShallowColor = ColorRGBA{ 30, 215, 210, 255 };
            m_waterFoamColor    = ColorRGBA{ 245, 255, 255, 255 };
            break;
        case WaterPreset::LANAYRU_SPRING:
            // Manantial de Lanayru: Tonos esmeralda y menta mística
            m_waterDeepColor    = ColorRGBA{ 10,  75,  88, 255 };
            m_waterShallowColor = ColorRGBA{ 45, 235, 185, 255 };
            m_waterFoamColor    = ColorRGBA{ 235, 255, 248, 255 };
            break;
        case WaterPreset::LURELIN_OCEAN:
            // Mar de Ona / Aldea Ona: Azul marino tropical con olas vibrantes
            m_waterDeepColor    = ColorRGBA{  8,  35,  95, 255 };
            m_waterShallowColor = ColorRGBA{ 22, 190, 245, 255 };
            m_waterFoamColor    = ColorRGBA{ 250, 252, 255, 255 };
            break;
    }

    // Configuración atmosférica según la hora del día
    switch (m_timeOfDay) {
        case TimeOfDay::NOON:
            m_sunDir           = Point3D{0.45f, 0.82f, -0.35f}.Normalized();
            m_skyZenithColor   = ColorRGBA{ 30, 100, 205, 255 };
            m_skyHorizonColor  = ColorRGBA{ 170, 220, 248, 255 };
            m_sunColor         = ColorRGBA{ 255, 250, 215, 255 };
            break;
        case TimeOfDay::SUNSET:
            m_sunDir           = Point3D{0.78f, 0.28f, -0.56f}.Normalized();
            m_skyZenithColor   = ColorRGBA{ 60,  30,  88, 255 };
            m_skyHorizonColor  = ColorRGBA{ 245, 135,  45, 255 };
            m_sunColor         = ColorRGBA{ 255, 190, 105, 255 };
            break;
        case TimeOfDay::NIGHT:
            m_sunDir           = Point3D{-0.35f, 0.70f, 0.62f}.Normalized();
            m_skyZenithColor   = ColorRGBA{  8,  12,  30, 255 };
            m_skyHorizonColor  = ColorRGBA{ 22,  38,  75, 255 };
            m_sunColor         = ColorRGBA{ 215, 230, 255, 255 }; // Luz lunar plateada
            break;
    }
}

void SceneWater::UpdateCameraVectors() {
    float cosP = std::cos(m_camPitch);
    float sinP = std::sin(m_camPitch);
    float cosY = std::cos(m_camYaw);
    float sinY = std::sin(m_camYaw);

    Point3D offset{
        m_camDistance * sinY * cosP,
        m_camDistance * sinP,
        -m_camDistance * cosY * cosP
    };

    m_camPos = m_camPivot + offset;
    m_camForward = (m_camPivot - m_camPos).Normalized();
    Point3D worldUp{0.0f, 1.0f, 0.0f};
    m_camRight = Point3D::Cross(m_camForward, worldUp).Normalized();
    m_camUp = Point3D::Cross(m_camRight, m_camForward).Normalized();
}

void SceneWater::UpdateButtonsLabels() {
    switch (m_modeButton.GetRect().w > 0 ? m_renderMode : WaterRenderMode::SOLID_BOTW) {
        case WaterRenderMode::SOLID_BOTW:
            m_modeButton.SetLabel("MODO: SHADER HW (LATTE)");
            break;
        case WaterRenderMode::SMP_CPU_ESPRESSO:
            m_modeButton.SetLabel("MODO: SMP CPU (ESPRESSO)");
            break;
        case WaterRenderMode::WIREFRAME_MESH:
            m_modeButton.SetLabel("MODO: MALLA (WIREFRAME)");
            break;
        case WaterRenderMode::SOLID_PLUS_WIREFRAME:
            m_modeButton.SetLabel("MODO: SHADER + MALLA 3D");
            break;
    }

    switch (m_preset) {
        case WaterPreset::LAKE_HYLIA:
            m_presetButton.SetLabel("PRESET: LAGO HYLIA");
            break;
        case WaterPreset::LANAYRU_SPRING:
            m_presetButton.SetLabel("PRESET: MANANTIAL LANAYRU");
            break;
        case WaterPreset::LURELIN_OCEAN:
            m_presetButton.SetLabel("PRESET: MAR DE ONA");
            break;
    }

    switch (m_waveIntensity) {
        case WaveIntensity::CALM:
            m_waveButton.SetLabel("OLEAJE: CALMO");
            break;
        case WaveIntensity::MODERATE:
            m_waveButton.SetLabel("OLEAJE: MODERADO");
            break;
        case WaveIntensity::STORMY:
            m_waveButton.SetLabel("OLEAJE: TEMPESTAD");
            break;
    }

    switch (m_timeOfDay) {
        case TimeOfDay::NOON:
            m_timeButton.SetLabel("HORA: MEDIODIA (BOTW)");
            break;
        case TimeOfDay::SUNSET:
            m_timeButton.SetLabel("HORA: ATARDECER DORADO");
            break;
        case TimeOfDay::NIGHT:
            m_timeButton.SetLabel("HORA: NOCHE DE HYRULE");
            break;
    }

    m_foamButton.SetLabel(m_crestFoamEnabled ? "ESPUMA CRESTAS: ACTIVADA" : "ESPUMA CRESTAS: DESACTIVADA");
}

void SceneWater::OnEnter() {
    m_animTime = 0.0f;
    m_lastFrameTicks = SDL_GetTicks();
    m_smoothedFps = 60.0f;
    AudioManager::Instance().StopBGM();

    // Iniciar emisor de audio 3D para el oleaje ambiental
    AudioEmitter3D emitter;
    emitter.position = Point3D{0.0f, m_waterLevel, 0.0f};
    emitter.minDistance = 3.0f;
    emitter.maxDistance = 45.0f;
    emitter.rolloffFactor = 0.8f;
    emitter.volume = (m_waveIntensity == WaveIntensity::CALM) ? 0.5f : ((m_waveIntensity == WaveIntensity::MODERATE) ? 0.80f : 1.0f);
    emitter.pitch = (m_waveIntensity == WaveIntensity::CALM) ? 0.85f : ((m_waveIntensity == WaveIntensity::MODERATE) ? 1.0f : 1.15f);
    emitter.isLooping = true;
    emitter.priority = 70;
    m_waterSoundHandle = AudioEngine3D::Instance().PlaySound3D(
        AudioEngine3D::Instance().GetWaterAmbienceClip(), emitter);

    if (m_baseGrid.empty()) {
        InitGridGeometry();
        InitWorkerThread();
    }

#ifdef __wiiu__
    InitGX2WaterPipeline();
    RHIPipelineCache::Instance().Invalidate();
#endif
}

void SceneWater::OnExit() {
    if (m_waterSoundHandle.IsValid()) {
        AudioEngine3D::Instance().StopSound(m_waterSoundHandle);
        m_waterSoundHandle = SoundHandle{};
    }

    ShutdownWorkerThread();
#ifdef __wiiu__
    DestroyGX2WaterPipeline();
    RHIPipelineCache::Instance().Invalidate();
#endif
    // Desalojo proactivo de memoria en heap (OPT-45)
    m_baseGrid.clear();
    m_baseGrid.shrink_to_fit();
    m_renderVertices.clear();
    m_renderVertices.shrink_to_fit();
    m_visibleIndices.clear();
    m_visibleIndices.shrink_to_fit();
    m_vertexDepthZ.clear();
    m_vertexDepthZ.shrink_to_fit();
    m_meshIndicesLOD0.clear();
    m_meshIndicesLOD0.shrink_to_fit();
    m_meshIndicesLOD1.clear();
    m_meshIndicesLOD1.shrink_to_fit();
    m_meshIndicesLOD2.clear();
    m_meshIndicesLOD2.shrink_to_fit();
}

void SceneWater::CyclePreset() {
    switch (m_preset) {
        case WaterPreset::LAKE_HYLIA:
            m_preset = WaterPreset::LANAYRU_SPRING;
            break;
        case WaterPreset::LANAYRU_SPRING:
            m_preset = WaterPreset::LURELIN_OCEAN;
            break;
        case WaterPreset::LURELIN_OCEAN:
            m_preset = WaterPreset::LAKE_HYLIA;
            break;
    }
    UpdateWaveParameters();
    UpdateButtonsLabels();
}

void SceneWater::CycleWaveIntensity() {
    switch (m_waveIntensity) {
        case WaveIntensity::CALM:
            m_waveIntensity = WaveIntensity::MODERATE;
            break;
        case WaveIntensity::MODERATE:
            m_waveIntensity = WaveIntensity::STORMY;
            break;
        case WaveIntensity::STORMY:
            m_waveIntensity = WaveIntensity::CALM;
            break;
    }
    UpdateWaveParameters();
    UpdateButtonsLabels();
}

void SceneWater::CycleRenderMode() {
    switch (m_renderMode) {
        case WaterRenderMode::SOLID_BOTW:
            m_renderMode = WaterRenderMode::SMP_CPU_ESPRESSO;
            break;
        case WaterRenderMode::SMP_CPU_ESPRESSO:
            m_renderMode = WaterRenderMode::WIREFRAME_MESH;
            break;
        case WaterRenderMode::WIREFRAME_MESH:
            m_renderMode = WaterRenderMode::SOLID_PLUS_WIREFRAME;
            break;
        case WaterRenderMode::SOLID_PLUS_WIREFRAME:
            m_renderMode = WaterRenderMode::SOLID_BOTW;
            break;
    }
    UpdateButtonsLabels();
}

void SceneWater::CycleTimeOfDay() {
    switch (m_timeOfDay) {
        case TimeOfDay::NOON:
            m_timeOfDay = TimeOfDay::SUNSET;
            break;
        case TimeOfDay::SUNSET:
            m_timeOfDay = TimeOfDay::NIGHT;
            break;
        case TimeOfDay::NIGHT:
            m_timeOfDay = TimeOfDay::NOON;
            break;
    }
    UpdateWaveParameters();
    UpdateButtonsLabels();
}

void SceneWater::ToggleCrestFoam() {
    m_crestFoamEnabled = !m_crestFoamEnabled;
    UpdateButtonsLabels();
}

void SceneWater::ResetCamera() {
    m_camYaw = 0.0f;
    m_camPitch = 0.48f;
    m_camDistance = 16.0f;
    UpdateCameraVectors();
}

void SceneWater::Update(float dt) {
    m_animTime += dt;

    const auto& input = InputManager::Instance();
    const auto& touch = input.GetTouchState();

    // 1. Manejo de botones de la interfaz
    if (m_backButton.Update(touch, "VOLVER MENU") || input.IsBackRequested()) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuGX2>());
        return;
    }

    if (m_modeButton.Update(touch, "CYCLE MODO")) {
        AudioManager::Instance().PlayClick();
        CycleRenderMode();
        return;
    }

    if (m_presetButton.Update(touch, "CYCLE PRESET")) {
        AudioManager::Instance().PlayClick();
        CyclePreset();
        return;
    }

    if (m_waveButton.Update(touch, "CYCLE OLEAJE")) {
        AudioManager::Instance().PlayClick();
        CycleWaveIntensity();
        return;
    }

    if (m_timeButton.Update(touch, "CYCLE HORA")) {
        AudioManager::Instance().PlayClick();
        CycleTimeOfDay();
        return;
    }

    if (m_foamButton.Update(touch, "TOGGLE ESPUMA")) {
        AudioManager::Instance().PlayClick();
        ToggleCrestFoam();
        return;
    }

    if (m_camResetButton.Update(touch, "RESET CAMARA")) {
        AudioManager::Instance().PlayClick();
        ResetCamera();
        return;
    }

    // 2. Control de cámara orbital con GamePad Sticks (Wii U y Gamepad PC)
    const auto& stickL = input.GetLeftStick();
    const auto& stickR = input.GetRightStick();

    bool cameraMoved = false;

    if (std::abs(stickL.x) > 0.08f || std::abs(stickL.y) > 0.08f) {
        m_camYaw += stickL.x * 2.2f * dt;
        m_camPitch = std::clamp(m_camPitch - stickL.y * 1.5f * dt, 0.33f, 1.35f);
        cameraMoved = true;
    }

    if (std::abs(stickR.y) > 0.08f) {
        m_camDistance = std::clamp(m_camDistance - stickR.y * 10.0f * dt, 14.5f, 26.0f);
        cameraMoved = true;
    }

    // 3. Control de cámara por arrastre táctil / mouse fuera de los botones
    if (touch.active) {
        // Verificar que el toque no colisione con el área de los botones superiores (Y < 100)
        if (touch.y > 100.0f && touch.y < 610.0f) {
            if (touch.justPressed || !m_isDraggingCamera) {
                m_isDraggingCamera = true;
                m_lastTouchX = touch.x;
                m_lastTouchY = touch.y;
            } else {
                float dx = touch.x - m_lastTouchX;
                float dy = touch.y - m_lastTouchY;

                m_camYaw += dx * 0.0055f;
                m_camPitch = std::clamp(m_camPitch - dy * 0.0045f, 0.33f, 1.35f);

                m_lastTouchX = touch.x;
                m_lastTouchY = touch.y;
                cameraMoved = true;
            }
        }
    } else {
        m_isDraggingCamera = false;
    }

    if (cameraMoved) {
        UpdateCameraVectors();
    }

    // 4. Actualización del oyente de Audio 3D y atenuación acústica
    AudioListener3D listener;
    listener.position = m_camPos;
    listener.forward = m_camForward;
    listener.up = m_camUp;
    listener.velocity = Point3D{0.0f, 0.0f, 0.0f};
    listener.isSubmerged = (m_camPos.y < m_waterLevel);
    AudioEngine3D::Instance().SetListener(listener);

    if (m_waterSoundHandle.IsValid()) {
        AudioEmitter3D emitter;
        emitter.position = Point3D{0.0f, m_waterLevel, 0.0f};
        emitter.minDistance = 3.0f;
        emitter.maxDistance = 45.0f;
        emitter.rolloffFactor = 0.8f;
        float baseVol = (m_waveIntensity == WaveIntensity::CALM) ? 0.5f : ((m_waveIntensity == WaveIntensity::MODERATE) ? 0.80f : 1.0f);
        emitter.volume = baseVol;
        emitter.pitch = (m_waveIntensity == WaveIntensity::CALM) ? 0.85f : ((m_waveIntensity == WaveIntensity::MODERATE) ? 1.0f : 1.15f);
        emitter.isLooping = true;
        emitter.priority = 70;
        AudioEngine3D::Instance().UpdateEmitter(m_waterSoundHandle, emitter);
    }
}

void SceneWater::RenderSkyAndLandscape(SDL_Renderer* renderer) {
    if (!renderer) return;

    // Cálculo del horizonte en perspectiva (FOV vertical de 60°)
    float horizonY = 360.0f - std::tan(m_camPitch) * FOCAL_FACTOR;
    horizonY = std::clamp(horizonY, 0.0f, 720.0f);
    int hY = static_cast<int>(horizonY);

    // 1. Cielo atmosférico de Hyrule: Gradiente vertical de 1 sola llamada GPU
    if (horizonY > 1.0f) {
        SDL_Vertex skyVerts[4];
        SDL_Color zenithCol{m_skyZenithColor.r, m_skyZenithColor.g, m_skyZenithColor.b, 255};
        SDL_Color horizonCol{m_skyHorizonColor.r, m_skyHorizonColor.g, m_skyHorizonColor.b, 255};

        skyVerts[0] = { SDL_FPoint{0.0f, 0.0f}, zenithCol, SDL_FPoint{0.0f, 0.0f} };
        skyVerts[1] = { SDL_FPoint{1280.0f, 0.0f}, zenithCol, SDL_FPoint{0.0f, 0.0f} };
        skyVerts[2] = { SDL_FPoint{1280.0f, horizonY}, horizonCol, SDL_FPoint{0.0f, 0.0f} };
        skyVerts[3] = { SDL_FPoint{0.0f, horizonY}, horizonCol, SDL_FPoint{0.0f, 0.0f} };

        int skyIndices[6] = { 0, 1, 2, 0, 2, 3 };
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_RenderGeometry(renderer, nullptr, skyVerts, 4, skyIndices, 6);
    }

    // 2. Silueta estilizada de las montañas lejanas de Hyrule (Doble capa BotW: Picos Lejanos + Crestas Medias)
    if (horizonY > -80.0f && horizonY < 650.0f) {
        ColorRGBA mtFarCol = (m_timeOfDay == TimeOfDay::NIGHT)
            ? ColorRGBA{18, 28, 52, 255}
            : ((m_timeOfDay == TimeOfDay::SUNSET) ? ColorRGBA{115, 55, 75, 255} : ColorRGBA{70, 120, 165, 255});
        ColorRGBA mtMidCol = (m_timeOfDay == TimeOfDay::NIGHT)
            ? ColorRGBA{10, 18, 38, 255}
            : ((m_timeOfDay == TimeOfDay::SUNSET) ? ColorRGBA{80, 35, 55, 255} : ColorRGBA{45, 90, 130, 255});
        SDL_Color farSdlCol{mtFarCol.r, mtFarCol.g, mtFarCol.b, 255};
        SDL_Color midSdlCol{mtMidCol.r, mtMidCol.g, mtMidCol.b, 255};

        constexpr int MT_STEPS = 32;
        static const auto s_rayOffsetAngles = []() {
            std::array<float, MT_STEPS + 1> angles{};
            for (int s = 0; s <= MT_STEPS; ++s) {
                float frac = static_cast<float>(s) / static_cast<float>(MT_STEPS);
                float screenX = frac * 1280.0f;
                angles[s] = std::atan2(screenX - 640.0f, FOCAL_FACTOR);
            }
            return angles;
        }();

        // Capa 1: Picos Lejanos de Hyrule (Alpinos afilados, 100-190 px sobre horizonte)
        SDL_Vertex farVerts[(MT_STEPS + 1) * 2];
        int farIndices[MT_STEPS * 6];
        for (int s = 0; s <= MT_STEPS; ++s) {
            float frac = static_cast<float>(s) / static_cast<float>(MT_STEPS);
            float screenX = frac * 1280.0f;
            float rayAngle = m_camYaw + s_rayOffsetAngles[s];
            float ridgeFar = std::abs(std::sin(rayAngle * 1.5f)) * 95.0f + std::sin(rayAngle * 3.5f + 0.8f) * 45.0f + 65.0f;
            float topY = std::max(2.0f, horizonY - ridgeFar);
            float botY = std::min(720.0f, std::max(horizonY + 280.0f, 440.0f));

            farVerts[s * 2 + 0] = { SDL_FPoint{screenX, topY}, farSdlCol, SDL_FPoint{0.0f, 0.0f} };
            farVerts[s * 2 + 1] = { SDL_FPoint{screenX, botY}, farSdlCol, SDL_FPoint{0.0f, 0.0f} };

            if (s < MT_STEPS) {
                int base = s * 2;
                int idxBase = s * 6;
                farIndices[idxBase + 0] = base + 0;
                farIndices[idxBase + 1] = base + 1;
                farIndices[idxBase + 2] = base + 2;
                farIndices[idxBase + 3] = base + 1;
                farIndices[idxBase + 4] = base + 3;
                farIndices[idxBase + 5] = base + 2;
            }
        }
        SDL_RenderGeometry(renderer, nullptr, farVerts, (MT_STEPS + 1) * 2, farIndices, MT_STEPS * 6);

        // Capa 2: Crestas Intermedias del Lago Hylia (50-110 px sobre horizonte, base profunda)
        SDL_Vertex midVerts[(MT_STEPS + 1) * 2];
        int midIndices[MT_STEPS * 6];
        for (int s = 0; s <= MT_STEPS; ++s) {
            float frac = static_cast<float>(s) / static_cast<float>(MT_STEPS);
            float screenX = frac * 1280.0f;
            float rayAngle = m_camYaw + s_rayOffsetAngles[s];
            float ridgeMid = std::abs(std::sin(rayAngle * 2.2f + 1.2f)) * 55.0f + std::cos(rayAngle * 4.5f) * 25.0f + 35.0f;
            float topY = std::max(10.0f, horizonY - ridgeMid);
            float botY = std::min(720.0f, std::max(horizonY + 360.0f, 520.0f));

            midVerts[s * 2 + 0] = { SDL_FPoint{screenX, topY}, midSdlCol, SDL_FPoint{0.0f, 0.0f} };
            midVerts[s * 2 + 1] = { SDL_FPoint{screenX, botY}, midSdlCol, SDL_FPoint{0.0f, 0.0f} };

            if (s < MT_STEPS) {
                int base = s * 2;
                int idxBase = s * 6;
                midIndices[idxBase + 0] = base + 0;
                midIndices[idxBase + 1] = base + 1;
                midIndices[idxBase + 2] = base + 2;
                midIndices[idxBase + 3] = base + 1;
                midIndices[idxBase + 4] = base + 3;
                midIndices[idxBase + 5] = base + 2;
            }
        }
        SDL_RenderGeometry(renderer, nullptr, midVerts, (MT_STEPS + 1) * 2, midIndices, MT_STEPS * 6);
    }

    // 3. Proyección perspectiva 3D del Sol / Luna: 1 sola llamada GPU
    float sunCz = Point3D::Dot(m_sunDir, m_camForward);
    if (sunCz > 0.05f) {
        float sunCx = Point3D::Dot(m_sunDir, m_camRight);
        float sunCy = Point3D::Dot(m_sunDir, m_camUp);
        float invCz = 1.0f / sunCz;
        int sunScreenX = static_cast<int>(640.0f + (sunCx * invCz) * FOCAL_FACTOR);
        int sunScreenY = static_cast<int>(360.0f - (sunCy * invCz) * FOCAL_FACTOR);

        if (sunScreenX >= -80 && sunScreenX <= 1360 && sunScreenY >= -80 && sunScreenY <= hY + 40) {
            constexpr int SUN_SEGS = 12;
            SDL_Vertex sunVerts[SUN_SEGS + 1];
            int sunIndices[SUN_SEGS * 3];

            sunVerts[0] = { SDL_FPoint{static_cast<float>(sunScreenX), static_cast<float>(sunScreenY)},
                            SDL_Color{m_sunColor.r, m_sunColor.g, m_sunColor.b, 255}, SDL_FPoint{0.0f, 0.0f} };

            for (int s = 0; s < SUN_SEGS; ++s) {
                float angle = static_cast<float>(s) * (6.2831853f / static_cast<float>(SUN_SEGS));
                float px = static_cast<float>(sunScreenX) + std::cos(angle) * 24.0f;
                float py = static_cast<float>(sunScreenY) + std::sin(angle) * 24.0f;
                sunVerts[s + 1] = { SDL_FPoint{px, py},
                                    SDL_Color{m_sunColor.r, m_sunColor.g, m_sunColor.b, 150}, SDL_FPoint{0.0f, 0.0f} };

                sunIndices[s * 3 + 0] = 0;
                sunIndices[s * 3 + 1] = s + 1;
                sunIndices[s * 3 + 2] = (s + 1 == SUN_SEGS) ? 1 : (s + 2);
            }

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(renderer, nullptr, sunVerts, SUN_SEGS + 1, sunIndices, SUN_SEGS * 3);
        }
    }
}

void SceneWater::InitWorkerThread() {
    m_stopWorker.store(false, std::memory_order_relaxed);
    m_targetFrame.store(0, std::memory_order_relaxed);
    m_workerDoneFrame.store(0, std::memory_order_relaxed);

#ifdef __wiiu__
    OSInitEvent(&m_jobEvent, FALSE, OS_EVENT_MODE_AUTO);
    OSInitEvent(&m_doneEvent, FALSE, OS_EVENT_MODE_AUTO);
#else
    m_jobReady = false;
#endif

    m_workerThread = std::thread(&SceneWater::WorkerLoop, this);
}

void SceneWater::ShutdownWorkerThread() {
    m_stopWorker.store(true, std::memory_order_release);
    m_targetFrame.fetch_add(1, std::memory_order_release);

#ifdef __wiiu__
    OSSignalEvent(&m_jobEvent);
#else
    {
        std::lock_guard<std::mutex> lock(m_syncMutex);
        m_jobReady = true;
    }
    m_syncCv.notify_all();
#endif

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void SceneWater::WorkerLoop() {
#ifdef __wiiu__
    // Asignar afinidad estricta al Core 2 de Espresso (Core 0 = SO/DMA, Core 1 = Hilo Principal)
    OSSetThreadAffinity(OSGetCurrentThread(), 1 << 2);
#endif

    int lastProcessedFrame = 0;
    while (!m_stopWorker.load(std::memory_order_relaxed)) {
#ifdef __wiiu__
        // Suspensión de kernel nativa en Cafe OS: Core 2 duerme al 0% CPU sin saturar el bus MESI
        OSWaitEvent(&m_jobEvent);
#else
        {
            std::unique_lock<std::mutex> lock(m_syncMutex);
            m_syncCv.wait(lock, [this]() {
                return m_jobReady || m_stopWorker.load(std::memory_order_relaxed);
            });
            m_jobReady = false;
        }
#endif

        if (m_stopWorker.load(std::memory_order_relaxed)) {
            break;
        }

        int currentTarget = m_targetFrame.load(std::memory_order_acquire);
        if (currentTarget > lastProcessedFrame) {
            lastProcessedFrame = currentTarget;

            // Core 2 procesa en paralelo la segunda mitad exacta de la cuadrícula [220 .. 441)
            constexpr size_t HALF_VERTICES = NUM_VERTICES / 2;
            ComputeVertexSlice(HALF_VERTICES, NUM_VERTICES, m_activeSimParams);

            m_workerDoneFrame.store(currentTarget, std::memory_order_release);

#ifdef __wiiu__
            OSSignalEvent(&m_doneEvent);
#else
            m_syncCv.notify_all();
#endif
        }
    }
}

void SceneWater::ComputeVertexSlice(size_t startIdx, size_t endIdx, const WaterSimParams& params) {
    for (size_t i = startIdx; i < endIdx; ++i) {
        const float x0 = m_baseGrid[i].x;
        const float z0 = m_baseGrid[i].z;

        Point3D pos{x0, m_waterLevel, z0};
        Point3D normal{0.0f, 1.0f, 0.0f};

        for (size_t wIdx = 0; wIdx < params.numWaves; ++wIdx) {
            const auto& pw = params.waves[wIdx];
            float phase = (pw.dirX * x0 + pw.dirZ * z0) * pw.k + pw.phaseTime;
            float cosP = std::cos(phase);
            float sinP = std::sin(phase);

            // Desplazamiento trocoidal horizontal (X, Z) y vertical (Y)
            pos.x += pw.qAmp * pw.dirX * cosP;
            pos.y += pw.amp * sinP;
            pos.z += pw.qAmp * pw.dirZ * cosP;

            // Vector normal analítico
            normal.x -= pw.dirX * pw.kAmp * cosP;
            normal.y -= pw.qkAmp * sinP;
            normal.z -= pw.dirZ * pw.kAmp * cosP;
        }

        // Normalización ultra-rápida vía frsqrte de hardware
        float normLenSq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
        float invNormLen = Point3D::FastInvSqrt(normLenSq);
        normal.x *= invNormLen;
        normal.y *= invNormLen;
        normal.z *= invNormLen;

        // 2. Coloración Cel-shaded y Fresnel inspirada en Breath of the Wild
        float elevRatio = std::clamp((pos.y - m_waterLevel) / params.maxAmp, -1.0f, 1.0f);
        float tElev = elevRatio * 0.5f + 0.5f; // [0.0 = fondo valle, 1.0 = cresta máxima]

        // Gradiente de agua profunda -> orilla turquesa superficial
        float r = m_waterDeepColor.r * (1.0f - tElev) + m_waterShallowColor.r * tElev;
        float g = m_waterDeepColor.g * (1.0f - tElev) + m_waterShallowColor.g * tElev;
        float b = m_waterDeepColor.b * (1.0f - tElev) + m_waterShallowColor.b * tElev;

        // Vector de visión y Fresnel de Schlick (Reflejo del cielo rasante)
        Point3D viewDir = params.camPos - pos;
        float vLenSq = viewDir.x * viewDir.x + viewDir.y * viewDir.y + viewDir.z * viewDir.z;
        float invVLen = Point3D::FastInvSqrt(vLenSq);
        float NdotV = std::clamp((normal.x * viewDir.x + normal.y * viewDir.y + normal.z * viewDir.z) * invVLen, 0.0f, 1.0f);
        float oneMinusNV = 1.0f - NdotV;
        float fresnel = 0.05f + 0.95f * (oneMinusNV * oneMinusNV * oneMinusNV * oneMinusNV);

        r = r * (1.0f - fresnel * 0.5f) + m_skyHorizonColor.r * (fresnel * 0.5f);
        g = g * (1.0f - fresnel * 0.5f) + m_skyHorizonColor.g * (fresnel * 0.5f);
        b = b * (1.0f - fresnel * 0.5f) + m_skyHorizonColor.b * (fresnel * 0.5f);

        // Espuma procedural en las crestas trocoidales afiladas (Crest Foam de BotW)
        if (m_crestFoamEnabled) {
            float slopeFactor = 1.0f - normal.y;
            if (tElev > 0.60f || slopeFactor > 0.30f) {
                float elevFoam = (tElev - 0.60f) / 0.35f;
                float slopeFoam = (slopeFactor - 0.30f) / 0.35f;
                float foamAmount = std::clamp(std::max(elevFoam, slopeFoam), 0.0f, 1.0f);
                r = r * (1.0f - foamAmount * 0.85f) + m_waterFoamColor.r * (foamAmount * 0.85f);
                g = g * (1.0f - foamAmount * 0.85f) + m_waterFoamColor.g * (foamAmount * 0.85f);
                b = b * (1.0f - foamAmount * 0.85f) + m_waterFoamColor.b * (foamAmount * 0.85f);
            }
        }

        // Destello solar especular Blinn-Phong vía exponenciación binaria (cero llamadas a std::pow)
        float NdotH = std::max(0.0f, normal.x * params.halfDir.x + normal.y * params.halfDir.y + normal.z * params.halfDir.z);
        if (NdotH > 0.65f) {
            float s2 = NdotH * NdotH;
            float s4 = s2 * s2;
            float s8 = s4 * s4;
            float s16 = s8 * s8;
            float s32 = s16 * s16;
            float spec = s32 * s4 * 1.8f;
            r = std::min(255.0f, r + m_sunColor.r * spec);
            g = std::min(255.0f, g + m_sunColor.g * spec);
            b = std::min(255.0f, b + m_sunColor.b * spec);
        }

        // 3. Proyección en perspectiva 3D al espacio de cámara
        Point3D rel = pos - params.camPos;
        float cz = Point3D::Dot(rel, params.camForward);
        m_vertexDepthZ[i] = cz;

        float screenX = 0.0f;
        float screenY = 0.0f;

        if (cz > NEAR_PLANE_Z) {
            float cx = Point3D::Dot(rel, params.camRight);
            float cy = Point3D::Dot(rel, params.camUp);
            float invZ = 1.0f / cz;
            screenX = 640.0f + (cx * invZ) * FOCAL_FACTOR;
            screenY = 360.0f - (cy * invZ) * FOCAL_FACTOR;
        }

        SDL_Color vertexColor{
            static_cast<Uint8>(std::clamp(r, 0.0f, 255.0f)),
            static_cast<Uint8>(std::clamp(g, 0.0f, 255.0f)),
            static_cast<Uint8>(std::clamp(b, 0.0f, 255.0f)),
            255
        };

        m_renderVertices[i] = SDL_Vertex{
            SDL_FPoint{screenX, screenY},
            vertexColor,
            SDL_FPoint{0.0f, 0.0f}
        };
    }
}

bool SceneWater::InitGX2WaterPipeline() {
#ifdef __wiiu__
    if (m_gx2ShaderLoaded && m_gx2BuffersCreated && m_nativeContextState) {
        return true;
    }

    if (!m_gx2ShaderLoaded) {
        std::vector<uint8_t> buffer;
        bool loaded = AssetManager::LoadBinaryFile("shaders/water.gsh", buffer);
        if (!loaded || buffer.empty()) {
            buffer.assign(WATER_GSH_DATA, WATER_GSH_DATA + WATER_GSH_SIZE);
            loaded = true;
        }

        if (loaded && !buffer.empty()) {
            // Direct GFD sin dependencia de heaps de WHB
            uint32_t vsHeaderSize = GFDGetVertexShaderHeaderSize(0, buffer.data());
            uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, buffer.data());
            uint32_t psHeaderSize = GFDGetPixelShaderHeaderSize(0, buffer.data());
            uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, buffer.data());

                if (vsHeaderSize > 0 && vsProgSize > 0 && psHeaderSize > 0 && psProgSize > 0) {
                    m_directVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHeaderSize, MemoryTag::ShaderProgram, "Water_VS_Hdr"));
                    m_directVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "Water_VS_Prog");
                    m_directPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHeaderSize, MemoryTag::ShaderProgram, "Water_PS_Hdr"));
                    m_directPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "Water_PS_Prog");

                    if (m_directVs && m_directVsProgram && m_directPs && m_directPsProgram) {
                        BOOL vsOk = GFDGetVertexShader(m_directVs, m_directVsProgram, 0, buffer.data());
                        BOOL psOk = GFDGetPixelShader(m_directPs, m_directPsProgram, 0, buffer.data());
                        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directVsProgram, vsProgSize);
                        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directPsProgram, psProgSize);

                        if (vsOk && psOk) {
                            GX2AttribStream attr;
                            std::memset(&attr, 0, sizeof(attr));
                            attr.location = 0;
                            attr.buffer = 0;
                            attr.offset = 0;
                            attr.format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
                            attr.type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attr.aluDivisor = 0;
                            attr.mask = 0x00010405;
                            attr.endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            uint32_t fsSize = GX2CalcFetchShaderSizeEx(1, GX2_FETCH_SHADER_TESSELLATION_NONE,
                                                                       GX2_TESSELLATION_MODE_DISCRETE);
                            m_directFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "Water_FS_Prog");
                            if (m_directFsProgram) {
                                GX2InitFetchShaderEx(&m_directFetchShader, static_cast<uint8_t*>(m_directFsProgram), 1, &attr,
                                                     GX2_FETCH_SHADER_TESSELLATION_NONE,
                                                     GX2_TESSELLATION_MODE_DISCRETE);
                                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directFsProgram, fsSize);
                                m_useDirectGFD = true;
                                m_gx2ShaderLoaded = true;
                            }
                        }
                    }
                }
            }
        }

    if (m_gx2ShaderLoaded && !m_gx2BuffersCreated) {
        m_gridVertexBuffer.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_gridVertexBuffer.elemSize = sizeof(BaseVertex);
        m_gridVertexBuffer.elemCount = NUM_VERTICES;

        if (GX2RCreateBuffer(&m_gridVertexBuffer)) {
            void* ptr = GX2RLockBufferEx(&m_gridVertexBuffer, GX2R_RESOURCE_BIND_NONE);
            if (ptr) {
                std::memcpy(ptr, m_baseGrid.data(), sizeof(BaseVertex) * NUM_VERTICES);
                GX2RUnlockBufferEx(&m_gridVertexBuffer, GX2R_RESOURCE_BIND_NONE);
            }
        }

        std::vector<uint16_t> idx16LOD0(NUM_INDICES_LOD0);
        for (size_t i = 0; i < NUM_INDICES_LOD0; ++i) {
            idx16LOD0[i] = static_cast<uint16_t>(m_meshIndicesLOD0[i]);
        }
        m_indexBufferLOD0.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_INDEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_indexBufferLOD0.elemSize = sizeof(uint16_t);
        m_indexBufferLOD0.elemCount = NUM_INDICES_LOD0;
        if (GX2RCreateBuffer(&m_indexBufferLOD0)) {
            void* ptr = GX2RLockBufferEx(&m_indexBufferLOD0, GX2R_RESOURCE_BIND_NONE);
            if (ptr) {
                std::memcpy(ptr, idx16LOD0.data(), sizeof(uint16_t) * NUM_INDICES_LOD0);
                GX2RUnlockBufferEx(&m_indexBufferLOD0, GX2R_RESOURCE_BIND_NONE);
            }
        }

        std::vector<uint16_t> idx16LOD1(NUM_INDICES_LOD1);
        for (size_t i = 0; i < NUM_INDICES_LOD1; ++i) {
            idx16LOD1[i] = static_cast<uint16_t>(m_meshIndicesLOD1[i]);
        }
        m_indexBufferLOD1.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_INDEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_indexBufferLOD1.elemSize = sizeof(uint16_t);
        m_indexBufferLOD1.elemCount = NUM_INDICES_LOD1;
        if (GX2RCreateBuffer(&m_indexBufferLOD1)) {
            void* ptr = GX2RLockBufferEx(&m_indexBufferLOD1, GX2R_RESOURCE_BIND_NONE);
            if (ptr) {
                std::memcpy(ptr, idx16LOD1.data(), sizeof(uint16_t) * NUM_INDICES_LOD1);
                GX2RUnlockBufferEx(&m_indexBufferLOD1, GX2R_RESOURCE_BIND_NONE);
            }
        }

        std::vector<uint16_t> idx16LOD2(NUM_INDICES_LOD2);
        for (size_t i = 0; i < NUM_INDICES_LOD2; ++i) {
            idx16LOD2[i] = static_cast<uint16_t>(m_meshIndicesLOD2[i]);
        }
        m_indexBufferLOD2.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_INDEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_indexBufferLOD2.elemSize = sizeof(uint16_t);
        m_indexBufferLOD2.elemCount = NUM_INDICES_LOD2;
        if (GX2RCreateBuffer(&m_indexBufferLOD2)) {
            void* ptr = GX2RLockBufferEx(&m_indexBufferLOD2, GX2R_RESOURCE_BIND_NONE);
            if (ptr) {
                std::memcpy(ptr, idx16LOD2.data(), sizeof(uint16_t) * NUM_INDICES_LOD2);
                GX2RUnlockBufferEx(&m_indexBufferLOD2, GX2R_RESOURCE_BIND_NONE);
            }
        }

        m_gx2BuffersCreated = true;
    }

    if (!m_nativeContextState) {
        m_nativeContextState = static_cast<GX2ContextState*>(MemoryManager::Instance().AllocAligned(
            GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState), MemoryTag::ContextState, "Water_ContextState"));
        if (m_nativeContextState) {
            GX2SetupContextStateEx(m_nativeContextState, FALSE);
            GX2SetContextState(m_nativeContextState);
            GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
        }
    }

    return m_gx2ShaderLoaded && m_gx2BuffersCreated && (m_nativeContextState != nullptr);
#else
    return false;
#endif
}

void SceneWater::DestroyGX2WaterPipeline() {
#ifdef __wiiu__
    if (m_nativeContextState) {
        void* rawCtx = static_cast<void*>(m_nativeContextState);
        MemoryManager::Instance().FreeAligned(rawCtx);
        m_nativeContextState = nullptr;
    }
    m_pColorBuffer = nullptr;
    m_colorBufferBound = false;

    if (m_gx2BuffersCreated) {
        GX2RDestroyBufferEx(&m_indexBufferLOD2, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_indexBufferLOD1, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_indexBufferLOD0, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_gridVertexBuffer, GX2R_RESOURCE_BIND_NONE);
        m_gx2BuffersCreated = false;
    }
    if (m_useDirectGFD) {
        if (m_directFsProgram) { MemoryManager::Instance().FreeAligned(m_directFsProgram); }
        if (m_directPsProgram) { MemoryManager::Instance().FreeAligned(m_directPsProgram); }
        if (m_directPs) { void* raw = static_cast<void*>(m_directPs); MemoryManager::Instance().Free(raw); m_directPs = nullptr; }
        if (m_directVsProgram) { MemoryManager::Instance().FreeAligned(m_directVsProgram); }
        if (m_directVs) { void* raw = static_cast<void*>(m_directVs); MemoryManager::Instance().Free(raw); m_directVs = nullptr; }
        m_useDirectGFD = false;
        m_gx2ShaderLoaded = false;
    } else if (m_gx2ShaderLoaded) {
        WHBGfxFreeShaderGroup(&m_waterShaderGroup);
        m_gx2ShaderLoaded = false;
    }
#endif
}

void SceneWater::BindColorBuffer(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!renderer) return;

    m_pColorBuffer = GetSDL2ColorBuffer(renderer);
    if (m_pColorBuffer && m_nativeContextState) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
        m_colorBufferBound = true;
    }
#else
    (void)renderer;
#endif
}

void SceneWater::RenderWaterMeshGX2() {
#ifdef __wiiu__
    if (!m_gx2ShaderLoaded || !m_gx2BuffersCreated) {
        return;
    }

    // 1. Configurar Uniforms del Vertex Shader (44 floats = 11 registros KC2[0 .. 10])
    float vsUniforms[44];

    // KC2[0]: uCamPos
    vsUniforms[0] = m_camPos.x;
    vsUniforms[1] = m_camPos.y;
    vsUniforms[2] = m_camPos.z;
    vsUniforms[3] = 0.0f;

    // KC2[1]: uCamForward
    vsUniforms[4] = m_camForward.x;
    vsUniforms[5] = m_camForward.y;
    vsUniforms[6] = m_camForward.z;
    vsUniforms[7] = 0.0f;

    // KC2[2]: uCamRight
    vsUniforms[8]  = m_camRight.x;
    vsUniforms[9]  = m_camRight.y;
    vsUniforms[10] = m_camRight.z;
    vsUniforms[11] = 0.0f;

    // KC2[3]: uCamUp
    vsUniforms[12] = m_camUp.x;
    vsUniforms[13] = m_camUp.y;
    vsUniforms[14] = m_camUp.z;
    vsUniforms[15] = 0.0f;

    // KC2[4]: uFocal
    vsUniforms[16] = FOCAL_FACTOR / 640.0f;
    vsUniforms[17] = FOCAL_FACTOR / 360.0f;
    vsUniforms[18] = 0.0f;
    vsUniforms[19] = 0.0f;

    // KC2[5 .. 8]: 4 Ondas de Gerstner
    for (int w = 0; w < 4; ++w) {
        int baseIdx = 20 + w * 4;
        if (w < static_cast<int>(m_waves.size())) {
            vsUniforms[baseIdx + 0] = m_waves[w].dirX;
            vsUniforms[baseIdx + 1] = m_waves[w].dirZ;
            vsUniforms[baseIdx + 2] = m_waves[w].amplitude;
            vsUniforms[baseIdx + 3] = m_waves[w].wavelength;
        } else {
            vsUniforms[baseIdx + 0] = 0.0f;
            vsUniforms[baseIdx + 1] = 0.0f;
            vsUniforms[baseIdx + 2] = 0.0f;
            vsUniforms[baseIdx + 3] = 1.0f;
        }
    }

    // KC2[9]: uSimParams
    vsUniforms[36] = m_animTime;
    vsUniforms[37] = m_waterLevel;
    vsUniforms[38] = m_waveAmplitudeMultiplier;
    vsUniforms[39] = static_cast<float>(m_waves.size());

    // KC2[10]: uWaveSpeeds
    vsUniforms[40] = (m_waves.size() > 0) ? m_waves[0].speed : 1.0f;
    vsUniforms[41] = (m_waves.size() > 1) ? m_waves[1].speed : 1.0f;
    vsUniforms[42] = (m_waves.size() > 2) ? m_waves[2].speed : 1.0f;
    vsUniforms[43] = (m_waves.size() > 3) ? m_waves[3].speed : 1.0f;

    // 2. Configurar Uniforms del Pixel Shader (32 floats = 8 registros KC2[0 .. 7])
    float psUniforms[32];

    // KC2[0]: uSunDir
    psUniforms[0] = m_sunDir.x;
    psUniforms[1] = m_sunDir.y;
    psUniforms[2] = m_sunDir.z;
    psUniforms[3] = 0.0f;

    // KC2[1]: uSunColor
    psUniforms[4] = static_cast<float>(m_sunColor.r) * (1.0f / 255.0f);
    psUniforms[5] = static_cast<float>(m_sunColor.g) * (1.0f / 255.0f);
    psUniforms[6] = static_cast<float>(m_sunColor.b) * (1.0f / 255.0f);
    psUniforms[7] = 1.0f;

    // KC2[2]: uWaterDeepColor
    psUniforms[8]  = static_cast<float>(m_waterDeepColor.r) * (1.0f / 255.0f);
    psUniforms[9]  = static_cast<float>(m_waterDeepColor.g) * (1.0f / 255.0f);
    psUniforms[10] = static_cast<float>(m_waterDeepColor.b) * (1.0f / 255.0f);
    psUniforms[11] = 1.0f;

    // KC2[3]: uWaterShallowColor
    psUniforms[12] = static_cast<float>(m_waterShallowColor.r) * (1.0f / 255.0f);
    psUniforms[13] = static_cast<float>(m_waterShallowColor.g) * (1.0f / 255.0f);
    psUniforms[14] = static_cast<float>(m_waterShallowColor.b) * (1.0f / 255.0f);
    psUniforms[15] = 1.0f;

    // KC2[4]: uWaterFoamColor
    psUniforms[16] = static_cast<float>(m_waterFoamColor.r) * (1.0f / 255.0f);
    psUniforms[17] = static_cast<float>(m_waterFoamColor.g) * (1.0f / 255.0f);
    psUniforms[18] = static_cast<float>(m_waterFoamColor.b) * (1.0f / 255.0f);
    psUniforms[19] = 1.0f;

    // KC2[5]: uSkyHorizonColor
    psUniforms[20] = static_cast<float>(m_skyHorizonColor.r) * (1.0f / 255.0f);
    psUniforms[21] = static_cast<float>(m_skyHorizonColor.g) * (1.0f / 255.0f);
    psUniforms[22] = static_cast<float>(m_skyHorizonColor.b) * (1.0f / 255.0f);
    psUniforms[23] = 1.0f;

    // KC2[6]: uCamPos (xyz = pos, w = maxAmp)
    float maxAmp = 0.01f;
    for (const auto& w : m_waves) {
        maxAmp += w.amplitude * m_waveAmplitudeMultiplier;
    }
    psUniforms[24] = m_camPos.x;
    psUniforms[25] = m_camPos.y;
    psUniforms[26] = m_camPos.z;
    psUniforms[27] = maxAmp;

    // KC2[7]: uShadingParams (x = foamEnabled, y = waterLevel)
    psUniforms[28] = m_crestFoamEnabled ? 1.0f : 0.0f;
    psUniforms[29] = m_waterLevel;
    psUniforms[30] = 0.0f;
    psUniforms[31] = 0.0f;

    // 3. Vincular Shaders y Subir Uniforms a la GPU AMD Latte
    if (m_useDirectGFD) {
        GX2SetFetchShader(&m_directFetchShader);
        GX2SetVertexShader(m_directVs);
        GX2SetPixelShader(m_directPs);
    } else {
        GX2SetFetchShader(&m_waterShaderGroup.fetchShader);
        GX2SetVertexShader(m_waterShaderGroup.vertexShader);
        GX2SetPixelShader(m_waterShaderGroup.pixelShader);
    }

    GX2SetVertexUniformReg(0, 44, vsUniforms);
    GX2SetPixelUniformReg(0, 32, psUniforms);

    // 4. Vincular Vertex Buffer de la Cuadrícula
    GX2RSetAttributeBuffer(&m_gridVertexBuffer, 0, sizeof(BaseVertex), 0);

    // 5. Configurar Estado de Rasterización y Mezcla en GPU mediante RHI Pipeline Cache
    RHIPipelineState state(false, false, RHI_COMPARE_ALWAYS, BlendMode::Opaque, false, RHI_FRONT_FACE_CCW);
    RHIPipelineCache::Instance().BindState(state);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);

    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);

    // 6. Selección Dinámica de 3 LODs y Dibujo Indexado Directo en GPU
    GX2RBuffer* activeIndexBuffer = &m_indexBufferLOD0;
    uint32_t activeIndexCount = NUM_INDICES_LOD0;
    if (m_camDistance >= 21.0f) {
        activeIndexBuffer = &m_indexBufferLOD2;
        activeIndexCount = NUM_INDICES_LOD2;
    } else if (m_camDistance >= 16.5f) {
        activeIndexBuffer = &m_indexBufferLOD1;
        activeIndexCount = NUM_INDICES_LOD1;
    }

    GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, activeIndexBuffer, GX2_INDEX_TYPE_U16, activeIndexCount, 0, 0, 1);
    GX2Flush();
#endif
}

void SceneWater::RenderWaterWireframeOnly(SDL_Renderer* renderer, int lodLevel) {
    ColorRGBA wireColor = (m_renderMode == WaterRenderMode::WIREFRAME_MESH)
        ? ColorRGBA{0, 255, 230, 230}
        : ColorRGBA{255, 255, 255, 140};

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, wireColor.r, wireColor.g, wireColor.b, wireColor.a);

    const int step = (lodLevel == 2) ? 4 : ((lodLevel == 1) ? 2 : 1);
    SDL_FPoint strip[GRID_SIZE_X];

    // A. Trazar líneas horizontales continuas (filas Z)
    for (int gz = 0; gz < GRID_SIZE_Z; gz += step) {
        int ptIdx = 0;
        for (int gx = 0; gx < GRID_SIZE_X; gx += step) {
            strip[ptIdx++] = m_renderVertices[gz * GRID_SIZE_X + gx].position;
        }
        SDL_RenderDrawLinesF(renderer, strip, ptIdx);
    }

    // B. Trazar líneas verticales continuas (columnas X)
    for (int gx = 0; gx < GRID_SIZE_X; gx += step) {
        int ptIdx = 0;
        for (int gz = 0; gz < GRID_SIZE_Z; gz += step) {
            strip[ptIdx++] = m_renderVertices[gz * GRID_SIZE_X + gx].position;
        }
        SDL_RenderDrawLinesF(renderer, strip, ptIdx);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void SceneWater::RenderWaterMeshCPU(SDL_Renderer* renderer) {
    // Altura máxima de onda para normalizar coloración por elevación
    float maxAmp = 0.01f;
    for (const auto& w : m_waves) {
        maxAmp += w.amplitude * m_waveAmplitudeMultiplier;
    }

    // Pre-cálculo de invariantes de onda fuera del bucle de vértices
    PrecomputedWave pWaves[4];
    const size_t numWaves = std::min(m_waves.size(), size_t{4});
    for (size_t wIdx = 0; wIdx < numWaves; ++wIdx) {
        const auto& w = m_waves[wIdx];
        float k = 6.2831853f / w.wavelength;
        float wSpeed = std::sqrt(9.81f * k) * w.speed;
        float amp = w.amplitude * m_waveAmplitudeMultiplier;
        float q = std::min(1.0f, w.steepness / (k * std::max(0.001f, amp) * 4.0f));

        pWaves[wIdx].dirX = w.dirX;
        pWaves[wIdx].dirZ = w.dirZ;
        pWaves[wIdx].k = k;
        pWaves[wIdx].phaseTime = m_animTime * wSpeed;
        pWaves[wIdx].amp = amp;
        pWaves[wIdx].qAmp = q * amp;
        pWaves[wIdx].kAmp = k * amp;
        pWaves[wIdx].qkAmp = q * (k * amp);
    }

    // Vector medio de luz solar constante para iluminación direccional de Hyrule
    Point3D camViewDir = (m_camPivot - m_camPos).Normalized();
    Point3D halfDir = (m_sunDir + camViewDir).Normalized();

    // Sincronización SMP: Configurar contexto de parámetros compartido para Core 1 y Core 2
    m_activeSimParams.numWaves = numWaves;
    m_activeSimParams.maxAmp = maxAmp;
    m_activeSimParams.halfDir = halfDir;
    m_activeSimParams.camPos = m_camPos;
    m_activeSimParams.camForward = m_camForward;
    m_activeSimParams.camRight = m_camRight;
    m_activeSimParams.camUp = m_camUp;
    for (size_t wIdx = 0; wIdx < numWaves; ++wIdx) {
        m_activeSimParams.waves[wIdx] = pWaves[wIdx];
    }

    // 1. Resetear el evento de finalización para purgar cualquier señal residual antes de despachar Core 2
#ifdef __wiiu__
    OSResetEvent(&m_doneEvent);
#endif

    int target = m_targetFrame.fetch_add(1, std::memory_order_release) + 1;

#ifdef __wiiu__
    OSSignalEvent(&m_jobEvent);
#else
    {
        std::lock_guard<std::mutex> lock(m_syncMutex);
        m_jobReady = true;
    }
    m_syncCv.notify_one();
#endif

    // 2. Hilo Principal (Core 1) procesa simultáneamente la primera mitad [0 .. 220)
    constexpr size_t HALF_VERTICES = NUM_VERTICES / 2;
    ComputeVertexSlice(0, HALF_VERTICES, m_activeSimParams);

    // 3. Barrera de sincronización lock-free: bucle while estricto hasta que Core 2 alcance el target
    while (m_workerDoneFrame.load(std::memory_order_acquire) < target) {
#ifdef __wiiu__
        OSWaitEvent(&m_doneEvent);
#else
        std::unique_lock<std::mutex> lock(m_syncMutex);
        m_syncCv.wait(lock, [this, target]() {
            return m_workerDoneFrame.load(std::memory_order_acquire) >= target || m_stopWorker.load(std::memory_order_relaxed);
        });
#endif
    }

    // 4. Selección dinámica de 3 LODs según la distancia de la cámara
    int lodLevel = 0;
    if (m_camDistance >= 21.0f) {
        lodLevel = 2;
    } else if (m_camDistance >= 16.5f) {
        lodLevel = 1;
    }
    const auto& meshIndices = (lodLevel == 2) ? m_meshIndicesLOD2 : ((lodLevel == 1) ? m_meshIndicesLOD1 : m_meshIndicesLOD0);

    // 5. Culling geométrico de triángulos si estamos en modo sólido
    if (m_renderMode == WaterRenderMode::SOLID_BOTW || 
        m_renderMode == WaterRenderMode::SMP_CPU_ESPRESSO || 
        m_renderMode == WaterRenderMode::SOLID_PLUS_WIREFRAME) {
        m_visibleIndices.clear();
        for (size_t idx = 0; idx < meshIndices.size(); idx += 3) {
            int i0 = meshIndices[idx + 0];
            int i1 = meshIndices[idx + 1];
            int i2 = meshIndices[idx + 2];

            if (m_vertexDepthZ[i0] <= NEAR_PLANE_Z ||
                m_vertexDepthZ[i1] <= NEAR_PLANE_Z ||
                m_vertexDepthZ[i2] <= NEAR_PLANE_Z) {
                continue;
            }

            const auto& p0 = m_renderVertices[i0].position;
            const auto& p1 = m_renderVertices[i1].position;
            const auto& p2 = m_renderVertices[i2].position;

            float minX = std::min(p0.x, std::min(p1.x, p2.x));
            float maxX = std::max(p0.x, std::max(p1.x, p2.x));
            float minY = std::min(p0.y, std::min(p1.y, p2.y));
            float maxY = std::max(p0.y, std::max(p1.y, p2.y));

            if (maxX < -32.0f || minX > 1312.0f || maxY < -32.0f || minY > 752.0f) {
                continue;
            }

            float triWidth = maxX - minX;
            float triHeight = maxY - minY;
            if (triHeight < 1.0f || triWidth < 1.0f || triWidth > 400.0f || triHeight > 300.0f) {
                continue;
            }

            if (minX < -2048.0f || maxX > 3328.0f || minY < -2048.0f || maxY > 2768.0f) {
                continue;
            }

            m_visibleIndices.push_back(i0);
            m_visibleIndices.push_back(i1);
            m_visibleIndices.push_back(i2);
        }

        if (!m_visibleIndices.empty()) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_RenderGeometry(renderer, nullptr,
                               m_renderVertices.data(), static_cast<int>(NUM_VERTICES),
                               m_visibleIndices.data(), static_cast<int>(m_visibleIndices.size()));
        }
    }

    // 6. Modo de inspección técnica Wireframe
    if (m_renderMode == WaterRenderMode::WIREFRAME_MESH || m_renderMode == WaterRenderMode::SOLID_PLUS_WIREFRAME) {
        RenderWaterWireframeOnly(renderer, lodLevel);
    }
}

void SceneWater::RenderWaterMesh(SDL_Renderer* renderer) {
    if (!renderer) return;

    m_renderedWithGPU = false;

#ifdef __wiiu__
    if (m_gx2ShaderLoaded && m_gx2BuffersCreated &&
        (m_renderMode == WaterRenderMode::SOLID_BOTW || m_renderMode == WaterRenderMode::SOLID_PLUS_WIREFRAME)) {
        SDL_RenderFlush(renderer);
        RenderWaterMeshGX2();
        m_renderedWithGPU = true;
        RestoreSDL2GX2State(renderer);

        if (m_renderMode == WaterRenderMode::SOLID_PLUS_WIREFRAME) {
            const bool useLOD1 = (m_camDistance >= 18.5f);
            RenderWaterMeshCPU(renderer); // Genera y superpone el wireframe exacto
        }
        return;
    }
#endif

    RenderWaterMeshCPU(renderer);
}

void SceneWater::RenderFloatingBuoy(SDL_Renderer* renderer) {
    if (!renderer) return;

    // Calcular desplazamiento exacto de las ondas de Gerstner en la posición de la boya
    Point3D buoyWorldPos = m_buoyBasePos;
    Point3D buoyNormal{0.0f, 1.0f, 0.0f};

    for (const auto& w : m_waves) {
        float k = 6.2831853f / w.wavelength;
        float wSpeed = std::sqrt(9.81f * k) * w.speed;
        float phase = (w.dirX * m_buoyBasePos.x + w.dirZ * m_buoyBasePos.z) * k + m_animTime * wSpeed;
        float cosP = std::cos(phase);
        float sinP = std::sin(phase);

        float amp = w.amplitude * m_waveAmplitudeMultiplier;
        float q = w.steepness / (k * std::max(0.001f, amp) * 4.0f);
        q = std::min(q, 1.0f);

        buoyWorldPos.x += q * amp * w.dirX * cosP;
        buoyWorldPos.y += amp * sinP;
        buoyWorldPos.z += q * amp * w.dirZ * cosP;

        buoyNormal.x -= w.dirX * (k * amp) * cosP;
        buoyNormal.y -= q * (k * amp) * sinP;
        buoyNormal.z -= w.dirZ * (k * amp) * cosP;
    }
    buoyNormal = buoyNormal.Normalized();

    // Ajuste de flotabilidad de Arquímedes
    buoyWorldPos.y += m_buoyRadius * 0.40f;

    // Proyección 3D de la boya
    Point3D rel = buoyWorldPos - m_camPos;
    float cz = Point3D::Dot(rel, m_camForward);
    if (cz > NEAR_PLANE_Z) {
        float cx = Point3D::Dot(rel, m_camRight);
        float cy = Point3D::Dot(rel, m_camUp);
        float invZ = 1.0f / cz;

        float buoyScreenX = 640.0f + (cx * invZ) * FOCAL_FACTOR;
        float buoyScreenY = 360.0f - (cy * invZ) * FOCAL_FACTOR;
        float buoyR = std::clamp((m_buoyRadius * invZ) * FOCAL_FACTOR, 5.0f, 150.0f);

        if (buoyScreenX >= -100.0f && buoyScreenX <= 1380.0f && buoyScreenY >= -100.0f && buoyScreenY <= 820.0f) {
            // Anillo elíptico de contacto de la boya en el agua (1 sola llamada GPU SDL_RenderGeometry)
            constexpr int RING_SEGS = 16;
            SDL_Vertex rVerts[RING_SEGS * 2];
            int rIndices[RING_SEGS * 6];
            float innerR = buoyR * 1.05f;
            float outerR = buoyR * 1.35f;
            float tiltY = std::max(0.18f, std::sin(m_camPitch));

            SDL_Color ringCol{230, 250, 255, 170};
            SDL_Color outerCol{230, 250, 255, 0};

            for (int s = 0; s < RING_SEGS; ++s) {
                float angle = static_cast<float>(s) * (6.2831853f / static_cast<float>(RING_SEGS));
                float cosA = std::cos(angle);
                float sinA = std::sin(angle);

                rVerts[s * 2 + 0] = { SDL_FPoint{buoyScreenX + cosA * innerR, buoyScreenY + sinA * (innerR * tiltY)}, ringCol, SDL_FPoint{0.0f, 0.0f} };
                rVerts[s * 2 + 1] = { SDL_FPoint{buoyScreenX + cosA * outerR, buoyScreenY + sinA * (outerR * tiltY)}, outerCol, SDL_FPoint{0.0f, 0.0f} };

                int nextS = (s + 1 == RING_SEGS) ? 0 : (s + 1);
                int idxBase = s * 6;
                rIndices[idxBase + 0] = s * 2 + 0;
                rIndices[idxBase + 1] = s * 2 + 1;
                rIndices[idxBase + 2] = nextS * 2 + 0;
                rIndices[idxBase + 3] = s * 2 + 1;
                rIndices[idxBase + 4] = nextS * 2 + 1;
                rIndices[idxBase + 5] = nextS * 2 + 0;
            }

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(renderer, nullptr, rVerts, RING_SEGS * 2, rIndices, RING_SEGS * 6);

            // Geometría de la boya en 1 sola llamada GPU
            constexpr int BUOY_SEGS = 12;
            SDL_Vertex bVerts[BUOY_SEGS + 1];
            int bIndices[BUOY_SEGS * 3];

            bVerts[0] = { SDL_FPoint{buoyScreenX, buoyScreenY},
                          SDL_Color{250, 210, 45, 255}, SDL_FPoint{0.0f, 0.0f} };

            for (int s = 0; s < BUOY_SEGS; ++s) {
                float angle = static_cast<float>(s) * (6.2831853f / static_cast<float>(BUOY_SEGS));
                float px = buoyScreenX + std::cos(angle) * buoyR;
                float py = buoyScreenY + std::sin(angle) * buoyR;
                bVerts[s + 1] = { SDL_FPoint{px, py},
                                  SDL_Color{170, 120, 20, 255}, SDL_FPoint{0.0f, 0.0f} };

                bIndices[s * 3 + 0] = 0;
                bIndices[s * 3 + 1] = s + 1;
                bIndices[s * 3 + 2] = (s + 1 == BUOY_SEGS) ? 1 : (s + 2);
            }

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(renderer, nullptr, bVerts, BUOY_SEGS + 1, bIndices, BUOY_SEGS * 3);
        }
    }
}

void SceneWater::RenderHUD(SDL_Renderer* renderer) {
    if (!renderer) return;

    // 1. Renderizado de botones de control superior
    m_backButton.Render(renderer);
    m_modeButton.Render(renderer);
    m_presetButton.Render(renderer);
    m_waveButton.Render(renderer);

    m_timeButton.Render(renderer);
    m_foamButton.Render(renderer);
    m_camResetButton.Render(renderer);

    // 2. Panel inferior de telemetría y ayuda técnica
    constexpr SDL_Rect hudBox = { 20, 618, 1240, 90 };
    Render2D::drawBox(renderer, hudBox.x, hudBox.y, hudBox.w, hudBox.h, ColorRGBA{10, 22, 38, 235}, ColorRGBA::Cyan());

    StackString<256> line1;
    line1.Format("DEMO 7: AGUA 3D (BOTW) | FPS: %.1f | PIPELINE: %s",
                 m_smoothedFps,
                 m_renderedWithGPU ? "SHADER HW (LATTE GX2)" : "SMP CPU (ESPRESSO CORES 1+2)");
    Render2D::drawPixelString(renderer, line1.c_str(), 38, 630, 2, ColorRGBA::Yellow());

    const char* lodDesc = (m_camDistance < 16.5f) ? "LOD 0 (ULTRA 40x40)"
                        : (m_camDistance < 21.0f) ? "LOD 1 (MEDIO 20x20)"
                                                  : "LOD 2 (LEJOS 10x10)";
    const int activeTris = m_renderedWithGPU 
        ? ((m_camDistance < 16.5f) ? (NUM_INDICES_LOD0 / 3)
           : (m_camDistance < 21.0f) ? (NUM_INDICES_LOD1 / 3)
                                     : (NUM_INDICES_LOD2 / 3))
        : static_cast<int>(m_visibleIndices.size() / 3);

    StackString<256> line2;
    line2.Format("GERSTNER WAVES 3D | %s | TRIANGULOS: %d | DRAW CALLS: 1",
                 lodDesc,
                 activeTris);
    Render2D::drawPixelString(renderer, line2.c_str(), 38, 656, 2, ColorRGBA::White());

    Render2D::drawPixelString(renderer,
                              "[STICK IZQ / TOQUE]: ORBITAR CAMARA | [STICK DER]: ZOOM | [B]: VOLVER",
                              38, 680, 2, ColorRGBA{180, 220, 255, 255});
}

void SceneWater::RenderSkyAndLandscapeGX2() {
    auto& r2d = Render2DGX2::Instance();

#if defined(__WIIU__) || defined(__wiiu__)
    if (m_nativeContextState && m_pColorBuffer) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    }
#endif

    r2d.Begin();

    // Cálculo del horizonte en perspectiva (FOV vertical de 60°)
    float horizonY = 360.0f - std::tan(m_camPitch) * FOCAL_FACTOR;
    horizonY = std::clamp(horizonY, -80.0f, 720.0f);
    int hY = static_cast<int>(horizonY);

    // 1. Cielo atmosférico y cuenca de Hyrule: Fondo continuo de pantalla completa
    ColorRGBA zenithCol = m_skyZenithColor;
    ColorRGBA horizonCol = m_skyHorizonColor;
    ColorRGBA basinCol = (m_timeOfDay == TimeOfDay::NIGHT)
        ? ColorRGBA{10, 18, 35, 255}
        : ((m_timeOfDay == TimeOfDay::SUNSET) ? ColorRGBA{85, 40, 55, 255} : ColorRGBA{35, 80, 120, 255});

    float splitY = std::clamp(horizonY, 0.0f, 720.0f);
    if (splitY > 0.0f) {
        // Tramo superior: Cénit -> Horizonte
        r2d.DrawQuad(0.0f, 0.0f, zenithCol,
                     0.0f, splitY, horizonCol,
                     1280.0f, splitY, horizonCol,
                     1280.0f, 0.0f, zenithCol);
    }
    if (splitY < 720.0f) {
        // Tramo inferior: Horizonte -> Cuenca del Lago
        r2d.DrawQuad(0.0f, splitY, horizonCol,
                     0.0f, 720.0f, basinCol,
                     1280.0f, 720.0f, basinCol,
                     1280.0f, splitY, horizonCol);
    }

    // 2. Silueta estilizada de las montañas lejanas de Hyrule (Doble capa BotW: Picos Lejanos + Crestas Medias)
    if (horizonY > -80.0f && horizonY < 650.0f) {
        ColorRGBA mtFarCol = (m_timeOfDay == TimeOfDay::NIGHT)
            ? ColorRGBA{18, 28, 52, 255}
            : ((m_timeOfDay == TimeOfDay::SUNSET) ? ColorRGBA{115, 55, 75, 255} : ColorRGBA{70, 120, 165, 255});
        ColorRGBA mtMidCol = (m_timeOfDay == TimeOfDay::NIGHT)
            ? ColorRGBA{10, 18, 38, 255}
            : ((m_timeOfDay == TimeOfDay::SUNSET) ? ColorRGBA{80, 35, 55, 255} : ColorRGBA{45, 90, 130, 255});

        constexpr int MT_STEPS = 32;
        static const auto s_rayOffsetAngles = []() {
            std::array<float, MT_STEPS + 1> angles{};
            for (int s = 0; s <= MT_STEPS; ++s) {
                float frac = static_cast<float>(s) / static_cast<float>(MT_STEPS);
                float screenX = frac * 1280.0f;
                angles[s] = std::atan2(screenX - 640.0f, FOCAL_FACTOR);
            }
            return angles;
        }();

        // Capa 1: Picos Lejanos de Hyrule (Alpinos afilados, 100-190 px sobre horizonte)
        float prevScreenX = 0.0f;
        float prevTopY = 0.0f;
        float prevBotY = 0.0f;
        for (int s = 0; s <= MT_STEPS; ++s) {
            float frac = static_cast<float>(s) / static_cast<float>(MT_STEPS);
            float screenX = frac * 1280.0f;
            float rayAngle = m_camYaw + s_rayOffsetAngles[s];
            float ridgeFar = std::abs(std::sin(rayAngle * 1.5f)) * 95.0f + std::sin(rayAngle * 3.5f + 0.8f) * 45.0f + 65.0f;
            float topY = std::max(2.0f, horizonY - ridgeFar);
            float botY = std::min(720.0f, std::max(horizonY + 280.0f, 440.0f));

            if (s > 0) {
                r2d.DrawQuad(prevScreenX, prevTopY, mtFarCol,
                             prevScreenX, prevBotY, mtFarCol,
                             screenX, botY, mtFarCol,
                             screenX, topY, mtFarCol);
            }
            prevScreenX = screenX;
            prevTopY = topY;
            prevBotY = botY;
        }

        // Capa 2: Crestas Intermedias del Lago Hylia (50-110 px sobre horizonte, base profunda)
        prevScreenX = 0.0f;
        prevTopY = 0.0f;
        prevBotY = 0.0f;
        for (int s = 0; s <= MT_STEPS; ++s) {
            float frac = static_cast<float>(s) / static_cast<float>(MT_STEPS);
            float screenX = frac * 1280.0f;
            float rayAngle = m_camYaw + s_rayOffsetAngles[s];
            float ridgeMid = std::abs(std::sin(rayAngle * 2.2f + 1.2f)) * 55.0f + std::cos(rayAngle * 4.5f) * 25.0f + 35.0f;
            float topY = std::max(10.0f, horizonY - ridgeMid);
            float botY = std::min(720.0f, std::max(horizonY + 360.0f, 520.0f));

            if (s > 0) {
                r2d.DrawQuad(prevScreenX, prevTopY, mtMidCol,
                             prevScreenX, prevBotY, mtMidCol,
                             screenX, botY, mtMidCol,
                             screenX, topY, mtMidCol);
            }
            prevScreenX = screenX;
            prevTopY = topY;
            prevBotY = botY;
        }
    }

    // 3. Proyección perspectiva 3D del Sol / Luna en silicio
    float sunCz = Point3D::Dot(m_sunDir, m_camForward);
    if (sunCz > 0.05f) {
        float sunCx = Point3D::Dot(m_sunDir, m_camRight);
        float sunCy = Point3D::Dot(m_sunDir, m_camUp);
        float invCz = 1.0f / sunCz;
        float sunScreenX = 640.0f + (sunCx * invCz) * FOCAL_FACTOR;
        float sunScreenY = 360.0f - (sunCy * invCz) * FOCAL_FACTOR;

        if (sunScreenX >= -80.0f && sunScreenX <= 1360.0f &&
            sunScreenY >= -80.0f && sunScreenY <= (static_cast<float>(hY) + 40.0f)) {
            ColorRGBA centerCol{m_sunColor.r, m_sunColor.g, m_sunColor.b, 255};
            ColorRGBA edgeCol{m_sunColor.r, m_sunColor.g, m_sunColor.b, 150};
            r2d.DrawCircle(sunScreenX, sunScreenY, 24.0f, centerCol, edgeCol, 16);
        }
    }

    r2d.End();
}

void SceneWater::RenderWaterWireframeOnlyGX2(int lodLevel) {
    ColorRGBA wireColor = (m_renderMode == WaterRenderMode::WIREFRAME_MESH)
        ? ColorRGBA{0, 255, 230, 230}
        : ColorRGBA{255, 255, 255, 140};

    auto& r2d = Render2DGX2::Instance();

#if defined(__WIIU__) || defined(__wiiu__)
    if (m_nativeContextState && m_pColorBuffer) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    }
#endif

    r2d.Begin();

    const int step = (lodLevel == 2) ? 4 : ((lodLevel == 1) ? 2 : 1);

    // A. Trazar líneas horizontales continuas (filas Z)
    for (int gz = 0; gz < GRID_SIZE_Z; gz += step) {
        for (int gx = 0; gx < GRID_SIZE_X - step; gx += step) {
            int idx0 = gz * GRID_SIZE_X + gx;
            int idx1 = gz * GRID_SIZE_X + (gx + step);
            if (m_vertexDepthZ[idx0] > NEAR_PLANE_Z && m_vertexDepthZ[idx1] > NEAR_PLANE_Z) {
                const auto& p0 = m_renderVertices[idx0].position;
                const auto& p1 = m_renderVertices[idx1].position;
                r2d.DrawLine(p0.x, p0.y, p1.x, p1.y, 1.2f, wireColor);
            }
        }
    }

    // B. Trazar líneas verticales continuas (columnas X)
    for (int gx = 0; gx < GRID_SIZE_X; gx += step) {
        for (int gz = 0; gz < GRID_SIZE_Z - step; gz += step) {
            int idx0 = gz * GRID_SIZE_X + gx;
            int idx1 = (gz + step) * GRID_SIZE_X + gx;
            if (m_vertexDepthZ[idx0] > NEAR_PLANE_Z && m_vertexDepthZ[idx1] > NEAR_PLANE_Z) {
                const auto& p0 = m_renderVertices[idx0].position;
                const auto& p1 = m_renderVertices[idx1].position;
                r2d.DrawLine(p0.x, p0.y, p1.x, p1.y, 1.2f, wireColor);
            }
        }
    }

    r2d.End();
}

void SceneWater::RenderWaterMeshCPUGX2() {
    float maxAmp = 0.01f;
    for (const auto& w : m_waves) {
        maxAmp += w.amplitude * m_waveAmplitudeMultiplier;
    }

    PrecomputedWave pWaves[4];
    const size_t numWaves = std::min(m_waves.size(), size_t{4});
    for (size_t wIdx = 0; wIdx < numWaves; ++wIdx) {
        const auto& w = m_waves[wIdx];
        float k = 6.2831853f / w.wavelength;
        float wSpeed = std::sqrt(9.81f * k) * w.speed;
        float amp = w.amplitude * m_waveAmplitudeMultiplier;
        float q = std::min(1.0f, w.steepness / (k * std::max(0.001f, amp) * 4.0f));

        pWaves[wIdx].dirX = w.dirX;
        pWaves[wIdx].dirZ = w.dirZ;
        pWaves[wIdx].k = k;
        pWaves[wIdx].phaseTime = m_animTime * wSpeed;
        pWaves[wIdx].amp = amp;
        pWaves[wIdx].qAmp = q * amp;
        pWaves[wIdx].kAmp = k * amp;
        pWaves[wIdx].qkAmp = q * (k * amp);
    }

    Point3D camViewDir = (m_camPivot - m_camPos).Normalized();
    Point3D halfDir = (m_sunDir + camViewDir).Normalized();

    m_activeSimParams.numWaves = numWaves;
    m_activeSimParams.maxAmp = maxAmp;
    m_activeSimParams.halfDir = halfDir;
    m_activeSimParams.camPos = m_camPos;
    m_activeSimParams.camForward = m_camForward;
    m_activeSimParams.camRight = m_camRight;
    m_activeSimParams.camUp = m_camUp;
    for (size_t wIdx = 0; wIdx < numWaves; ++wIdx) {
        m_activeSimParams.waves[wIdx] = pWaves[wIdx];
    }

#ifdef __wiiu__
    OSResetEvent(&m_doneEvent);
#endif

    int target = m_targetFrame.fetch_add(1, std::memory_order_release) + 1;

#ifdef __wiiu__
    OSSignalEvent(&m_jobEvent);
#else
    {
        std::lock_guard<std::mutex> lock(m_syncMutex);
        m_jobReady = true;
    }
    m_syncCv.notify_one();
#endif

    constexpr size_t HALF_VERTICES = NUM_VERTICES / 2;
    ComputeVertexSlice(0, HALF_VERTICES, m_activeSimParams);

    while (m_workerDoneFrame.load(std::memory_order_acquire) < target) {
#ifdef __wiiu__
        OSWaitEvent(&m_doneEvent);
#else
        std::unique_lock<std::mutex> lock(m_syncMutex);
        m_syncCv.wait(lock, [this, target]() {
            return m_workerDoneFrame.load(std::memory_order_acquire) >= target || m_stopWorker.load(std::memory_order_relaxed);
        });
#endif
    }

    int lodLevel = 0;
    if (m_camDistance >= 21.0f) {
        lodLevel = 2;
    } else if (m_camDistance >= 16.5f) {
        lodLevel = 1;
    }
    const auto& meshIndices = (lodLevel == 2) ? m_meshIndicesLOD2 : ((lodLevel == 1) ? m_meshIndicesLOD1 : m_meshIndicesLOD0);

    auto& r2d = Render2DGX2::Instance();

    if (m_renderMode == WaterRenderMode::SMP_CPU_ESPRESSO || 
        (!m_renderedWithGPU && (m_renderMode == WaterRenderMode::SOLID_BOTW || m_renderMode == WaterRenderMode::SOLID_PLUS_WIREFRAME))) {
        m_visibleIndices.clear();

#if defined(__WIIU__) || defined(__wiiu__)
        if (m_nativeContextState && m_pColorBuffer) {
            GX2SetContextState(m_nativeContextState);
            GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
        }
#endif

        r2d.Begin();
        for (size_t idx = 0; idx < meshIndices.size(); idx += 3) {
            int i0 = meshIndices[idx + 0];
            int i1 = meshIndices[idx + 1];
            int i2 = meshIndices[idx + 2];

            if (m_vertexDepthZ[i0] <= NEAR_PLANE_Z ||
                m_vertexDepthZ[i1] <= NEAR_PLANE_Z ||
                m_vertexDepthZ[i2] <= NEAR_PLANE_Z) {
                continue;
            }

            const auto& p0 = m_renderVertices[i0].position;
            const auto& p1 = m_renderVertices[i1].position;
            const auto& p2 = m_renderVertices[i2].position;

            float minX = std::min(p0.x, std::min(p1.x, p2.x));
            float maxX = std::max(p0.x, std::max(p1.x, p2.x));
            float minY = std::min(p0.y, std::min(p1.y, p2.y));
            float maxY = std::max(p0.y, std::max(p1.y, p2.y));

            if (maxX < -32.0f || minX > 1312.0f || maxY < -32.0f || minY > 752.0f) {
                continue;
            }

            float triWidth = maxX - minX;
            float triHeight = maxY - minY;
            if (triHeight < 1.0f || triWidth < 1.0f || triWidth > 400.0f || triHeight > 300.0f) {
                continue;
            }

            if (minX < -2048.0f || maxX > 3328.0f || minY < -2048.0f || maxY > 2768.0f) {
                continue;
            }

            m_visibleIndices.push_back(i0);
            m_visibleIndices.push_back(i1);
            m_visibleIndices.push_back(i2);

            const auto& c0 = m_renderVertices[i0].color;
            const auto& c1 = m_renderVertices[i1].color;
            const auto& c2 = m_renderVertices[i2].color;

            r2d.DrawTriangle(p0.x, p0.y, ColorRGBA{c0.r, c0.g, c0.b, c0.a},
                            p1.x, p1.y, ColorRGBA{c1.r, c1.g, c1.b, c1.a},
                            p2.x, p2.y, ColorRGBA{c2.r, c2.g, c2.b, c2.a});
        }
        r2d.End();
    }

    if (m_renderMode == WaterRenderMode::WIREFRAME_MESH || m_renderMode == WaterRenderMode::SOLID_PLUS_WIREFRAME) {
        RenderWaterWireframeOnlyGX2(lodLevel);
    }
}

void SceneWater::RenderWaterMeshGX2Native() {
    m_renderedWithGPU = false;

#if defined(__WIIU__) || defined(__wiiu__)
    if (m_renderMode == WaterRenderMode::SOLID_BOTW || m_renderMode == WaterRenderMode::SOLID_PLUS_WIREFRAME) {
        if (m_gx2ShaderLoaded && m_gx2BuffersCreated) {
            if (m_nativeContextState && m_pColorBuffer) {
                GX2SetContextState(m_nativeContextState);
                GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
            }
            RenderWaterMeshGX2();
            m_renderedWithGPU = true;

            if (m_renderMode == WaterRenderMode::SOLID_PLUS_WIREFRAME) {
                RenderWaterMeshCPUGX2();
            }
            return;
        }
    }

    RenderWaterMeshCPUGX2();
#endif
}

void SceneWater::RenderBuoyAndHUD_GX2() {
    auto& r2d = Render2DGX2::Instance();

#if defined(__WIIU__) || defined(__wiiu__)
    if (m_nativeContextState && m_pColorBuffer) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    }
#endif

    r2d.Begin();

    // 1. Boya náutica flotante con física de Arquímedes
    Point3D buoyWorldPos = m_buoyBasePos;
    Point3D buoyNormal{0.0f, 1.0f, 0.0f};

    for (const auto& w : m_waves) {
        float k = 6.2831853f / w.wavelength;
        float wSpeed = std::sqrt(9.81f * k) * w.speed;
        float phase = (w.dirX * m_buoyBasePos.x + w.dirZ * m_buoyBasePos.z) * k + m_animTime * wSpeed;
        float cosP = std::cos(phase);
        float sinP = std::sin(phase);

        float amp = w.amplitude * m_waveAmplitudeMultiplier;
        float q = w.steepness / (k * std::max(0.001f, amp) * 4.0f);
        q = std::min(q, 1.0f);

        buoyWorldPos.x += q * amp * w.dirX * cosP;
        buoyWorldPos.y += amp * sinP;
        buoyWorldPos.z += q * amp * w.dirZ * cosP;

        buoyNormal.x -= w.dirX * (k * amp) * cosP;
        buoyNormal.y -= q * (k * amp) * sinP;
        buoyNormal.z -= w.dirZ * (k * amp) * cosP;
    }
    buoyNormal = buoyNormal.Normalized();

    buoyWorldPos.y += m_buoyRadius * 0.40f;

    Point3D rel = buoyWorldPos - m_camPos;
    float cz = Point3D::Dot(rel, m_camForward);
    if (cz > NEAR_PLANE_Z) {
        float cx = Point3D::Dot(rel, m_camRight);
        float cy = Point3D::Dot(rel, m_camUp);
        float invZ = 1.0f / cz;

        float buoyScreenX = 640.0f + (cx * invZ) * FOCAL_FACTOR;
        float buoyScreenY = 360.0f - (cy * invZ) * FOCAL_FACTOR;
        float buoyR = std::clamp((m_buoyRadius * invZ) * FOCAL_FACTOR, 5.0f, 150.0f);

        if (buoyScreenX >= -100.0f && buoyScreenX <= 1380.0f && buoyScreenY >= -100.0f && buoyScreenY <= 820.0f) {
            // Anillo elíptico de espuma en contacto con la superficie del agua
            constexpr int RING_SEGS = 16;
            float innerR = buoyR * 1.05f;
            float outerR = buoyR * 1.35f;
            ColorRGBA ringCol{230, 250, 255, 170};
            ColorRGBA outerCol{230, 250, 255, 0};
            float tiltY = std::max(0.18f, std::sin(m_camPitch));

            for (int s = 0; s < RING_SEGS; ++s) {
                float a0 = static_cast<float>(s) * (6.2831853f / static_cast<float>(RING_SEGS));
                float a1 = static_cast<float>(s + 1 == RING_SEGS ? 0 : s + 1) * (6.2831853f / static_cast<float>(RING_SEGS));

                float cosA0 = std::cos(a0);
                float sinA0 = std::sin(a0);
                float cosA1 = std::cos(a1);
                float sinA1 = std::sin(a1);

                float x0 = buoyScreenX + cosA0 * innerR;
                float y0 = buoyScreenY + sinA0 * (innerR * tiltY);
                float x1 = buoyScreenX + cosA0 * outerR;
                float y1 = buoyScreenY + sinA0 * (outerR * tiltY);
                float x2 = buoyScreenX + cosA1 * outerR;
                float y2 = buoyScreenY + sinA1 * (outerR * tiltY);
                float x3 = buoyScreenX + cosA1 * innerR;
                float y3 = buoyScreenY + sinA1 * (innerR * tiltY);

                r2d.DrawQuad(x0, y0, ringCol,
                             x1, y1, outerCol,
                             x2, y2, outerCol,
                             x3, y3, ringCol);
            }

            // Cuerpo esférico flotante de la boya con baliza superior (alta visibilidad)
            ColorRGBA buoyCenterCol{255, 130, 25, 255};
            ColorRGBA buoyEdgeCol{180, 50, 10, 255};
            r2d.DrawCircle(buoyScreenX, buoyScreenY, buoyR, buoyCenterCol, buoyEdgeCol, 18);
            // Franja intermedia blanca/crema de señalización marítima
            r2d.DrawCircle(buoyScreenX, buoyScreenY - buoyR * 0.20f, buoyR * 0.65f, ColorRGBA{255, 255, 240, 255}, ColorRGBA{230, 190, 40, 255}, 14);
            // Cúpula superior y baliza destellante
            r2d.DrawCircle(buoyScreenX, buoyScreenY - buoyR * 0.70f, buoyR * 0.32f, ColorRGBA{255, 50, 50, 255}, ColorRGBA{180, 20, 20, 255}, 10);
        }
    }

    // 2. Botones de Control Superior
    // Botón Volver con fondo sólido de alto contraste (Fila 1)
    const auto& rBack = m_backButton.GetRect();
    r2d.DrawButton(m_backButton.GetLabel(), static_cast<float>(rBack.x), static_cast<float>(rBack.y),
                   static_cast<float>(rBack.w), static_cast<float>(rBack.h), m_backButton.IsHovered(),
                   ColorRGBA{12, 18, 32, 255}, ColorRGBA{200, 40, 40, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(),
                   ColorRGBA::White(), 2.0f);

    ColorRGBA normalFill{20, 35, 55, 230};
    ColorRGBA hoverFill{0, 180, 240, 240};
    ColorRGBA normalBorder{0, 210, 255, 255};
    ColorRGBA hoverBorder = ColorRGBA::Yellow();
    ColorRGBA textColor = ColorRGBA::White();

    auto drawSceneButton = [&](const UIButton& btn) {
        const auto& rect = btn.GetRect();
        r2d.DrawButton(btn.GetLabel(), static_cast<float>(rect.x), static_cast<float>(rect.y),
                       static_cast<float>(rect.w), static_cast<float>(rect.h), btn.IsHovered(),
                       normalFill, hoverFill, normalBorder, hoverBorder, textColor, 2.0f);
    };

    drawSceneButton(m_modeButton);
    drawSceneButton(m_presetButton);
    drawSceneButton(m_waveButton);
    drawSceneButton(m_timeButton);
    drawSceneButton(m_foamButton);
    drawSceneButton(m_camResetButton);

    // 3. Panel inferior de telemetría estandarizada (UIToolkit - DrawHudFooter)
    int activeTris = 0;
    const char* lodName = "LOD 0 (ULTRA 40x40)";
    if (m_camDistance >= 21.0f) {
        activeTris = NUM_INDICES_LOD2 / 3;
        lodName = "LOD 2 (LEJOS 10x10)";
    } else if (m_camDistance >= 16.5f) {
        activeTris = NUM_INDICES_LOD1 / 3;
        lodName = "LOD 1 (MEDIO 20x20)";
    } else {
        activeTris = NUM_INDICES_LOD0 / 3;
        lodName = "LOD 0 (ULTRA 40x40)";
    }

    StackString<256> line1;
    line1.Format("DEMO 7: AGUA 3D (BOTW) | FPS: %.1f | PIPELINE: %s",
                 m_smoothedFps,
                 m_renderedWithGPU ? "SHADER HW (LATTE GX2)" : "SMP CPU (CORES 1+2)");

    StackString<256> line2;
    line2.Format("GERSTNER WAVES 3D | %s | TRIS: %d | DRAWS: 1",
                 lodName,
                 m_renderedWithGPU ? activeTris : static_cast<int>(m_visibleIndices.size() / 3));

    UI::UIToolkit::DrawHudFooter({
        { "[STICK IZQ / TOQUE]: ORBITAR | [STICK DER]: ZOOM | [B]: VOLVER", ColorRGBA{180, 220, 255, 255} },
        { line1.string_view(), ColorRGBA::Yellow() },
        { line2.string_view(), ColorRGBA::White() }
    });

    r2d.End();
}

void SceneWater::Render(SDL_Renderer* renderer) {
    // Medición y suavizado de framerate
    Uint32 nowTicks = SDL_GetTicks();
    if (m_lastFrameTicks > 0) {
        Uint32 deltaTicks = nowTicks - m_lastFrameTicks;
        if (deltaTicks > 0) {
            float instFps = 1000.0f / static_cast<float>(deltaTicks);
            m_smoothedFps = m_smoothedFps * 0.90f + instFps * 0.10f;
        }
    }
    m_lastFrameTicks = nowTicks;

#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_gx2ShaderLoaded || !m_gx2BuffersCreated || !m_nativeContextState) {
        InitGX2WaterPipeline();
    }
    // Actualizar siempre el color buffer actual de SDL2 (double-buffered backbuffer swapchain)
    BindColorBuffer(renderer);

    if (m_pColorBuffer && m_nativeContextState) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
        GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
        GX2SetScissor(0, 0, 1280, 720);
        float clrR = static_cast<float>(m_skyHorizonColor.r) * (1.0f / 255.0f);
        float clrG = static_cast<float>(m_skyHorizonColor.g) * (1.0f / 255.0f);
        float clrB = static_cast<float>(m_skyHorizonColor.b) * (1.0f / 255.0f);
        GX2ClearColor(m_pColorBuffer, clrR, clrG, clrB, 1.0f);
    }

    // Invalidar caché RHI para forzar configuración limpia de registros en m_nativeContextState
    RHIPipelineCache::Instance().Invalidate();

    // Pase 1: Cielo atmosférico y montañas de Hyrule (Render2DGX2 - 1 Draw Call)
    RenderSkyAndLandscapeGX2();

    // Pase 2: Malla 3D de agua de Gerstner (GX2 CafeGLSL Shader o SMP CPU - 1 Draw Call)
    RenderWaterMeshGX2Native();

    // Pase 3: Boya náutica flotante con física de Arquímedes + HUD Completo (Render2DGX2 - 1 Draw Call)
    RenderBuoyAndHUD_GX2();
#else
    if (!renderer) return;

    // 1. Cielo atmosférico y montañas de Hyrule
    RenderSkyAndLandscape(renderer);

    // 2. Malla 3D de agua de Gerstner (Single GPU Batch Call)
    RenderWaterMesh(renderer);

    // 3. Boya náutica flotante con física de Arquímedes
    RenderFloatingBuoy(renderer);

    // 4. Interfaz interactiva y telemetría
    RenderHUD(renderer);
#endif
}

} // namespace WiiU
