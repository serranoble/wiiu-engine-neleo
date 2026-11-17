#include "Scene/SceneInteractiveGX2.hpp"
#include "Scene/SceneMenuGX2.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include "Graphics/GX2StateHelper.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/NativeGX2ShaderBinary.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/StackString.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <malloc.h>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/utils.h>
#include <gx2/shaders.h>
#endif

namespace WiiU {

SceneInteractiveGX2::SceneInteractiveGX2()
    : m_backButton(20, 12, 140, 40, "< VOLVER", 2),
      m_colorButton(170, 12, 230, 40, "COLOR: CAMBIAR", 2),
      m_rotateButton(410, 12, 260, 40, "GIRO: AUTOMATICO", 2),
      m_lightButton(680, 12, 260, 40, "LUZ: ORBITAL 3D", 2),
      m_resetButton(950, 12, 310, 40, "RESET: POSICION", 2) {

    // Configuración estética de botones en silicio
    ColorRGBA normalFill{20, 35, 55, 230};
    ColorRGBA hoverFill{0, 180, 240, 240};
    ColorRGBA normalBorder{0, 210, 255, 255};
    ColorRGBA hoverBorder = ColorRGBA::Yellow();
    ColorRGBA textColor = ColorRGBA::White();

    m_backButton.SetColors(ColorRGBA{20, 30, 50, 220}, ColorRGBA{200, 40, 40, 230},
                           ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White());
    m_colorButton.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);
    m_rotateButton.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);
    m_lightButton.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);
    m_resetButton.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);

    // Inicializar geometría y paleta
    InitCubeGeometry();
    ResetWidgetPosition();

#if defined(__WIIU__) || defined(__wiiu__)
    InitGX2Pipeline();
#endif
}

SceneInteractiveGX2::~SceneInteractiveGX2() {
    OnExit();
}

void SceneInteractiveGX2::OnEnter() {
    m_lastFrameTicks = SDL_GetTicks();
    m_smoothedFps = 60.0f;
    AudioManager::Instance().StopBGM();

    if (m_cubeVertices.empty()) {
        InitCubeGeometry();
    }

#if defined(__WIIU__) || defined(__wiiu__)
    InitGX2Pipeline();
#endif
}

void SceneInteractiveGX2::OnExit() {
#if defined(__WIIU__) || defined(__wiiu__)
    DestroyGX2Pipeline();
#endif
    // Desalojo proactivo de memoria en heap (OPT-45)
    m_cubeVertices.clear();
    m_cubeVertices.shrink_to_fit();
    m_cubeIndices.clear();
    m_cubeIndices.shrink_to_fit();
}

void SceneInteractiveGX2::InitCubeGeometry() {
    // 6 Caras de un Cubo 3D de 2.0x2.0x2.0 unidades centradas en el origen
    // 24 vértices (4 por cara con sus respectivas normales)
    constexpr float S = 1.0f;

    struct FaceDef {
        float normal[3];
        float corners[4][3];
    };

    static const FaceDef faces[6] = {
        // Frente (+Z)
        { { 0.0f,  0.0f,  1.0f}, { {-S, -S,  S}, { S, -S,  S}, { S,  S,  S}, {-S,  S,  S} } },
        // Atrás (-Z)
        { { 0.0f,  0.0f, -1.0f}, { { S, -S, -S}, {-S, -S, -S}, {-S,  S, -S}, { S,  S, -S} } },
        // Derecha (+X)
        { { 1.0f,  0.0f,  0.0f}, { { S, -S,  S}, { S, -S, -S}, { S,  S, -S}, { S,  S,  S} } },
        // Izquierda (-X)
        { {-1.0f,  0.0f,  0.0f}, { {-S, -S, -S}, {-S, -S,  S}, {-S,  S,  S}, {-S,  S, -S} } },
        // Arriba (+Y)
        { { 0.0f,  1.0f,  0.0f}, { {-S,  S,  S}, { S,  S,  S}, { S,  S, -S}, {-S,  S, -S} } },
        // Abajo (-Y)
        { { 0.0f, -1.0f,  0.0f}, { {-S, -S, -S}, { S, -S, -S}, { S, -S,  S}, {-S, -S,  S} } }
    };

    // Paleta de colores base por cara
    static const ColorRGBA paletteNeon[6] = {
        ColorRGBA{0, 240, 255, 255},   // Cyan neón
        ColorRGBA{255, 0, 160, 255},   // Magenta neón
        ColorRGBA{255, 220, 0, 255},   // Amarillo dorado
        ColorRGBA{0, 255, 120, 255},   // Verde esmeralda
        ColorRGBA{160, 32, 240, 255},  // Púrpura cósmico
        ColorRGBA{255, 90, 0, 255}     // Naranja solar
    };

    m_cubeVertices.clear();
    m_cubeVertices.reserve(24);
    m_cubeIndices.clear();
    m_cubeIndices.reserve(36);

    for (int f = 0; f < 6; ++f) {
        uint16_t baseIdx = static_cast<uint16_t>(m_cubeVertices.size());
        const auto& col = paletteNeon[f];
        float r = col.r * (1.0f / 255.0f);
        float g = col.g * (1.0f / 255.0f);
        float b = col.b * (1.0f / 255.0f);

        for (int v = 0; v < 4; ++v) {
            InteractiveGX2Vertex vert;
            vert.pos[0] = faces[f].corners[v][0];
            vert.pos[1] = faces[f].corners[v][1];
            vert.pos[2] = faces[f].corners[v][2];

            vert.normal[0] = faces[f].normal[0];
            vert.normal[1] = faces[f].normal[1];
            vert.normal[2] = faces[f].normal[2];

            vert.color[0] = r;
            vert.color[1] = g;
            vert.color[2] = b;
            vert.color[3] = 1.0f;

            m_cubeVertices.push_back(vert);
        }

        // 2 Triángulos por cara (CCW)
        m_cubeIndices.push_back(baseIdx + 0);
        m_cubeIndices.push_back(baseIdx + 1);
        m_cubeIndices.push_back(baseIdx + 2);

        m_cubeIndices.push_back(baseIdx + 0);
        m_cubeIndices.push_back(baseIdx + 2);
        m_cubeIndices.push_back(baseIdx + 3);
    }
}

