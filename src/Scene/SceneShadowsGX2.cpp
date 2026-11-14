#include "Scene/SceneShadowsGX2.hpp"
#include "Scene/SceneMenuGX2.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/GX2StateHelper.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/ShadowsShaderBinary.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/StackString.hpp"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <malloc.h>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/event.h>
#include <proc_ui/procui.h>
#endif

namespace WiiU {

namespace {

// Paletas de color para la luz central
struct LightColorPreset {
    const char* name;
    float r, g, b;
    ColorRGBA uiColor;
};

constexpr std::array<LightColorPreset, 4> LIGHT_PALETTES = {{
    { "ORO CALIDO",    1.00f, 0.85f, 0.50f, ColorRGBA{255, 215, 120, 255} },
    { "BLANCO PURO",   1.00f, 1.00f, 1.00f, ColorRGBA{240, 245, 255, 255} },
    { "CIAN SHEIKAH",  0.25f, 0.90f, 1.00f, ColorRGBA{60, 230, 255, 255} },
    { "FUEGO SOLAR",   1.00f, 0.45f, 0.15f, ColorRGBA{255, 110, 40, 255} }
}};

// Función auxiliar para añadir un vértice
inline void AddVertex(std::vector<ShadowsGX2Vertex>& list,
                      float px, float py, float pz,
                      float nx, float ny, float nz,
                      const ColorRGBA& c) {
    ShadowsGX2Vertex v;
    v.pos[0] = px; v.pos[1] = py; v.pos[2] = pz;
    v.normal[0] = nx; v.normal[1] = ny; v.normal[2] = nz;
    v.color[0] = c.r * (1.0f / 255.0f);
    v.color[1] = c.g * (1.0f / 255.0f);
    v.color[2] = c.b * (1.0f / 255.0f);
    v.color[3] = c.a * (1.0f / 255.0f);
    list.push_back(v);
}

} // namespace

SceneShadowsGX2::SceneShadowsGX2() {
    InitGeometry();
}

SceneShadowsGX2::~SceneShadowsGX2() {
    OnExit();
}

void SceneShadowsGX2::InitGeometry() {
    BuildFloorMesh();
    BuildObjectsMesh();
    BuildLightOrbMesh();
}

void SceneShadowsGX2::BuildFloorMesh() {
    m_floorVertices.clear();
    m_floorIndices.clear();

    constexpr int GRID_STEPS = 12; // Cuadrícula de 12x12 para un terreno liso y uniforme de 24x24
    constexpr float FLOOR_SIZE = 24.0f;
    constexpr float STEP_SIZE = FLOOR_SIZE / static_cast<float>(GRID_STEPS);
    constexpr float HALF_SIZE = FLOOR_SIZE * 0.5f;

    m_floorVertices.reserve((GRID_STEPS + 1) * (GRID_STEPS + 1));
    m_floorIndices.reserve(GRID_STEPS * GRID_STEPS * 6);

    for (int iz = 0; iz <= GRID_STEPS; ++iz) {
        float z = -HALF_SIZE + static_cast<float>(iz) * STEP_SIZE;
        for (int ix = 0; ix <= GRID_STEPS; ++ix) {
            float x = -HALF_SIZE + static_cast<float>(ix) * STEP_SIZE;
            AddVertex(m_floorVertices, x, 0.0f, z, 0.0f, 1.0f, 0.0f, ColorRGBA{190, 200, 215, 255});
        }
    }

    // [OPT-111] Recorrido serpentino en zig-zag para maximizar aciertos en Post-Transform Vertex Cache de Latte
    for (int iz = 0; iz < GRID_STEPS; ++iz) {
        for (int step = 0; step < GRID_STEPS; ++step) {
            int ix = (iz & 1) ? (GRID_STEPS - 1 - step) : step;
            uint16_t row1 = static_cast<uint16_t>(iz * (GRID_STEPS + 1) + ix);
            uint16_t row2 = static_cast<uint16_t>((iz + 1) * (GRID_STEPS + 1) + ix);

            // Triángulo 1 (Verdadero CCW: normal apunta a +Y)
            m_floorIndices.push_back(row1);
            m_floorIndices.push_back(row2);
            m_floorIndices.push_back(row1 + 1);

            // Triángulo 2 (Verdadero CCW: normal apunta a +Y)
            m_floorIndices.push_back(row1 + 1);
            m_floorIndices.push_back(row2);
            m_floorIndices.push_back(row2 + 1);
        }
    }
}

void SceneShadowsGX2::BuildObjectsMesh() {
    m_objectsVertices.clear();
    m_objectsIndices.clear();

    auto addQuad = [&](float x0, float y0, float z0,
                       float x1, float y1, float z1,
                       float x2, float y2, float z2,
                       float x3, float y3, float z3,
                       float nx, float ny, float nz,
                       const ColorRGBA& col) {
        uint16_t base = static_cast<uint16_t>(m_objectsVertices.size());
        AddVertex(m_objectsVertices, x0, y0, z0, nx, ny, nz, col);
        AddVertex(m_objectsVertices, x1, y1, z1, nx, ny, nz, col);
        AddVertex(m_objectsVertices, x2, y2, z2, nx, ny, nz, col);
        AddVertex(m_objectsVertices, x3, y3, z3, nx, ny, nz, col);
        m_objectsIndices.push_back(base + 0);
        m_objectsIndices.push_back(base + 1);
        m_objectsIndices.push_back(base + 2);
        m_objectsIndices.push_back(base + 0);
        m_objectsIndices.push_back(base + 2);
        m_objectsIndices.push_back(base + 3);
    };

    auto addTri = [&](float x0, float y0, float z0,
                      float x1, float y1, float z1,
                      float x2, float y2, float z2,
                      const ColorRGBA& col) {
        float ux = x1 - x0, uy = y1 - y0, uz = z1 - z0;
        float vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 0.0001f) {
            nx /= len; ny /= len; nz /= len;
        }
        uint16_t base = static_cast<uint16_t>(m_objectsVertices.size());
        AddVertex(m_objectsVertices, x0, y0, z0, nx, ny, nz, col);
        AddVertex(m_objectsVertices, x1, y1, z1, nx, ny, nz, col);
        AddVertex(m_objectsVertices, x2, y2, z2, nx, ny, nz, col);
        m_objectsIndices.push_back(base + 0);
        m_objectsIndices.push_back(base + 1);
        m_objectsIndices.push_back(base + 2);
    };

    // =========================================================
    // 1. CUBO CORAL (Centro: -2.6, 0.75, -1.5 | Semilado: 0.75)
    // =========================================================
    {
        constexpr float cx = -2.6f, cy = 0.75f, cz = -1.5f, h = 0.75f;
        constexpr ColorRGBA cubeCol{235, 95, 75, 255};
        float x0 = cx - h, x1 = cx + h;
        float y0 = cy - h, y1 = cy + h;
        float z0 = cz - h, z1 = cz + h;

        // Cara Frontal (+Z)
        addQuad(x0, y0, z1,  x1, y0, z1,  x1, y1, z1,  x0, y1, z1,  0.0f,  0.0f,  1.0f, cubeCol);
        // Cara Trasera (-Z)
        addQuad(x1, y0, z0,  x0, y0, z0,  x0, y1, z0,  x1, y1, z0,  0.0f,  0.0f, -1.0f, cubeCol);
        // Cara Derecha (+X)
        addQuad(x1, y0, z1,  x1, y0, z0,  x1, y1, z0,  x1, y1, z1,  1.0f,  0.0f,  0.0f, cubeCol);
        // Cara Izquierda (-X)
        addQuad(x0, y0, z0,  x0, y0, z1,  x0, y1, z1,  x0, y1, z0, -1.0f,  0.0f,  0.0f, cubeCol);
        // Cara Superior (+Y)
        addQuad(x0, y1, z1,  x1, y1, z1,  x1, y1, z0,  x0, y1, z0,  0.0f,  1.0f,  0.0f, cubeCol);
        // Cara Inferior (-Y)
        addQuad(x0, y0, z0,  x1, y0, z0,  x1, y0, z1,  x0, y0, z1,  0.0f, -1.0f,  0.0f, cubeCol);
    }

    // =========================================================
    // 2. PIRÁMIDE ESMERALDA (Base: 2.6, 0.0, -1.5 | Altura: 1.75)
    // =========================================================
    {
        constexpr float px = 2.6f, py = 0.0f, pz = -1.5f;
        constexpr float bw = 0.85f, bh = 1.75f;
        constexpr ColorRGBA pyrCol{70, 220, 145, 255};
        float x0 = px - bw, x1 = px + bw;
        float z0 = pz - bw, z1 = pz + bw;
        float ax = px, ay = py + bh, az = pz;

        // 4 Caras laterales inclinadas (CCW hacia afuera)
        addTri(x0, py, z1,  x1, py, z1,  ax, ay, az, pyrCol); // Frontal (+Z)
        addTri(x1, py, z1,  x1, py, z0,  ax, ay, az, pyrCol); // Derecha (+X)
        addTri(x1, py, z0,  x0, py, z0,  ax, ay, az, pyrCol); // Trasera (-Z)
        addTri(x0, py, z0,  x0, py, z1,  ax, ay, az, pyrCol); // Izquierda (-X)
        // Base inferior (-Y)
        addQuad(x0, py, z0,  x1, py, z0,  x1, py, z1,  x0, py, z1,  0.0f, -1.0f, 0.0f, pyrCol);
    }

    // =========================================================
    // 3. ESFERA ZAFIRO (Centro: 0.0, 0.85, 2.8 | Radio: 0.85)
    // =========================================================
    {
        constexpr float sx = 0.0f, sy = 0.85f, sz = 2.8f, sr = 0.85f;
        constexpr int SLICES = 16;
        constexpr int STACKS = 12;
        constexpr ColorRGBA sphCol{95, 155, 250, 255};

        uint16_t baseVertex = static_cast<uint16_t>(m_objectsVertices.size());
        for (int i = 0; i <= STACKS; ++i) {
            float phi = (static_cast<float>(i) / static_cast<float>(STACKS)) * 3.14159265f;
            float ny = std::cos(phi);
            float rStack = std::sin(phi);

            for (int j = 0; j <= SLICES; ++j) {
                float theta = (static_cast<float>(j) / static_cast<float>(SLICES)) * (2.0f * 3.14159265f);
                float nx = rStack * std::cos(theta);
                float nz = rStack * std::sin(theta);

                AddVertex(m_objectsVertices,
                          sx + sr * nx, sy + sr * ny, sz + sr * nz,
                          nx, ny, nz, sphCol);
            }
        }

        // [OPT-111] Recorrido serpentino en zig-zag para maximizar localidad en caché de vértices de Latte
        for (int i = 0; i < STACKS; ++i) {
            for (int step = 0; step < SLICES; ++step) {
                int j = (i & 1) ? (SLICES - 1 - step) : step;
                uint16_t row1 = static_cast<uint16_t>(baseVertex + i * (SLICES + 1) + j);
                uint16_t row2 = static_cast<uint16_t>(baseVertex + (i + 1) * (SLICES + 1) + j);

                m_objectsIndices.push_back(row1);
                m_objectsIndices.push_back(row2);
                m_objectsIndices.push_back(row1 + 1);

                m_objectsIndices.push_back(row1 + 1);
                m_objectsIndices.push_back(row2);
                m_objectsIndices.push_back(row2 + 1);
            }
        }
    }
}

void SceneShadowsGX2::BuildLightOrbMesh() {
    m_lightOrbVertices.clear();
    m_lightOrbIndices.clear();

    constexpr float ORB_RADIUS = 0.28f;
    constexpr int SLICES = 12;
    constexpr int STACKS = 8;

    m_lightOrbVertices.reserve((STACKS + 1) * (SLICES + 1));
    m_lightOrbIndices.reserve(STACKS * SLICES * 6);

    for (int i = 0; i <= STACKS; ++i) {
        float phi = (static_cast<float>(i) / static_cast<float>(STACKS)) * 3.14159265f;
        float ny = std::cos(phi);
        float rStack = std::sin(phi);

        for (int j = 0; j <= SLICES; ++j) {
            float theta = (static_cast<float>(j) / static_cast<float>(SLICES)) * (2.0f * 3.14159265f);
            float nx = rStack * std::cos(theta);
            float nz = rStack * std::sin(theta);

            AddVertex(m_lightOrbVertices,
                      ORB_RADIUS * nx, ORB_RADIUS * ny, ORB_RADIUS * nz,
                      nx, ny, nz, ColorRGBA::White());
        }
    }

    // [OPT-111] Recorrido serpentino en zig-zag para el Orbe de Luz
    for (int i = 0; i < STACKS; ++i) {
        for (int step = 0; step < SLICES; ++step) {
            int j = (i & 1) ? (SLICES - 1 - step) : step;
            uint16_t row1 = static_cast<uint16_t>(i * (SLICES + 1) + j);
            uint16_t row2 = static_cast<uint16_t>((i + 1) * (SLICES + 1) + j);

            // Triángulo 1 (Verdadero CCW: normal exterior)
            m_lightOrbIndices.push_back(row1);
            m_lightOrbIndices.push_back(row2);
            m_lightOrbIndices.push_back(row1 + 1);

            // Triángulo 2 (Verdadero CCW: normal exterior)
            m_lightOrbIndices.push_back(row1 + 1);
            m_lightOrbIndices.push_back(row2);
            m_lightOrbIndices.push_back(row2 + 1);
        }
    }
}