void SceneInteractiveGX2::MutateCubePalette() {
    m_paletteIndex = (m_paletteIndex + 1) % 4;

    static const ColorRGBA palettes[4][6] = {
        // Paleta 0: Cyberpunk Neón
        { {0, 240, 255, 255}, {255, 0, 160, 255}, {255, 220, 0, 255},
          {0, 255, 120, 255}, {160, 32, 240, 255}, {255, 90, 0, 255} },
        // Paleta 1: Vaporwave Atardecer
        { {255, 110, 180, 255}, {120, 220, 255, 255}, {255, 200, 90, 255},
          {180, 100, 240, 255}, {255, 80, 120, 255}, {70, 130, 240, 255} },
        // Paleta 2: Matrix Esmeralda
        { {0, 255, 100, 255}, {40, 200, 60, 255}, {180, 255, 140, 255},
          {20, 160, 80, 255}, {100, 255, 200, 255}, {0, 220, 140, 255} },
        // Paleta 3: Fuego Solar & Oro
        { {255, 50, 0, 255}, {255, 140, 0, 255}, {255, 220, 0, 255},
          {240, 30, 70, 255}, {255, 180, 50, 255}, {200, 40, 0, 255} }
    };

    const auto& curPal = palettes[m_paletteIndex];

    for (int f = 0; f < 6; ++f) {
        const auto& col = curPal[f];
        float r = col.r * (1.0f / 255.0f);
        float g = col.g * (1.0f / 255.0f);
        float b = col.b * (1.0f / 255.0f);

        for (int v = 0; v < 4; ++v) {
            size_t idx = f * 4 + v;
            if (idx < m_cubeVertices.size()) {
                m_cubeVertices[idx].color[0] = r;
                m_cubeVertices[idx].color[1] = g;
                m_cubeVertices[idx].color[2] = b;
                m_cubeVertices[idx].color[3] = 1.0f;
            }
        }
    }

#if defined(__WIIU__) || defined(__wiiu__)
    if (m_buffersCreated) {
        void* ptr = GX2RLockBufferEx(&m_vertexBuffer, GX2R_RESOURCE_BIND_NONE);
        if (ptr) {
            std::memcpy(ptr, m_cubeVertices.data(), sizeof(InteractiveGX2Vertex) * m_cubeVertices.size());
            GX2RUnlockBufferEx(&m_vertexBuffer, GX2R_RESOURCE_BIND_NONE);
        }
    }
#endif
}

void SceneInteractiveGX2::SpawnShockwave(float x, float y, const ColorRGBA& col) {
    for (auto& ring : m_shockwaves) {
        if (!ring.active) {
            ring.x = x;
            ring.y = y;
            ring.radius = 12.0f;
            ring.maxRadius = 150.0f;
            ring.alpha = 1.0f;
            ring.color = col;
            ring.active = true;
            return;
        }
    }

    // Si todos están ocupados, reutilizamos el primero
    m_shockwaves[0].x = x;
    m_shockwaves[0].y = y;
    m_shockwaves[0].radius = 12.0f;
    m_shockwaves[0].maxRadius = 150.0f;
    m_shockwaves[0].alpha = 1.0f;
    m_shockwaves[0].color = col;
    m_shockwaves[0].active = true;
}