void SceneShadowsGX2::OnEnter() {
    AudioManager::Instance().StopBGM();
    if (m_floorVertices.empty()) {
        InitGeometry();
    }
    InitGX2Pipeline();
}

void SceneShadowsGX2::OnExit() {
    DestroyGX2Pipeline();
    // Desalojo proactivo de memoria en heap (OPT-45)
    m_floorVertices.clear();
    m_floorVertices.shrink_to_fit();
    m_floorIndices.clear();
    m_floorIndices.shrink_to_fit();
    m_objectsVertices.clear();
    m_objectsVertices.shrink_to_fit();
    m_objectsIndices.clear();
    m_objectsIndices.shrink_to_fit();
    m_lightOrbVertices.clear();
    m_lightOrbVertices.shrink_to_fit();
    m_lightOrbIndices.clear();
    m_lightOrbIndices.shrink_to_fit();
}

void SceneShadowsGX2::Update(float dt) {
    // 1. Telemetría de FPS
    if (dt > 0.0f) {
        float instantFps = 1.0f / dt;
        m_smoothedFps = m_smoothedFps * 0.95f + instantFps * 0.05f;
    }

    // 2. Rotación automática de cámara (opcional) y control con Sticks del GamePad
    if (m_autoRotateCam) {
        m_targetCamYaw += 0.35f * dt;
    }

    const auto& input = InputManager::Instance();
    const auto& stickL = input.GetLeftStick();
    const auto& stickR = input.GetRightStick();
    if (std::abs(stickL.x) > 0.08f || std::abs(stickL.y) > 0.08f) {
        m_targetCamYaw += stickL.x * 2.2f * dt;
        m_targetCamPitch = std::clamp(m_targetCamPitch - stickL.y * 1.5f * dt, 0.15f, 1.35f);
    }
    if (std::abs(stickR.y) > 0.08f) {
        m_targetCamDistance = std::clamp(m_targetCamDistance - stickR.y * 8.0f * dt, 4.5f, 18.0f);
    }

    // 3. Controles táctiles e interactividad
    const auto& touch = input.GetTouchState();
    bool touched = touch.active;
    float tx = touch.x;
    float ty = touch.y;

    if (touched) {
        // Interacción con el Slider de Brillo integrado: X in [260, 980], Y in [620, 670]
        constexpr float SLIDER_TRACK_X = 280.0f;
        constexpr float SLIDER_TRACK_W = 680.0f;
        if (tx >= (SLIDER_TRACK_X - 20.0f) && tx <= (SLIDER_TRACK_X + SLIDER_TRACK_W + 20.0f) &&
            ty >= 620.0f && ty <= 670.0f) {
            m_isDraggingSlider = true;
            float normalized = std::clamp((tx - SLIDER_TRACK_X) / SLIDER_TRACK_W, 0.0f, 1.0f);
            m_lightBrightness = normalized * 2.0f; // Rango 0.0f a 2.0f (0% a 200%)
        } else if (m_isDraggingSlider) {
            float normalized = std::clamp((tx - SLIDER_TRACK_X) / SLIDER_TRACK_W, 0.0f, 1.0f);
            m_lightBrightness = normalized * 2.0f;
        } else if (ty > 105.0f && ty < 585.0f) {
            // Arrastre en la escena 3D para rotar la cámara (área despejada entre botones superiores y HUD inferior)
            if (!m_isDraggingCamera) {
                m_isDraggingCamera = true;
                m_lastTouchX = tx;
                m_lastTouchY = ty;
            } else {
                float dx = tx - m_lastTouchX;
                float dy = ty - m_lastTouchY;
                m_targetCamYaw += dx * 0.006f;
                m_targetCamPitch = std::clamp(m_targetCamPitch - dy * 0.005f, 0.15f, 1.35f);
                m_lastTouchX = tx;
                m_lastTouchY = ty;
            }
        }

        // ==========================================
        // FILA 1 DE BOTONES (Y in [14, 54], H = 40):
        // ==========================================
        // Botón 1: < VOLVER [25, 14, 140, 40]
        if (touch.justPressed && !m_isDraggingSlider && !m_isDraggingCamera && tx >= 25.0f && tx <= 165.0f && ty >= 14.0f && ty <= 54.0f) {
            AudioManager::Instance().PlayClick();
            SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuGX2>());
            return;
        }

        // Botón 2: RAYTRACING: [ON] / [OFF] [180, 14, 250, 40]
        if (touch.justPressed && !m_isDraggingSlider && !m_isDraggingCamera && tx >= 180.0f && tx <= 430.0f && ty >= 14.0f && ty <= 54.0f) {
            m_raytracingEnabled = !m_raytracingEnabled;
            AudioManager::Instance().PlayClick();
        }

        // Botón 3: RES: [1080P FULLHD] / [720P NATIVO] / [648P->720P] [445, 14, 280, 40]
        if (touch.justPressed && !m_isDraggingSlider && !m_isDraggingCamera && tx >= 445.0f && tx <= 725.0f && ty >= 14.0f && ty <= 54.0f) {
            m_resPresetIndex = (m_resPresetIndex + 1) % 3;
            AudioManager::Instance().PlayClick();
        }

        // Botón 4: CAMARA RESET [740, 14, 210, 40]
        if (touch.justPressed && !m_isDraggingSlider && !m_isDraggingCamera && tx >= 740.0f && tx <= 950.0f && ty >= 14.0f && ty <= 54.0f) {
            m_targetCamYaw = 0.85f;
            m_targetCamPitch = 0.48f;
            m_targetCamDistance = 9.2f;
            AudioManager::Instance().PlayClick();
        }

        // Botón 5: GIRO CAMARA [965, 14, 270, 40]
        if (touch.justPressed && !m_isDraggingSlider && !m_isDraggingCamera && tx >= 965.0f && tx <= 1235.0f && ty >= 14.0f && ty <= 54.0f) {
            m_autoRotateCam = !m_autoRotateCam;
            AudioManager::Instance().PlayClick();
        }

        // ==========================================
        // FILA 2 DE BOTONES (Y in [58, 98], H = 40):
        // ==========================================
        // Botón 6: COLOR [25, 58, 270, 40]
        if (touch.justPressed && !m_isDraggingSlider && !m_isDraggingCamera && tx >= 25.0f && tx <= 295.0f && ty >= 58.0f && ty <= 98.0f) {
            m_lightColorIndex = (m_lightColorIndex + 1) % static_cast<int>(LIGHT_PALETTES.size());
            AudioManager::Instance().PlayClick();
        }

        // Botón 7: ALTURA [310, 58, 240, 40]
        if (touch.justPressed && !m_isDraggingSlider && !m_isDraggingCamera && tx >= 310.0f && tx <= 550.0f && ty >= 58.0f && ty <= 98.0f) {
            if (m_targetLightHeight < 1.8f) m_targetLightHeight = 2.2f;
            else if (m_targetLightHeight < 2.8f) m_targetLightHeight = 3.2f;
            else m_targetLightHeight = 1.4f;
            AudioManager::Instance().PlayClick();
        }
    } else {
        m_isDraggingSlider = false;
        m_isDraggingCamera = false;
    }

    // 4. [OPT-164] Amortiguamiento exponencial suave para cámara orbital y altura de luz
    float smoothAlpha = std::clamp(1.0f - std::exp(-12.0f * dt), 0.0f, 1.0f);
    m_camYaw += (m_targetCamYaw - m_camYaw) * smoothAlpha;
    m_camPitch += (m_targetCamPitch - m_camPitch) * smoothAlpha;
    m_camDistance += (m_targetCamDistance - m_camDistance) * smoothAlpha;
    m_lightHeight += (m_targetLightHeight - m_lightHeight) * smoothAlpha;

    constexpr float TWO_PI = 6.283185307f;
    if (m_camYaw > TWO_PI && m_targetCamYaw > TWO_PI) {
        m_camYaw -= TWO_PI;
        m_targetCamYaw -= TWO_PI;
    } else if (m_camYaw < -TWO_PI && m_targetCamYaw < -TWO_PI) {
        m_camYaw += TWO_PI;
        m_targetCamYaw += TWO_PI;
    }

    m_lightX = 0.0f;
    m_lightZ = 0.0f;
    m_lightY = m_lightHeight;

    // Retorno por hardware con botón 'B'
    if (input.IsBackRequested()) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuGX2>());
    }
}