void SceneInteractiveGX2::ResetWidgetPosition() {
    m_isDraggingWidget = false;
    m_isHomingToAnchor = true;
}

bool SceneInteractiveGX2::InitGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_shaderLoaded && m_buffersCreated) {
        return true;
    }

    // 1. Cargar Shader Group de CafeGLSL
    if (!m_shaderLoaded) {
        std::vector<uint8_t> shaderData;
        bool loaded = AssetManager::LoadBinaryFile("shaders/native_gx2.gsh", shaderData);
        if (!loaded || shaderData.empty()) {
            shaderData.assign(NATIVE_GX2_GSH_DATA, NATIVE_GX2_GSH_DATA + NATIVE_GX2_GSH_SIZE);
            loaded = true;
        }

        if (loaded && !shaderData.empty()) {
            uint32_t vsHeaderSize = GFDGetVertexShaderHeaderSize(0, shaderData.data());
            uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
            uint32_t psHeaderSize = GFDGetPixelShaderHeaderSize(0, shaderData.data());
            uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());

            if (vsHeaderSize > 0 && vsProgSize > 0 && psHeaderSize > 0 && psProgSize > 0) {
                m_directVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHeaderSize, MemoryTag::ShaderProgram, "Inter_VS_Hdr"));
                m_directVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "Inter_VS_Prog");
                m_directPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHeaderSize, MemoryTag::ShaderProgram, "Inter_PS_Hdr"));
                m_directPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "Inter_PS_Prog");

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
                        attrs[0].offset = offsetof(InteractiveGX2Vertex, pos);
                        attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                        attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[0].aluDivisor = 0;
                        attrs[0].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                        attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                        attrs[1].location = 1;
                        attrs[1].buffer = 0;
                        attrs[1].offset = offsetof(InteractiveGX2Vertex, normal);
                        attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                        attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[1].aluDivisor = 0;
                        attrs[1].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                        attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                        attrs[2].location = 2;
                        attrs[2].buffer = 0;
                        attrs[2].offset = offsetof(InteractiveGX2Vertex, color);
                        attrs[2].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
                        attrs[2].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[2].aluDivisor = 0;
                        attrs[2].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                        attrs[2].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                        uint32_t fsSize = GX2CalcFetchShaderSizeEx(3, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                        m_directFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "Inter_FS_Prog");
                        if (m_directFsProgram) {
                            GX2InitFetchShaderEx(&m_directFetchShader, static_cast<uint8_t*>(m_directFsProgram), 3, attrs,
                                                 GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directFsProgram, fsSize);
                            m_shaderLoaded = true;
                            m_useDirectGFD = true;
                        }
                    }
                }
            }
        }
    }

    // 2. Crear Vertex Buffer e Index Buffer en VRAM
    if (m_shaderLoaded && !m_buffersCreated) {
        m_vertexBuffer.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_vertexBuffer.elemSize = sizeof(InteractiveGX2Vertex);
        m_vertexBuffer.elemCount = static_cast<uint32_t>(m_cubeVertices.size());

        bool vboOk = false;
        if (GX2RCreateBuffer(&m_vertexBuffer)) {
            void* ptr = GX2RLockBufferEx(&m_vertexBuffer, GX2R_RESOURCE_BIND_NONE);
            if (ptr) {
                std::memcpy(ptr, m_cubeVertices.data(), sizeof(InteractiveGX2Vertex) * m_cubeVertices.size());
                GX2RUnlockBufferEx(&m_vertexBuffer, GX2R_RESOURCE_BIND_NONE);
                vboOk = true;
            }
        }

        m_indexBuffer.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_INDEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_indexBuffer.elemSize = sizeof(uint16_t);
        m_indexBuffer.elemCount = static_cast<uint32_t>(m_cubeIndices.size());

        bool iboOk = false;
        if (GX2RCreateBuffer(&m_indexBuffer)) {
            void* ptr = GX2RLockBufferEx(&m_indexBuffer, GX2R_RESOURCE_BIND_NONE);
            if (ptr) {
                std::memcpy(ptr, m_cubeIndices.data(), sizeof(uint16_t) * m_cubeIndices.size());
                GX2RUnlockBufferEx(&m_indexBuffer, GX2R_RESOURCE_BIND_NONE);
                iboOk = true;
            }
        }

        m_buffersCreated = (vboOk && iboOk);
    }

    // 3. Crear Hardware Z-Buffer (Depth Buffer) en eDRAM de Latte
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

    // 4. Crear GX2ContextState aislado para el Sandbox
    if (!m_nativeContextState) {
        m_nativeContextState = static_cast<GX2ContextState*>(MemoryManager::Instance().AllocAligned(
            GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState), MemoryTag::ContextState, "Interactive_ContextState"));
        if (m_nativeContextState) {
            GX2SetupContextStateEx(m_nativeContextState, TRUE);
        }
    }

    return m_shaderLoaded && m_buffersCreated;
#else
    return true;
#endif
}

void SceneInteractiveGX2::DestroyGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_nativeContextState) {
        void* rawCtx = static_cast<void*>(m_nativeContextState);
        MemoryManager::Instance().FreeAligned(rawCtx);
        m_nativeContextState = nullptr;
    }

    if (m_depthBufferCreated) {
        GX2RDestroySurfaceEx(&m_depthBuffer.surface, GX2R_RESOURCE_BIND_NONE);
        m_depthBufferCreated = false;
    }

    if (m_buffersCreated) {
        GX2RDestroyBufferEx(&m_indexBuffer, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_vertexBuffer, GX2R_RESOURCE_BIND_NONE);
        m_buffersCreated = false;
    }

    if (m_shaderLoaded) {
        if (!m_useDirectGFD) {
            WHBGfxFreeShaderGroup(&m_shaderGroup);
        } else {
            if (m_directVs) { void* p = m_directVs; MemoryManager::Instance().Free(p); m_directVs = nullptr; }
            if (m_directVsProgram) { MemoryManager::Instance().FreeAligned(m_directVsProgram); m_directVsProgram = nullptr; }
            if (m_directPs) { void* p = m_directPs; MemoryManager::Instance().Free(p); m_directPs = nullptr; }
            if (m_directPsProgram) { MemoryManager::Instance().FreeAligned(m_directPsProgram); m_directPsProgram = nullptr; }
            if (m_directFsProgram) { MemoryManager::Instance().FreeAligned(m_directFsProgram); m_directFsProgram = nullptr; }
        }
        m_shaderLoaded = false;
    }

    m_pColorBuffer = nullptr;
    m_colorBufferBound = false;
#endif
}

void SceneInteractiveGX2::BindColorBuffer(SDL_Renderer* renderer) {
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
#endif
}