bool SceneShadowsGX2::InitGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_shaderLoaded && m_buffersCreated) {
        return true;
    }

    // 1. Cargar Shader Group CafeGLSL
    if (!m_shaderLoaded) {
        std::vector<uint8_t> shaderData;
        bool loaded = AssetManager::LoadBinaryFile("shaders/shadows_gx2.gsh", shaderData);
        if (!loaded || shaderData.empty()) {
            shaderData.assign(SHADOWS_GX2_GSH_DATA, SHADOWS_GX2_GSH_DATA + SHADOWS_GX2_GSH_SIZE);
            loaded = true;
        }

        if (loaded && !shaderData.empty()) {
            // Método 1: WHBGfxLoadGFDShaderGroup (idéntico a SceneGX2Native y SceneInteractiveGX2)
            if (WHBGfxLoadGFDShaderGroup(&m_shaderGroup, 0, shaderData.data())) {
                bool a1 = WHBGfxInitShaderAttribute(&m_shaderGroup, "aPos", 0,
                                                    offsetof(ShadowsGX2Vertex, pos),
                                                    GX2_ATTRIB_FORMAT_FLOAT_32_32_32);
                bool a2 = WHBGfxInitShaderAttribute(&m_shaderGroup, "aNormal", 0,
                                                    offsetof(ShadowsGX2Vertex, normal),
                                                    GX2_ATTRIB_FORMAT_FLOAT_32_32_32);
                bool a3 = WHBGfxInitShaderAttribute(&m_shaderGroup, "aColor", 0,
                                                    offsetof(ShadowsGX2Vertex, color),
                                                    GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32);
                if (a1 && a2 && a3 && WHBGfxInitFetchShader(&m_shaderGroup)) {
                    m_shaderLoaded = true;
                    m_useDirectGFD = false;
                }
            }

            // Método 2: Fallback Directo GFD con máscaras canónicas GX2_SEL_MASK
            if (!m_shaderLoaded) {
                uint32_t vsHeaderSize = GFDGetVertexShaderHeaderSize(0, shaderData.data());
                uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
                uint32_t psHeaderSize = GFDGetPixelShaderHeaderSize(0, shaderData.data());
                uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());

                if (vsHeaderSize > 0 && vsProgSize > 0 && psHeaderSize > 0 && psProgSize > 0) {
                    m_directVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHeaderSize, MemoryTag::ShaderProgram, "ShadowsGX2_VS_Hdr"));
                    m_directVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "ShadowsGX2_VS_Prog");
                    m_directPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHeaderSize, MemoryTag::ShaderProgram, "ShadowsGX2_PS_Hdr"));
                    m_directPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "ShadowsGX2_PS_Prog");

                    if (m_directVs && m_directVsProgram && m_directPs && m_directPsProgram) {
                        BOOL vsOk = GFDGetVertexShader(m_directVs, m_directVsProgram, 0, shaderData.data());
                        BOOL psOk = GFDGetPixelShader(m_directPs, m_directPsProgram, 0, shaderData.data());
                        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directVsProgram, vsProgSize);
                        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directPsProgram, psProgSize);

                        if (vsOk && psOk) {
                            GX2AttribStream attrs[3];
                            std::memset(attrs, 0, sizeof(attrs));
                            attrs[0].location = 0;
                            attrs[0].buffer = 0;
                            attrs[0].offset = offsetof(ShadowsGX2Vertex, pos);
                            attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                            attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attrs[0].aluDivisor = 0;
                            attrs[0].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                            attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            attrs[1].location = 1;
                            attrs[1].buffer = 0;
                            attrs[1].offset = offsetof(ShadowsGX2Vertex, normal);
                            attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                            attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attrs[1].aluDivisor = 0;
                            attrs[1].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                            attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            attrs[2].location = 2;
                            attrs[2].buffer = 0;
                            attrs[2].offset = offsetof(ShadowsGX2Vertex, color);
                            attrs[2].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
                            attrs[2].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attrs[2].aluDivisor = 0;
                            attrs[2].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                            attrs[2].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            uint32_t fsSize = GX2CalcFetchShaderSizeEx(3, GX2_FETCH_SHADER_TESSELLATION_NONE,
                                                                       GX2_TESSELLATION_MODE_DISCRETE);
                            m_directFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "ShadowsGX2_FS_Prog");
                            if (m_directFsProgram) {
                                GX2InitFetchShaderEx(&m_directFs, static_cast<uint8_t*>(m_directFsProgram), 3, attrs,
                                                     GX2_FETCH_SHADER_TESSELLATION_NONE,
                                                     GX2_TESSELLATION_MODE_DISCRETE);
                                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directFsProgram, fsSize);
                                m_useDirectGFD = true;
                                m_shaderLoaded = true;
                            }
                        }
                    }
                }
            }
        }
    }

    // 2. Crear Búferes VRAM (Búfer de Vértices Unificado idéntico a SceneGX2Native)
    if (m_shaderLoaded && !m_buffersCreated) {
        std::vector<ShadowsGX2Vertex> allVertices = m_floorVertices;
        uint16_t objectsVertexOffset = static_cast<uint16_t>(allVertices.size());
        allVertices.insert(allVertices.end(), m_objectsVertices.begin(), m_objectsVertices.end());
        uint16_t orbVertexOffset = static_cast<uint16_t>(allVertices.size());
        allVertices.insert(allVertices.end(), m_lightOrbVertices.begin(), m_lightOrbVertices.end());

        m_vertexBuffer.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_vertexBuffer.elemSize = sizeof(ShadowsGX2Vertex);
        m_vertexBuffer.elemCount = static_cast<uint32_t>(allVertices.size());
        if (GX2RCreateBuffer(&m_vertexBuffer)) {
            void* pV = GX2RLockBufferEx(&m_vertexBuffer, GX2R_RESOURCE_BIND_NONE);
            if (pV) {
                std::memcpy(pV, allVertices.data(), m_vertexBuffer.elemSize * m_vertexBuffer.elemCount);
                GX2RUnlockBufferEx(&m_vertexBuffer, GX2R_RESOURCE_BIND_NONE);
            }
        }

        // Index Buffer del Terreno (864 índices)
        m_floorIBO.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_INDEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_floorIBO.elemSize = sizeof(uint16_t);
        m_floorIBO.elemCount = static_cast<uint32_t>(m_floorIndices.size());
        if (GX2RCreateBuffer(&m_floorIBO)) {
            void* pFI = GX2RLockBufferEx(&m_floorIBO, GX2R_RESOURCE_BIND_NONE);
            if (pFI) {
                std::memcpy(pFI, m_floorIndices.data(), m_floorIBO.elemSize * m_floorIBO.elemCount);
                GX2RUnlockBufferEx(&m_floorIBO, GX2R_RESOURCE_BIND_NONE);
            }
        }

        // Index Buffer de las 3 Geometrías 3D (Cubo, Pirámide y Esfera) con offset base de vértices
        std::vector<uint16_t> objectsIndicesOffset = m_objectsIndices;
        for (auto& idx : objectsIndicesOffset) {
            idx += objectsVertexOffset;
        }

        m_objectsIBO.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_INDEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_objectsIBO.elemSize = sizeof(uint16_t);
        m_objectsIBO.elemCount = static_cast<uint32_t>(objectsIndicesOffset.size());
        if (GX2RCreateBuffer(&m_objectsIBO)) {
            void* pOI = GX2RLockBufferEx(&m_objectsIBO, GX2R_RESOURCE_BIND_NONE);
            if (pOI) {
                std::memcpy(pOI, objectsIndicesOffset.data(), m_objectsIBO.elemSize * m_objectsIBO.elemCount);
                GX2RUnlockBufferEx(&m_objectsIBO, GX2R_RESOURCE_BIND_NONE);
            }
        }

        // Index Buffer del Orbe (576 índices) con offset base de vértices
        std::vector<uint16_t> orbIndicesOffset = m_lightOrbIndices;
        for (auto& idx : orbIndicesOffset) {
            idx += orbVertexOffset;
        }

        m_lightOrbIBO.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_INDEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_lightOrbIBO.elemSize = sizeof(uint16_t);
        m_lightOrbIBO.elemCount = static_cast<uint32_t>(orbIndicesOffset.size());
        if (GX2RCreateBuffer(&m_lightOrbIBO)) {
            void* pLI = GX2RLockBufferEx(&m_lightOrbIBO, GX2R_RESOURCE_BIND_NONE);
            if (pLI) {
                std::memcpy(pLI, orbIndicesOffset.data(), m_lightOrbIBO.elemSize * m_lightOrbIBO.elemCount);
                GX2RUnlockBufferEx(&m_lightOrbIBO, GX2R_RESOURCE_BIND_NONE);
            }
        }

        m_buffersCreated = true;
    }

    // 3. Crear Context State y Z-Buffer en eDRAM
    if (!m_nativeContextState) {
        m_nativeContextState = static_cast<GX2ContextState*>(MemoryManager::Instance().AllocAligned(
            GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState), MemoryTag::ContextState, "ShadowsGX2_ContextState"));
        if (m_nativeContextState) {
            GX2SetupContextStateEx(m_nativeContextState, TRUE);
        }
    }

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
        m_depthBuffer.viewMip = 0;
        m_depthBuffer.viewFirstSlice = 0;
        m_depthBuffer.viewNumSlices = 1;
        m_depthBuffer.depthClear = 1.0f;
        m_depthBuffer.stencilClear = 0;

        GX2CalcSurfaceSizeAndAlignment(&m_depthBuffer.surface);
        if (GX2RCreateSurface(&m_depthBuffer.surface,
                              static_cast<GX2RResourceFlags>(GX2R_RESOURCE_BIND_DEPTH_BUFFER |
                                                             GX2R_RESOURCE_USAGE_GPU_READ |
                                                             GX2R_RESOURCE_USAGE_GPU_WRITE))) {
            GX2InitDepthBufferRegs(&m_depthBuffer);
            GX2InitDepthBufferHiZEnable(&m_depthBuffer, FALSE);
            m_depthBufferCreated = true;
        }
    }

    return m_shaderLoaded && m_buffersCreated && m_depthBufferCreated;