void SceneInteractiveGX2::Update(float dt) {
    const auto& input = InputManager::Instance();
    const auto& touch = input.GetTouchState();

    // 1. Navegación y Botones del HUD
    if (m_backButton.Update(touch, "VOLVER MENU") || input.IsBackRequested()) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuGX2>());
        return;
    }

    if (m_colorButton.Update(touch, "MUTAR COLOR")) {
        AudioManager::Instance().PlayClick();
        MutateCubePalette();
        SpawnShockwave(m_colorButton.GetRect().x + 115.0f, m_colorButton.GetRect().y + 20.0f, ColorRGBA::Yellow());
    }

    if (m_rotateButton.Update(touch, "TOGGLE GIRO")) {
        AudioManager::Instance().PlayClick();
        m_autoRotateCube = !m_autoRotateCube;
        m_rotateButton.SetLabel(m_autoRotateCube ? "GIRO: AUTOMATICO" : "GIRO: MANUAL (STICK)");
    }

    if (m_lightButton.Update(touch, "TOGGLE LUZ")) {
        AudioManager::Instance().PlayClick();
        m_rotateLight = !m_rotateLight;
        m_lightButton.SetLabel(m_rotateLight ? "LUZ: ORBITAL 3D" : "LUZ: FIJA FRONTAL");
    }

    if (m_resetButton.Update(touch, "RESET WIDGET")) {
        AudioManager::Instance().PlayClick();
        ResetWidgetPosition();
        SpawnShockwave(640.0f, 360.0f, ColorRGBA::Cyan());
    }

    // Botón A o acción física del GamePad
    if (input.WasActionTriggeredThisFrame()) {
        AudioManager::Instance().PlayClick();
        MutateCubePalette();
        SpawnShockwave(m_widgetX + m_widgetW * 0.5f, m_widgetY + m_widgetH * 0.5f, ColorRGBA::Magenta());
    }

    // 2. Dinámica del Cubo 3D
    if (m_autoRotateCube) {
        m_cubeRotX += 45.0f * (3.14159265f / 180.0f) * dt;
        m_cubeRotY += 65.0f * (3.14159265f / 180.0f) * dt;
        m_cubeRotZ += 25.0f * (3.14159265f / 180.0f) * dt;
    }

    // Control orbital con Stick Izquierdo
    const auto& stickL = input.GetLeftStick();
    if (std::abs(stickL.x) > 0.08f || std::abs(stickL.y) > 0.08f) {
        m_cubeRotY += stickL.x * 3.5f * dt;
        m_cubeRotX -= stickL.y * 3.5f * dt;
    }

    // Dinámica de la luz orbital
    if (m_rotateLight) {
        m_lightAngle += 1.8f * dt;
        m_lightDir = Point3D(std::cos(m_lightAngle), 0.75f, std::sin(m_lightAngle)).Normalized();
    } else {
        m_lightDir = Point3D(0.2f, 0.4f, 1.0f).Normalized();
    }

    // 3. Interacción con el Widget Arrastrable
    if (touch.active) {
        if (!m_isDraggingWidget) {
            // Verificar colisión con el widget (con margen ergonómico de 16 px)
            constexpr float MARGIN = 16.0f;
            if (touch.x >= (m_widgetX - MARGIN) && touch.x <= (m_widgetX + m_widgetW + MARGIN) &&
                touch.y >= (m_widgetY - MARGIN) && touch.y <= (m_widgetY + m_widgetH + MARGIN)) {
                m_isDraggingWidget = true;
                m_isHomingToAnchor = false;
                m_dragGrabOffsetX = touch.x - m_widgetX;
                m_dragGrabOffsetY = touch.y - m_widgetY;
                m_widgetVx = 0.0f;
                m_widgetVy = 0.0f;
                SpawnShockwave(touch.x, touch.y, ColorRGBA::Cyan());
                AudioManager::Instance().PlayClick();
            } else if (touch.justPressed && touch.y > 60.0f && touch.y < 615.0f) {
                // Toque en el canvas libre genera onda de choque
                SpawnShockwave(touch.x, touch.y, ColorRGBA{0, 240, 255, 230});
            }
        } else {
            // Arrastre activo
            float newX = touch.x - m_dragGrabOffsetX;
            float newY = touch.y - m_dragGrabOffsetY;

            float dx = touch.x - m_lastTouchX;
            float dy = touch.y - m_lastTouchY;
            if (dt > 0.0001f) {
                m_widgetVx = dx / dt;
                m_widgetVy = dy / dt;
            }

            m_widgetX = newX;
            m_widgetY = newY;
        }

        m_lastTouchX = touch.x;
        m_lastTouchY = touch.y;
    } else {
        if (m_isDraggingWidget) {
            m_isDraggingWidget = false;
            // Acotar velocidad de inercia
            m_widgetVx = std::clamp(m_widgetVx, -1200.0f, 1200.0f);
            m_widgetVy = std::clamp(m_widgetVy, -1200.0f, 1200.0f);

            // [OPT-116] Detección de radio de anclaje magnético al soltar
            float distToAnchorSq = (m_widgetX - ANCHOR_X) * (m_widgetX - ANCHOR_X) +
                                   (m_widgetY - ANCHOR_Y) * (m_widgetY - ANCHOR_Y);
            if (distToAnchorSq < DOCKING_RADIUS * DOCKING_RADIUS) {
                m_isHomingToAnchor = true;
                AudioManager::Instance().PlayClick();
                SpawnShockwave(ANCHOR_X + m_widgetW * 0.5f, ANCHOR_Y + m_widgetH * 0.5f, ColorRGBA::Cyan());
            }
        }
    }

    // 4. [OPT-116] Anclaje Magnético con Resorte Amortiguado Crítico vs [OPT-86] Inercia Libre y Rebote
    if (!m_isDraggingWidget) {
        if (m_isHomingToAnchor) {
            // [OPT-116] Solución analítica exacta en forma cerrada para amortiguamiento crítico (zeta = 1.0):
            // u(t + dt) = (u(t) + (v(t) + omega0 * u(t)) * dt) * exp(-omega0 * dt)
            // v(t + dt) = (v(t) - omega0 * (v(t) + omega0 * u(t)) * dt) * exp(-omega0 * dt)
            const float uX = m_widgetX - ANCHOR_X;
            const float uY = m_widgetY - ANCHOR_Y;
            const float expFactor = std::exp(-OMEGA_0 * dt);

            const float newUx = (uX + (m_widgetVx + OMEGA_0 * uX) * dt) * expFactor;
            const float newUy = (uY + (m_widgetVy + OMEGA_0 * uY) * dt) * expFactor;

            m_widgetVx = (m_widgetVx - OMEGA_0 * (m_widgetVx + OMEGA_0 * uX) * dt) * expFactor;
            m_widgetVy = (m_widgetVy - OMEGA_0 * (m_widgetVy + OMEGA_0 * uY) * dt) * expFactor;

            m_widgetX = ANCHOR_X + newUx;
            m_widgetY = ANCHOR_Y + newUy;

            // Reposo y acoplamiento estricto en ancla al acercarse al punto cero
            if (std::abs(newUx) < 0.4f && std::abs(newUy) < 0.4f &&
                std::abs(m_widgetVx) < 1.0f && std::abs(m_widgetVy) < 1.0f) {
                m_widgetX = ANCHOR_X;
                m_widgetY = ANCHOR_Y;
                m_widgetVx = 0.0f;
                m_widgetVy = 0.0f;
                m_isHomingToAnchor = false;
            }
        } else {
            m_widgetX += m_widgetVx * dt;
            m_widgetY += m_widgetVy * dt;

            // Amortiguamiento exponencial analítico independiente de variaciones en dt (Casey Muratori / Jakobsen)
            constexpr float DAMPING_GAMMA = 3.6f;
            float decayFactor = std::exp(-DAMPING_GAMMA * dt);
            m_widgetVx *= decayFactor;
            m_widgetVy *= decayFactor;

            // Reposo numérico estricto para erradicar temblores subpíxel
            if (std::abs(m_widgetVx) < 1.5f) m_widgetVx = 0.0f;
            if (std::abs(m_widgetVy) < 1.5f) m_widgetVy = 0.0f;

            // Rebote elástico amortiguado en los límites de la pantalla (área de juego: Y 62..615)
            constexpr float RESTITUTION = 0.72f;
            if (m_widgetX < 20.0f) {
                m_widgetX = 20.0f;
                if (std::abs(m_widgetVx) > 240.0f) {
                    SpawnShockwave(20.0f, m_widgetY + m_widgetH * 0.5f, ColorRGBA{0, 255, 230, 200});
                }
                m_widgetVx = -m_widgetVx * RESTITUTION;
            } else if (m_widgetX + m_widgetW > 1260.0f) {
                m_widgetX = 1260.0f - m_widgetW;
                if (std::abs(m_widgetVx) > 240.0f) {
                    SpawnShockwave(1260.0f, m_widgetY + m_widgetH * 0.5f, ColorRGBA{0, 255, 230, 200});
                }
                m_widgetVx = -m_widgetVx * RESTITUTION;
            }

            if (m_widgetY < 62.0f) {
                m_widgetY = 62.0f;
                if (std::abs(m_widgetVy) > 240.0f) {
                    SpawnShockwave(m_widgetX + m_widgetW * 0.5f, 62.0f, ColorRGBA{0, 255, 230, 200});
                }
                m_widgetVy = -m_widgetVy * RESTITUTION;
            } else if (m_widgetY + m_widgetH > 615.0f) {
                m_widgetY = 615.0f - m_widgetH;
                if (std::abs(m_widgetVy) > 240.0f) {
                    SpawnShockwave(m_widgetX + m_widgetW * 0.5f, 615.0f, ColorRGBA{0, 255, 230, 200});
                }
                m_widgetVy = -m_widgetVy * RESTITUTION;
            }
        }
    }

    // 5. Registro de Estela de Arrastre
    m_trailTimer += dt;
    if (m_trailTimer >= 0.025f) {
        m_trailTimer = 0.0f;
        float centerX = m_widgetX + m_widgetW * 0.5f;
        float centerY = m_widgetY + m_widgetH * 0.5f;

        m_trailHistory[m_trailHead] = TrailPoint{ centerX, centerY, 0.75f };
        m_trailHead = (m_trailHead + 1) % TRAIL_POINTS;
    }

    for (auto& pt : m_trailHistory) {
        if (pt.alpha > 0.0f) {
            pt.alpha -= 1.8f * dt;
            if (pt.alpha < 0.0f) pt.alpha = 0.0f;
        }
    }

    // 6. Actualización de Ondas de Choque
    for (auto& ring : m_shockwaves) {
        if (ring.active) {
            ring.radius += 230.0f * dt;
            ring.alpha -= 1.9f * dt;
            if (ring.radius >= ring.maxRadius || ring.alpha <= 0.0f) {
                ring.active = false;
                ring.alpha = 0.0f;
            }
        }
    }
}

void SceneInteractiveGX2::RenderScene3D_GX2() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_shaderLoaded || !m_buffersCreated) return;

    if (m_nativeContextState) {
        GX2SetContextState(m_nativeContextState);
    }

    // 1. Construir Matrices MVP en Espacio 3D
    Matrix4x4 proj = Matrix4x4::Perspective(0.85f, 1280.0f / 720.0f, 0.2f, 100.0f);

    Point3D camPos{0.0f, 0.8f, m_camDistance};
    Point3D camTarget{0.0f, 0.0f, 0.0f};
    Point3D camUp{0.0f, 1.0f, 0.0f};
    Matrix4x4 view = Matrix4x4::LookAt(camPos, camTarget, camUp);

    Matrix4x4 model = Matrix4x4::Identity() *
                      Matrix4x4::RotationX(m_cubeRotX) *
                      Matrix4x4::RotationY(m_cubeRotY) *
                      Matrix4x4::RotationZ(m_cubeRotZ);

    Matrix4x4 mvp = proj * view * model;

    // 2. Subir Uniforms al Vertex Shader
    float vsUniforms[32];
    std::memcpy(&vsUniforms[0], mvp.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], model.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 32, vsUniforms);

    // 3. Subir Uniforms al Pixel Shader (Iluminación Blinn-Phong)
    float psUniforms[16];
    psUniforms[0] = m_lightDir.x;
    psUniforms[1] = m_lightDir.y;
    psUniforms[2] = m_lightDir.z;
    psUniforms[3] = 1.0f; // Activa componente difusa y especular

    psUniforms[4] = 1.0f; psUniforms[5] = 1.0f; psUniforms[6] = 0.95f; psUniforms[7] = 1.0f; // Light Color
    psUniforms[8] = camPos.x; psUniforms[9] = camPos.y; psUniforms[10] = camPos.z; psUniforms[11] = 48.0f; // Cam Pos + Specular Power
    psUniforms[12] = 0.25f; psUniforms[13] = 0.28f; psUniforms[14] = 0.35f; psUniforms[15] = 0.0f; // Luz Ambiental

    GX2SetPixelUniformReg(0, 16, psUniforms);

    // 4. Vincular Shaders y Buffers de la GPU Latte
    if (!m_useDirectGFD) {
        GX2SetFetchShader(&m_shaderGroup.fetchShader);
        GX2SetVertexShader(m_shaderGroup.vertexShader);
        GX2SetPixelShader(m_shaderGroup.pixelShader);
    } else {
        GX2SetFetchShader(&m_directFetchShader);
        GX2SetVertexShader(m_directVs);
        GX2SetPixelShader(m_directPs);
    }

    GX2RSetAttributeBuffer(&m_vertexBuffer, 0, sizeof(InteractiveGX2Vertex), 0);

    // 5. Configurar Rasterización y Z-Buffer en eDRAM con RHI Pipeline Cache
    RHIPipelineState state(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false, RHI_FRONT_FACE_CCW);
    RHIPipelineCache::Instance().BindState(state);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);

    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);

    // 6. Dibujo Indexado en Silicio (36 índices)
    GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_indexBuffer, GX2_INDEX_TYPE_U16, 36, 0, 0, 1);
    GX2Flush();