#else
    return true;
#endif
}

void SceneShadowsGX2::DestroyGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown()) {
        GX2DrawDone();
    }

    if (m_buffersCreated) {
        GX2RDestroyBufferEx(&m_vertexBuffer, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_floorIBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_objectsIBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_lightOrbIBO, GX2R_RESOURCE_BIND_NONE);
        m_buffersCreated = false;
    }

    if (m_useDirectGFD) {
        if (m_directFsProgram) { MemoryManager::Instance().FreeAligned(m_directFsProgram); }
        if (m_directPsProgram) { MemoryManager::Instance().FreeAligned(m_directPsProgram); }
        if (m_directPs) { void* raw = static_cast<void*>(m_directPs); MemoryManager::Instance().Free(raw); m_directPs = nullptr; }
        if (m_directVsProgram) { MemoryManager::Instance().FreeAligned(m_directVsProgram); }
        if (m_directVs) { void* raw = static_cast<void*>(m_directVs); MemoryManager::Instance().Free(raw); m_directVs = nullptr; }
        m_useDirectGFD = false;
        m_shaderLoaded = false;
    } else if (m_shaderLoaded) {
        WHBGfxFreeShaderGroup(&m_shaderGroup);
        m_shaderLoaded = false;
    }

    if (m_depthBufferCreated) {
        GX2RDestroySurfaceEx(&m_depthBuffer.surface, GX2R_RESOURCE_BIND_NONE);
        m_depthBufferCreated = false;
    }

    if (m_nativeContextState) {
        void* rawCtx = static_cast<void*>(m_nativeContextState);
        MemoryManager::Instance().FreeAligned(rawCtx);
        m_nativeContextState = nullptr;
    }

    m_pColorBuffer = nullptr;
    m_colorBufferBound = false;
#endif
}

void SceneShadowsGX2::BindColorBuffer(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!renderer) return;

    m_pColorBuffer = GetSDL2ColorBuffer(renderer);
    if (m_pColorBuffer && m_nativeContextState) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
        if (m_depthBufferCreated) {
            GX2SetDepthBuffer(&m_depthBuffer);
        }
        m_colorBufferBound = true;
    }
#else
    (void)renderer;
    m_colorBufferBound = true;
#endif
}

void SceneShadowsGX2::Render(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_shaderLoaded || !m_buffersCreated) {
        InitGX2Pipeline();
    }
    if (!m_colorBufferBound && renderer) {
        BindColorBuffer(renderer);
    }

    if (!m_shaderLoaded || !m_buffersCreated || !m_pColorBuffer || !m_nativeContextState) {
        return;
    }

    GX2SetContextState(m_nativeContextState);
    GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    if (m_depthBufferCreated) {
        GX2SetDepthBuffer(&m_depthBuffer);
    }
    float vpW = 1280.0f;
    float vpH = 720.0f;
    float vpX = 0.0f;
    float vpY = 0.0f;
    if (m_resPresetIndex == 2) {
        vpW = 1152.0f;
        vpH = 648.0f;
        vpX = (1280.0f - 1152.0f) * 0.5f;
        vpY = (720.0f - 648.0f) * 0.5f;
    }
    GX2SetViewport(vpX, vpY, vpW, vpH, 0.0f, 1.0f);
    GX2SetScissor(static_cast<uint32_t>(vpX), static_cast<uint32_t>(vpY),
                  static_cast<uint32_t>(vpW), static_cast<uint32_t>(vpH));

    GX2ClearColor(m_pColorBuffer, 0.04f, 0.06f, 0.10f, 1.0f);
    if (m_depthBufferCreated) {
        GX2ClearDepthStencilEx(&m_depthBuffer, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
    }

    RenderScene3D_GX2();
    RenderHUD_GX2();
#else
    (void)renderer;
#endif
}