#endif
}

void SceneInteractiveGX2::RenderInteractive2D_GX2() {
    auto& r2d = Render2DGX2::Instance();

#if defined(__WIIU__) || defined(__wiiu__)
    if (m_nativeContextState && m_pColorBuffer) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    }
#endif

    r2d.Begin();

    // 1. Estela de Arrastre del Widget (Cuadrícula de fondo eliminada para lienzo limpio)
    for (size_t i = 0; i < TRAIL_POINTS; ++i) {
        const auto& pt = m_trailHistory[i];
        if (pt.alpha > 0.01f) {
            Uint8 a = static_cast<Uint8>(std::clamp(pt.alpha * 200.0f, 0.0f, 255.0f));
            float sz = 40.0f * pt.alpha;
            r2d.DrawBox(pt.x - sz * 0.5f, pt.y - sz * 0.5f, sz, sz,
                        ColorRGBA{0, 200, 255, static_cast<Uint8>(a / 3)},
                        ColorRGBA{0, 255, 255, a}, 1.5f);
        }
    }

    // 2. Base de Anclaje Magnético Estilizada [OPT-116]
    r2d.DrawBox(ANCHOR_X, ANCHOR_Y, m_widgetW, m_widgetH,
                ColorRGBA{0, 80, 160, 45}, ColorRGBA{0, 220, 255, 90}, 1.5f);

    // 3. Widget Holográfico Arrastrable
    ColorRGBA widgetFill = m_isDraggingWidget ? ColorRGBA{0, 140, 220, 190} :
                           (m_isHomingToAnchor ? ColorRGBA{0, 180, 240, 185} : ColorRGBA{10, 40, 80, 175});
    ColorRGBA widgetBorder = m_isDraggingWidget ? ColorRGBA::Yellow() :
                             (m_isHomingToAnchor ? ColorRGBA{0, 255, 180, 255} : ColorRGBA::Cyan());
    r2d.DrawBox(m_widgetX, m_widgetY, m_widgetW, m_widgetH, widgetFill, widgetBorder, 3.0f);

    // Icono / Símbolo interior del widget
    r2d.DrawText("<+>", m_widgetX + 27.0f, m_widgetY + 36.0f, 2.0f,
                 m_isDraggingWidget ? ColorRGBA::Yellow() : ColorRGBA::White());

    // 4. Ondas de Choque Táctiles
    for (const auto& ring : m_shockwaves) {
        if (ring.active && ring.alpha > 0.01f) {
            Uint8 a = static_cast<Uint8>(std::clamp(ring.alpha * 255.0f, 0.0f, 255.0f));
            ColorRGBA edgeCol = ring.color;
            edgeCol.a = a;
            ColorRGBA centerCol = ring.color;
            centerCol.a = 0;

            // Anillo exterior con gradiente radial y borde
            r2d.DrawCircle(ring.x, ring.y, ring.radius, centerCol, edgeCol, 18);
            if (ring.radius > 8.0f) {
                r2d.DrawCircle(ring.x, ring.y, ring.radius * 0.75f, centerCol, edgeCol, 14);
            }
        }
    }

    // 5. Botones Superiores del HUD
    const auto& rBack = m_backButton.GetRect();
    r2d.DrawButton(m_backButton.GetLabel(), static_cast<float>(rBack.x), static_cast<float>(rBack.y),
                   static_cast<float>(rBack.w), static_cast<float>(rBack.h), m_backButton.IsHovered(),
                   ColorRGBA{20, 30, 50, 220}, ColorRGBA{200, 40, 40, 230},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    auto drawHudButton = [&](const UIButton& btn) {
        const auto& rect = btn.GetRect();
        r2d.DrawButton(btn.GetLabel(), static_cast<float>(rect.x), static_cast<float>(rect.y),
                       static_cast<float>(rect.w), static_cast<float>(rect.h), btn.IsHovered(),
                       ColorRGBA{20, 35, 55, 230}, ColorRGBA{0, 180, 240, 240},
                       ColorRGBA{0, 210, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);
    };

    drawHudButton(m_colorButton);
    drawHudButton(m_rotateButton);
    drawHudButton(m_lightButton);
    drawHudButton(m_resetButton);

    // 6. Panel Inferior de Telemetría Estandarizada (UIToolkit - DrawHudFooter)
    StackString<256> line1;
    line1.Format("DEMO 8: SANDBOX INTERACTIVO | FPS: %.1f | PIPELINE: 100%% GX2 NATIVO LATTE | DRAW CALLS: 2",
                 m_smoothedFps);

    StackString<256> line2;
    line2.Format("WIDGET TACTIL: POS (%.0f, %.0f) | INERCIA (%.0f, %.0f) | %s | ANCLAJE MAGNETICO: ACTIVO",
                 m_widgetX, m_widgetY, m_widgetVx, m_widgetVy,
                 m_isDraggingWidget ? "ARRASTRANDO (STYLUS)" :
                 (m_isHomingToAnchor ? "ACOPLANDO A ANCLA (SPRING)" : "FLOTANDO (INERCIA)"));

    UI::UIToolkit::DrawHudFooter({
        { "[STYLUS / TOQUE]: ARRASTRAR WIDGET Y CREAR ONDAS | [STICK L]: ROTAR 3D | [B]: VOLVER", ColorRGBA{180, 220, 255, 255} },
        { line1.string_view(), ColorRGBA::Yellow() },
        { line2.string_view(), ColorRGBA::White() }
    });

    r2d.End();
}

void SceneInteractiveGX2::Render(SDL_Renderer* renderer) {
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
    if (!m_shaderLoaded || !m_buffersCreated) {
        InitGX2Pipeline();
    }
    if (!m_colorBufferBound && renderer) {
        BindColorBuffer(renderer);
    }

    if (m_pColorBuffer && m_nativeContextState) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
        if (m_depthBufferCreated) {
            GX2SetDepthBuffer(&m_depthBuffer);
        }
        GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
        GX2SetScissor(0, 0, 1280, 720);

        // Limpieza de pantalla en silicio
        GX2ClearColor(m_pColorBuffer, 0.02f, 0.04f, 0.08f, 1.0f);
        if (m_depthBufferCreated) {
            GX2ClearDepthStencilEx(&m_depthBuffer, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
        }
    }

    // Pase 1: Entidad 3D con Shaders CafeGLSL y Z-Buffer en eDRAM (1 Draw Call)
    RenderScene3D_GX2();

    // Pase 2: Widget + Estela + Ondas + HUD Completo con Render2DGX2 (1 Draw Call)
    RenderInteractive2D_GX2();
#else
    if (!renderer) return;

    // Fallback Desktop PC con SDL_Renderer
    SDL_SetRenderDrawColor(renderer, 5, 10, 20, 255);
    SDL_RenderClear(renderer);

    // Dibujar widget
    SDL_Rect wRect{ static_cast<int>(m_widgetX), static_cast<int>(m_widgetY),
                    static_cast<int>(m_widgetW), static_cast<int>(m_widgetH) };
    SDL_SetRenderDrawColor(renderer, 0, 200, 255, 255);
    SDL_RenderFillRect(renderer, &wRect);

    // Dibujar botones
    m_backButton.Render(renderer);
    m_colorButton.Render(renderer);
    m_rotateButton.Render(renderer);
    m_lightButton.Render(renderer);
    m_resetButton.Render(renderer);
#endif
}

} // namespace WiiU