void SceneShadowsGX2::RenderScene3D_GX2() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_pColorBuffer || !m_nativeContextState || !m_shaderLoaded || !m_buffersCreated) return;

    m_gpuDraws = 0;
    m_triangleCount = 0;

    // 0. Re-activar Context State aislado tras GX2ClearColor/GX2ClearDepthStencilEx
    GX2SetContextState(m_nativeContextState);
    GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    if (m_depthBufferCreated) {
        GX2SetDepthBuffer(&m_depthBuffer);
    }
    float vpW = 1280.0f;
    float vpH = 720.0f;
    float vpX = 0.0f;
    float vpY = 0.0f;
    if (m_resPresetIndex == 2) {
        vpW = 1152.0f;
        vpH = 648.0f;
        vpX = (1280.0f - 1152.0f) * 0.5f;
        vpY = (720.0f - 648.0f) * 0.5f;
    }
    GX2SetViewport(vpX, vpY, vpW, vpH, 0.0f, 1.0f);
    GX2SetScissor(static_cast<uint32_t>(vpX), static_cast<uint32_t>(vpY),
                  static_cast<uint32_t>(vpW), static_cast<uint32_t>(vpH));

    // 1. Activación del Pipeline de Shaders
    if (m_useDirectGFD) {
        if (!m_directVs || !m_directPs) return;
        GX2SetFetchShader(&m_directFs);
        GX2SetVertexShader(m_directVs);
        GX2SetPixelShader(m_directPs);
    } else {
        GX2SetFetchShader(&m_shaderGroup.fetchShader);
        GX2SetVertexShader(m_shaderGroup.vertexShader);
        GX2SetPixelShader(m_shaderGroup.pixelShader);
    }

    // 2. Estados de Rasterización: sin culling para garantizar visibilidad total del terreno, objetos y orbe
    GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, FALSE,
                         GX2_POLYGON_MODE_TRIANGLE, GX2_POLYGON_MODE_TRIANGLE,
                         FALSE, FALSE, FALSE);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);

    // 3. Matrices de Cámara
    float camX = m_camDistance * std::cos(m_camPitch) * std::sin(m_camYaw);
    float camY = m_camDistance * std::sin(m_camPitch);
    float camZ = m_camDistance * std::cos(m_camPitch) * std::cos(m_camYaw);

    Point3D eye(camX, camY, camZ);
    Point3D target(0.0f, 0.8f, 0.0f);
    Point3D up(0.0f, 1.0f, 0.0f);

    Matrix4x4 viewMat = Matrix4x4::LookAt(eye, target, up);
    Matrix4x4 projMat = Matrix4x4::Perspective(48.0f * (3.14159265f / 180.0f), 1280.0f / 720.0f, 0.2f, 100.0f);
    Matrix4x4 vpMat = projMat * viewMat;
    Matrix4x4 modelIdent = Matrix4x4::Identity();
    Matrix4x4 mvpIdent = vpMat * modelIdent;

    // 4. Vincular Búfer de Vértices Unificado (compartido por suelo, objetos 3D y orbe)
    GX2RSetAttributeBuffer(&m_vertexBuffer, 0, sizeof(ShadowsGX2Vertex), 0);

    const auto& pal = LIGHT_PALETTES[m_lightColorIndex];
    float ambLift = std::clamp(m_lightBrightness, 0.0f, 1.0f);
    float ambR = 0.14f + 0.08f * ambLift;
    float ambG = 0.16f + 0.09f * ambLift;
    float ambB = 0.22f + 0.10f * ambLift;

    alignas(256) float vsUniforms[32];
    alignas(256) float psUniforms[20];

    // ==========================================
    // PASE 1: Suelo / Terreno de 24x24 (con Sombras Proyectadas)
    // ==========================================
    {
        // 1. Uniforms del Vertex Shader (KC2[0..3] = MVP, KC2[4..7] = Model)
        std::memcpy(&vsUniforms[0], mvpIdent.m, 16 * sizeof(float));
        std::memcpy(&vsUniforms[16], modelIdent.m, 16 * sizeof(float));
        GX2SetVertexUniformReg(0, 32, vsUniforms);

        // 2. Uniforms del Pixel Shader (KC2[0..4] = 20 floats contiguos)
        // KC2[0]: uLightPos
        psUniforms[0]  = m_lightX; psUniforms[1]  = m_lightY; psUniforms[2]  = m_lightZ; psUniforms[3]  = 8.0f;
        // KC2[1]: uLightColor
        psUniforms[4]  = pal.r;    psUniforms[5]  = pal.g;    psUniforms[6]  = pal.b;    psUniforms[7]  = m_lightBrightness;
        // KC2[2]: uCamPos
        psUniforms[8]  = camX;     psUniforms[9]  = camY;     psUniforms[10] = camZ;     psUniforms[11] = 1.0f;
        // KC2[3]: uAmbient (visibilidad nocturna base al 0% + incremento ambiental suave)
        psUniforms[12] = ambR;     psUniforms[13] = ambG;     psUniforms[14] = ambB;     psUniforms[15] = 32.0f;
        // KC2[4]: uMaterialParams (x: isFloor = 1.0, y: isLightOrb = 0.0, z: isRaytracing, w: is648p)
        psUniforms[16] = 1.0f;     psUniforms[17] = 0.0f;     psUniforms[18] = m_raytracingEnabled ? 1.0f : 0.0f;     psUniforms[19] = (m_resPresetIndex == 2) ? 1.0f : 0.0f;
        GX2SetPixelUniformReg(0, 20, psUniforms);

        // Configuración de Hardware Z-Buffer y Rasterización con RHI Pipeline Cache
        RHIPipelineState state(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false, RHI_FRONT_FACE_CCW);
        RHIPipelineCache::Instance().BindState(state);
        GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);

        GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_floorIBO, GX2_INDEX_TYPE_U16,
                        m_floorIBO.elemCount, 0, 0, 1);
        m_gpuDraws++;
        m_triangleCount += m_floorIBO.elemCount / 3;
    }

    // ==========================================
    // PASE 2: Geometrías 3D Básicas (Cubo Coral, Pirámide Esmeralda y Esfera Zafiro)
    // ==========================================
    {
        // Uniforms del Pixel Shader para Geometrías 3D (x: isFloor = 0.0, y: isLightOrb = 0.0, z: isRaytracing, w: is648p)
        psUniforms[16] = 0.0f;     psUniforms[17] = 0.0f;     psUniforms[18] = m_raytracingEnabled ? 1.0f : 0.0f;     psUniforms[19] = (m_resPresetIndex == 2) ? 1.0f : 0.0f;
        GX2SetPixelUniformReg(0, 20, psUniforms);

        GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_objectsIBO, GX2_INDEX_TYPE_U16,
                        m_objectsIBO.elemCount, 0, 0, 1);
        m_gpuDraws++;
        m_triangleCount += m_objectsIBO.elemCount / 3;
    }

    // ==========================================
    // PASE 3: Orbe de Luz Central Emisivo
    // ==========================================
    {
        Matrix4x4 modelOrb = Matrix4x4::Translation(m_lightX, m_lightY, m_lightZ);
        Matrix4x4 mvpOrb = vpMat * modelOrb;

        // 1. Uniforms del Vertex Shader para el Orbe
        std::memcpy(&vsUniforms[0], mvpOrb.m, 16 * sizeof(float));
        std::memcpy(&vsUniforms[16], modelOrb.m, 16 * sizeof(float));
        GX2SetVertexUniformReg(0, 32, vsUniforms);

        // 2. Uniforms del Pixel Shader para el Orbe (x: isFloor = 0.0, y: isLightOrb = 1.0, z: isRaytracing = 0.0, w: is648p = 0.0)
        psUniforms[16] = 0.0f; psUniforms[17] = 1.0f; psUniforms[18] = 0.0f; psUniforms[19] = 0.0f;
        GX2SetPixelUniformReg(0, 20, psUniforms);

        GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_lightOrbIBO, GX2_INDEX_TYPE_U16,
                        m_lightOrbIBO.elemCount, 0, 0, 1);
        m_gpuDraws++;
        m_triangleCount += m_lightOrbIBO.elemCount / 3;
    }

    GX2Flush();
#endif
}

void SceneShadowsGX2::RenderHUD_GX2() {
#if defined(__WIIU__) || defined(__wiiu__)
    auto& r2d = Render2DGX2::Instance();

    if (m_nativeContextState && m_pColorBuffer) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    }

    r2d.Begin();

    // 1. Fila 1 de Botones del HUD (Y = 14.0f, H = 40.0f, Escala Canónica 2.0f)
    r2d.DrawButton("< VOLVER", 25.0f, 14.0f, 140.0f, 40.0f, false,
                   ColorRGBA{12, 18, 32, 255}, ColorRGBA{200, 40, 40, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    const char* rtText = m_raytracingEnabled ? "RAYTRACING: [ON]" : "RAYTRACING: [OFF]";
    ColorRGBA rtBg = m_raytracingEnabled ? ColorRGBA{20, 45, 65, 240} : ColorRGBA{28, 28, 38, 230};
    ColorRGBA rtBorder = m_raytracingEnabled ? ColorRGBA::Cyan() : ColorRGBA{140, 150, 170, 255};
    r2d.DrawButton(rtText, 180.0f, 14.0f, 250.0f, 40.0f, false,
                   rtBg, ColorRGBA{0, 180, 240, 240},
                   rtBorder, ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    const char* resText = (m_resPresetIndex == 0) ? "RES: [1080P FULLHD]" : ((m_resPresetIndex == 1) ? "RES: [720P NATIVO]" : "RES: [648P->720P]");
    ColorRGBA resBg = (m_resPresetIndex == 0) ? ColorRGBA{20, 50, 45, 240} : ((m_resPresetIndex == 1) ? ColorRGBA{20, 40, 55, 230} : ColorRGBA{35, 30, 55, 240});
    r2d.DrawButton(resText, 445.0f, 14.0f, 280.0f, 40.0f, false,
                   resBg, ColorRGBA{0, 180, 240, 240},
                   ColorRGBA{0, 210, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    r2d.DrawButton("CAMARA: RESET", 740.0f, 14.0f, 210.0f, 40.0f, false,
                   ColorRGBA{20, 35, 55, 230}, ColorRGBA{0, 180, 240, 240},
                   ColorRGBA{0, 210, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    const char* orbitText = m_autoRotateCam ? "GIRO CAM: [ON]" : "GIRO CAM: [OFF]";
    r2d.DrawButton(orbitText, 965.0f, 14.0f, 270.0f, 40.0f, false,
                   ColorRGBA{20, 35, 55, 230}, ColorRGBA{0, 180, 240, 240},
                   ColorRGBA{0, 210, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    // 2. Fila 2 de Botones y Panel Informativo (Y = 58.0f, H = 40.0f, Escala Canónica 2.0f)
    StackString<64> colBtn;
    colBtn.Format("COLOR: [%s]", LIGHT_PALETTES[m_lightColorIndex].name);
    r2d.DrawButton(colBtn.string_view(), 25.0f, 58.0f, 270.0f, 40.0f, false,
                   ColorRGBA{20, 35, 55, 230}, ColorRGBA{0, 180, 240, 240},
                   ColorRGBA{0, 210, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    StackString<64> hBtn;
    hBtn.Format("ALTURA: [%.1f M]", m_targetLightHeight);
    r2d.DrawButton(hBtn.string_view(), 310.0f, 58.0f, 240.0f, 40.0f, false,
                   ColorRGBA{20, 35, 55, 230}, ColorRGBA{0, 180, 240, 240},
                   ColorRGBA{0, 210, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    // Panel informativo de estado
    r2d.DrawBox(565.0f, 58.0f, 670.0f, 40.0f, ColorRGBA{14, 26, 46, 235}, ColorRGBA::Cyan(), 1.5f);
    if (m_raytracingEnabled) {
        r2d.DrawText("MODO: [RAY-TRACED REFLECTIONS & SHADOWS GX2]", 580.0f, 68.0f, 2.0f, ColorRGBA::Yellow());
    } else {
        r2d.DrawText("MODO: [RASTER & SHADOWS SDF PCSS (RT APAGADO)]", 580.0f, 68.0f, 2.0f, ColorRGBA{180, 205, 235, 255});
    }

    // 3. Banner Inferior Unificado de Telemetría y Controles (x: 20, y: 590, w: 1240, h: 112)
    constexpr float HUD_X = 20.0f;
    constexpr float HUD_Y = 590.0f;
    constexpr float HUD_W = 1240.0f;
    constexpr float HUD_H = 112.0f;
    r2d.DrawBox(HUD_X, HUD_Y, HUD_W, HUD_H, ColorRGBA{10, 22, 38, 235}, ColorRGBA::Cyan(), 2.0f);

    // Línea 1: Telemetría y Diagnóstico (Escala Canónica 2.0f)
    StackString<256> line1;
    const char* resStr = (m_resPresetIndex == 0) ? "1080P FULLHD (2.07M PX)" : ((m_resPresetIndex == 1) ? "720P NATIVO (922K PX)" : "648P->720P (746K PX)");
    const char* rtStr = m_raytracingEnabled ? "RT: [ON]" : "RT: [OFF]";
    line1.Format("DEMO 9: %s | %s | FPS: %4.1f | DRAWS: %u | TRIS: %u",
                 rtStr, resStr, m_smoothedFps, m_gpuDraws, m_triangleCount);
    r2d.DrawText(line1.string_view(), HUD_X + 18.0f, HUD_Y + 12.0f, 2.0f, ColorRGBA::Yellow());

    // Línea 2: Slider Interactivo de Brillo Integrado (Escala Canónica 2.0f)
    constexpr float SLIDER_X = HUD_X + 260.0f;
    constexpr float SLIDER_Y = HUD_Y + 44.0f;
    constexpr float SLIDER_W = 680.0f;
    constexpr float SLIDER_H = 22.0f;

    int brightPercent = static_cast<int>(m_lightBrightness * 100.0f + 0.5f);
    StackString<64> brightLabel;
    brightLabel.Format("BRILLO LUZ: %3d%%", brightPercent);
    r2d.DrawText(brightLabel.string_view(), HUD_X + 18.0f, HUD_Y + 48.0f, 2.0f, ColorRGBA::White());

    // Riel del Slider
    r2d.DrawBox(SLIDER_X, SLIDER_Y, SLIDER_W, SLIDER_H,
                ColorRGBA{15, 25, 45, 255}, ColorRGBA{70, 120, 190, 255}, 1.5f);

    // Barra de Relleno Proporcional al Brillo
    float fillRatio = std::clamp(m_lightBrightness / 2.0f, 0.0f, 1.0f);
    float fillW = SLIDER_W * fillRatio;
    if (fillW > 3.0f) {
        const auto& curPal = LIGHT_PALETTES[m_lightColorIndex].uiColor;
        r2d.DrawRect(SLIDER_X + 2.0f, SLIDER_Y + 2.0f, fillW - 4.0f, SLIDER_H - 4.0f,
                     ColorRGBA{curPal.r, curPal.g, curPal.b, 220});
    }

    // Cursor / Mango del Slider
    float thumbX = SLIDER_X + fillRatio * SLIDER_W - 8.0f;
    r2d.DrawBox(thumbX, SLIDER_Y - 4.0f, 16.0f, SLIDER_H + 8.0f,
                ColorRGBA::White(), ColorRGBA::Yellow(), 2.0f);

    // Estado del Brillo a la derecha del Slider (Escala Canónica 2.0f)
    if (m_lightBrightness <= 0.01f) {
        r2d.DrawText("[APAGADA]", SLIDER_X + SLIDER_W + 18.0f, HUD_Y + 48.0f, 2.0f, ColorRGBA{255, 110, 110, 255});
    } else if (m_lightBrightness <= 1.05f) {
        r2d.DrawText("[NOMINAL]", SLIDER_X + SLIDER_W + 18.0f, HUD_Y + 48.0f, 2.0f, ColorRGBA::Cyan());
    } else {
        r2d.DrawText("[MAXIMO]", SLIDER_X + SLIDER_W + 18.0f, HUD_Y + 48.0f, 2.0f, ColorRGBA::Yellow());
    }

    // Línea 3: Guía de Controles Rápidos (Escala Canónica 2.0f)
    r2d.DrawText("[STYLUS]: DESLIZAR BRILLO Y ORBITAR CAMARA | [STICK L]: ROTAR 3D | [B]: VOLVER",
                 HUD_X + 18.0f, HUD_Y + 80.0f, 2.0f, ColorRGBA{180, 220, 255, 255});

    r2d.End();
#endif
}

} // namespace WiiU
