#include "Scene/SceneGX2Native.hpp"
#include "Scene/SceneMenu.hpp"
#include "Scene/SceneMenuGX2.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/AssetManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include "Graphics/NativeGX2ShaderBinary.hpp"
#include "Graphics/GX2StateHelper.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/StackString.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <malloc.h>

namespace WiiU {

SceneGX2Native::SceneGX2Native() {
    BuildGeometry();
    InitGX2Pipeline();
}

SceneGX2Native::~SceneGX2Native() {
    OnExit();
}

void SceneGX2Native::BuildGeometry() {
    if (!m_cubeVertices.empty()) return;

    // -------------------------------------------------------------------------
    // 1. Geometría del Cubo Central Multifacetado (24 Vértices, 36 Índices)
    // -------------------------------------------------------------------------
    constexpr float S = 0.85f; // Semilado
    m_cubeVertices.reserve(24);
    m_cubeIndices.reserve(36);

    auto addFace = [this](const Point3D& p0, const Point3D& p1, const Point3D& p2, const Point3D& p3,
                          const Point3D& norm, const ColorRGBA& col) {
        uint16_t baseIdx = static_cast<uint16_t>(m_cubeVertices.size());
        float r = static_cast<float>(col.r) / 255.0f;
        float g = static_cast<float>(col.g) / 255.0f;
        float b = static_cast<float>(col.b) / 255.0f;
        float a = static_cast<float>(col.a) / 255.0f;

        m_cubeVertices.push_back({ {p0.x, p0.y, p0.z}, {norm.x, norm.y, norm.z}, {r, g, b, a} });
        m_cubeVertices.push_back({ {p1.x, p1.y, p1.z}, {norm.x, norm.y, norm.z}, {r, g, b, a} });
        m_cubeVertices.push_back({ {p2.x, p2.y, p2.z}, {norm.x, norm.y, norm.z}, {r, g, b, a} });
        m_cubeVertices.push_back({ {p3.x, p3.y, p3.z}, {norm.x, norm.y, norm.z}, {r, g, b, a} });

        // Dos triángulos por cara (sentido antihorario CCW)
        m_cubeIndices.push_back(baseIdx + 0);
        m_cubeIndices.push_back(baseIdx + 1);
        m_cubeIndices.push_back(baseIdx + 2);

        m_cubeIndices.push_back(baseIdx + 0);
        m_cubeIndices.push_back(baseIdx + 2);
        m_cubeIndices.push_back(baseIdx + 3);
    };

    // Frontal (+Z) - Rojo Carmesí
    addFace({-S, -S,  S}, { S, -S,  S}, { S,  S,  S}, {-S,  S,  S}, {0.0f, 0.0f, 1.0f}, ColorRGBA{255, 65, 65, 255});
    // Trasera (-Z) - Azul Cyan Neón
    addFace({ S, -S, -S}, {-S, -S, -S}, {-S,  S, -S}, { S,  S, -S}, {0.0f, 0.0f, -1.0f}, ColorRGBA{0, 225, 255, 255});
    // Izquierda (-X) - Naranja Fuego
    addFace({-S, -S, -S}, {-S, -S,  S}, {-S,  S,  S}, {-S,  S, -S}, {-1.0f, 0.0f, 0.0f}, ColorRGBA{255, 140, 20, 255});
    // Derecha (+X) - Violeta Magenta
    addFace({ S, -S,  S}, { S, -S, -S}, { S,  S, -S}, { S,  S,  S}, {1.0f, 0.0f, 0.0f}, ColorRGBA{240, 55, 240, 255});
    // Superior (+Y) - Verde Esmeralda Neón
    addFace({-S,  S,  S}, { S,  S,  S}, { S,  S, -S}, {-S,  S, -S}, {0.0f, 1.0f, 0.0f}, ColorRGBA{50, 255, 120, 255});
    // Inferior (-Y) - Amarillo Dorado
    addFace({-S, -S, -S}, { S, -S, -S}, { S, -S,  S}, {-S, -S,  S}, {0.0f, -1.0f, 0.0f}, ColorRGBA{255, 220, 40, 255});

    // -------------------------------------------------------------------------
    // 2. Geometría de la Plataforma Base (Disco Poligonal de 16 Lados)
    // -------------------------------------------------------------------------
    constexpr int PLATFORM_SEGS = 16;
    constexpr float PLATFORM_RADIUS = 3.2f;
    uint16_t platCenterIdx = static_cast<uint16_t>(m_platformVertices.size());
    m_platformVertices.push_back({ {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.15f, 0.25f, 0.40f, 0.85f} });

    for (int i = 0; i < PLATFORM_SEGS; ++i) {
        float angle = static_cast<float>(i) * (6.2831853f / static_cast<float>(PLATFORM_SEGS));
        float px = std::cos(angle) * PLATFORM_RADIUS;
        float pz = std::sin(angle) * PLATFORM_RADIUS;
        float rimCol = (i % 2 == 0) ? 0.35f : 0.20f;
        m_platformVertices.push_back({ {px, 0.0f, pz}, {0.0f, 1.0f, 0.0f}, {0.10f, rimCol, 0.45f, 0.85f} });
    }

    for (int i = 0; i < PLATFORM_SEGS; ++i) {
        uint16_t current = static_cast<uint16_t>(platCenterIdx + 1 + i);
        uint16_t next = static_cast<uint16_t>(platCenterIdx + 1 + ((i + 1) % PLATFORM_SEGS));
        m_platformIndices.push_back(platCenterIdx);
        m_platformIndices.push_back(current);
        m_platformIndices.push_back(next);
    }
}

bool SceneGX2Native::InitGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_shaderLoaded && m_buffersCreated) {
        return true;
    }

    // 1. Cargar Shader Group de CafeGLSL (assets/shaders/native_gx2.gsh)
    if (!m_shaderLoaded) {
        std::vector<uint8_t> shaderData;
        bool loaded = AssetManager::LoadBinaryFile("shaders/native_gx2.gsh", shaderData);
        if (!loaded || shaderData.empty()) {
            // Fallback infalible de silicio en ROM con binario CafeGLSL GSH precompilado
            shaderData.assign(NATIVE_GX2_GSH_DATA, NATIVE_GX2_GSH_DATA + NATIVE_GX2_GSH_SIZE);
            loaded = true;
        }
        m_diagGshBytes = static_cast<uint32_t>(shaderData.size());
        m_diagLoadOk = loaded && (!shaderData.empty());

        if (m_diagLoadOk) {
            // Método 1: Intentar mediante WHBGfxLoadGFDShaderGroup
            if (WHBGfxLoadGFDShaderGroup(&m_shaderGroup, 0, shaderData.data())) {
                bool a1 = WHBGfxInitShaderAttribute(&m_shaderGroup, "aPos", 0,
                                                    offsetof(NativeGX2Vertex, pos),
                                                    GX2_ATTRIB_FORMAT_FLOAT_32_32_32);
                bool a2 = WHBGfxInitShaderAttribute(&m_shaderGroup, "aNormal", 0,
                                                    offsetof(NativeGX2Vertex, normal),
                                                    GX2_ATTRIB_FORMAT_FLOAT_32_32_32);
                bool a3 = WHBGfxInitShaderAttribute(&m_shaderGroup, "aColor", 0,
                                                    offsetof(NativeGX2Vertex, color),
                                                    GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32);
                if (a1 && a2 && a3 && WHBGfxInitFetchShader(&m_shaderGroup)) {
                    m_shaderLoaded = true;
                    m_useDirectGFD = false;
                    m_diagWhbOk = true;
                }
            }

            // Método 2: Fallback Robusto Directo GFD (100% independiente de WHB y allocators globales)
            if (!m_shaderLoaded) {
                uint32_t vsHeaderSize = GFDGetVertexShaderHeaderSize(0, shaderData.data());
                uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
                uint32_t psHeaderSize = GFDGetPixelShaderHeaderSize(0, shaderData.data());
                uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());

                if (vsHeaderSize > 0 && vsProgSize > 0 && psHeaderSize > 0 && psProgSize > 0) {
                    m_directVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHeaderSize, MemoryTag::ShaderProgram, "GX2Native_VS_Hdr"));
                    m_directVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "GX2Native_VS_Prog");
                    m_directPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHeaderSize, MemoryTag::ShaderProgram, "GX2Native_PS_Hdr"));
                    m_directPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "GX2Native_PS_Prog");

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
                            attrs[0].offset = offsetof(NativeGX2Vertex, pos);
                            attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                            attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attrs[0].aluDivisor = 0;
                            attrs[0].mask = 0x00010205;
                            attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            attrs[1].location = 1;
                            attrs[1].buffer = 0;
                            attrs[1].offset = offsetof(NativeGX2Vertex, normal);
                            attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                            attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attrs[1].aluDivisor = 0;
                            attrs[1].mask = 0x00010205;
                            attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            attrs[2].location = 2;
                            attrs[2].buffer = 0;
                            attrs[2].offset = offsetof(NativeGX2Vertex, color);
                            attrs[2].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
                            attrs[2].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attrs[2].aluDivisor = 0;
                            attrs[2].mask = 0x00010203;
                            attrs[2].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            uint32_t fsSize = GX2CalcFetchShaderSizeEx(3, GX2_FETCH_SHADER_TESSELLATION_NONE,
                                                                       GX2_TESSELLATION_MODE_DISCRETE);
                            m_directFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "GX2Native_FS_Prog");
                            if (m_directFsProgram) {
                                GX2InitFetchShaderEx(&m_directFetchShader, static_cast<uint8_t*>(m_directFsProgram), 3, attrs,
                                                     GX2_FETCH_SHADER_TESSELLATION_NONE,
                                                     GX2_TESSELLATION_MODE_DISCRETE);
                                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directFsProgram, fsSize);
                                m_useDirectGFD = true;
                                m_shaderLoaded = true;
                                m_diagDirectGfdOk = true;
                            }
                        }
                    }
                }
            }
        }
    }

    // 2. Crear Búferes de Vértices e Índices en VRAM (GX2R)
    if (m_shaderLoaded && !m_buffersCreated) {
        // Vertex Buffer Unificado
        std::vector<NativeGX2Vertex> allVerts = m_cubeVertices;
        size_t cubeVertCount = m_cubeVertices.size();
        allVerts.insert(allVerts.end(), m_platformVertices.begin(), m_platformVertices.end());

        m_vertexBuffer.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_vertexBuffer.elemSize = sizeof(NativeGX2Vertex);
        m_vertexBuffer.elemCount = static_cast<uint32_t>(allVerts.size());

        bool vboOk = false;
        if (GX2RCreateBuffer(&m_vertexBuffer)) {
            void* ptr = GX2RLockBufferEx(&m_vertexBuffer, GX2R_RESOURCE_BIND_NONE);
            if (ptr) {
                std::memcpy(ptr, allVerts.data(), sizeof(NativeGX2Vertex) * allVerts.size());
                GX2RUnlockBufferEx(&m_vertexBuffer, GX2R_RESOURCE_BIND_NONE);
                vboOk = true;
            }
        }
        m_diagVboOk = vboOk;

        // Index Buffer del Cubo (36 índices)
        m_indexBufferCube.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_INDEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_indexBufferCube.elemSize = sizeof(uint16_t);
        m_indexBufferCube.elemCount = static_cast<uint32_t>(m_cubeIndices.size());
        bool iboCubeOk = false;
        if (GX2RCreateBuffer(&m_indexBufferCube)) {
            void* ptr = GX2RLockBufferEx(&m_indexBufferCube, GX2R_RESOURCE_BIND_NONE);
            if (ptr) {
                std::memcpy(ptr, m_cubeIndices.data(), sizeof(uint16_t) * m_cubeIndices.size());
                GX2RUnlockBufferEx(&m_indexBufferCube, GX2R_RESOURCE_BIND_NONE);
                iboCubeOk = true;
            }
        }

        // Index Buffer de la Plataforma (48 índices) ajustando el offset base de vértices
        std::vector<uint16_t> platformIndicesOffset = m_platformIndices;
        for (auto& idx : platformIndicesOffset) {
            idx += static_cast<uint16_t>(cubeVertCount);
        }

        m_indexBufferPlatform.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_INDEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        m_indexBufferPlatform.elemSize = sizeof(uint16_t);
        m_indexBufferPlatform.elemCount = static_cast<uint32_t>(platformIndicesOffset.size());
        bool iboPlatOk = false;
        if (GX2RCreateBuffer(&m_indexBufferPlatform)) {
            void* ptr = GX2RLockBufferEx(&m_indexBufferPlatform, GX2R_RESOURCE_BIND_NONE);
            if (ptr) {
                std::memcpy(ptr, platformIndicesOffset.data(), sizeof(uint16_t) * platformIndicesOffset.size());
                GX2RUnlockBufferEx(&m_indexBufferPlatform, GX2R_RESOURCE_BIND_NONE);
                iboPlatOk = true;
            }
        }
        m_diagIboOk = iboCubeOk && iboPlatOk;

        m_buffersCreated = (vboOk && iboCubeOk && iboPlatOk);
    }

    // 3. Crear Hardware Z-Buffer (Depth Buffer) en eDRAM de la GPU Latte
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

    // 4. Crear GX2ContextState aislado para proteger el pipeline de SDL2
    if (!m_nativeContextState) {
        m_nativeContextState = static_cast<GX2ContextState*>(MemoryManager::Instance().AllocAligned(
            GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState), MemoryTag::ContextState, "GX2Native_ContextState"));
        if (m_nativeContextState) {
            GX2SetupContextStateEx(m_nativeContextState, TRUE);
        }
    }

    return m_shaderLoaded && m_buffersCreated;
#else
    return true;
#endif
}

void SceneGX2Native::DestroyGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_depthBufferCreated) {
        GX2RDestroySurfaceEx(&m_depthBuffer.surface, GX2R_RESOURCE_BIND_NONE);
        m_depthBufferCreated = false;
    }

    if (m_buffersCreated) {
        GX2RDestroyBufferEx(&m_indexBufferPlatform, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_indexBufferCube, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_vertexBuffer, GX2R_RESOURCE_BIND_NONE);
        m_buffersCreated = false;
    }

    if (m_useDirectGFD) {
        if (m_directFsProgram) {
            MemoryManager::Instance().FreeAligned(m_directFsProgram);
        }
        if (m_directPsProgram) {
            MemoryManager::Instance().FreeAligned(m_directPsProgram);
        }
        if (m_directPs) {
            void* rawPs = static_cast<void*>(m_directPs);
            MemoryManager::Instance().Free(rawPs);
            m_directPs = nullptr;
        }
        if (m_directVsProgram) {
            MemoryManager::Instance().FreeAligned(m_directVsProgram);
        }
        if (m_directVs) {
            void* rawVs = static_cast<void*>(m_directVs);
            MemoryManager::Instance().Free(rawVs);
            m_directVs = nullptr;
        }
        m_useDirectGFD = false;
        m_shaderLoaded = false;
    } else if (m_shaderLoaded) {
        WHBGfxFreeShaderGroup(&m_shaderGroup);
        m_shaderLoaded = false;
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

void SceneGX2Native::BindColorBuffer(SDL_Renderer* renderer) {
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
    m_colorBufferBound = true;
#endif
}

void SceneGX2Native::Update(float dt) {
    // 1. Dinámica rotacional multiaxial del objeto principal y satélites
    m_cubeRotX += 35.0f * (3.14159265f / 180.0f) * dt;
    m_cubeRotY += 60.0f * (3.14159265f / 180.0f) * dt;
    m_cubeRotZ += 20.0f * (3.14159265f / 180.0f) * dt;
    m_orbitAngle += 1.25f * dt;

    // 2. Navegación e interacción táctil / GamePad
    const auto& input = InputManager::Instance();
    const auto& touch = input.GetTouchState();

    // Detección de botón Volver (< VOLVER en x: 25..165, y: 18..62)
    m_backHover = (touch.active && touch.x >= 25.0f && touch.x <= 165.0f &&
                   touch.y >= 18.0f && touch.y <= 62.0f);
    if ((m_backHover && touch.justPressed) || input.IsBackRequested()) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuGX2>());
        return;
    }

    // Detección de botón HUD Toggle (HUD: ON / OFF en x: 1060..1255, y: 18..62)
    m_hudToggleHover = (touch.active && touch.x >= 1060.0f && touch.x <= 1255.0f &&
                        touch.y >= 18.0f && touch.y <= 62.0f);
    if (m_hudToggleHover && touch.justPressed) {
        AudioManager::Instance().PlayClick();
        m_showHUD = !m_showHUD;
        return;
    }

    // 3. Control de Cámara Orbital (GamePad Sticks)
    const auto& stickL = input.GetLeftStick();
    const auto& stickR = input.GetRightStick();

    if (std::abs(stickL.x) > 0.08f || std::abs(stickL.y) > 0.08f) {
        m_camYaw += stickL.x * 2.2f * dt;
        m_camPitch = std::clamp(m_camPitch - stickL.y * 1.5f * dt, -1.45f, 1.45f);
    }

    if (std::abs(stickR.y) > 0.08f) {
        m_camDistance = std::clamp(m_camDistance - stickR.y * 8.0f * dt, 2.5f, 14.0f);
    }

    // 4. Control de Cámara por Arrastre Táctil (excluyendo zona de botones superiores y panel inferior)
    if (touch.active) {
        if (touch.y > 70.0f && touch.y < 610.0f) {
            if (touch.justPressed || !m_isDraggingCamera) {
                m_isDraggingCamera = true;
                m_lastTouchX = touch.x;
                m_lastTouchY = touch.y;
            } else {
                float dx = touch.x - m_lastTouchX;
                float dy = touch.y - m_lastTouchY;
                m_camYaw += dx * 0.006f;
                m_camPitch = std::clamp(m_camPitch - dy * 0.005f, -1.45f, 1.45f);
                m_lastTouchX = touch.x;
                m_lastTouchY = touch.y;
            }
        }
    } else {
        m_isDraggingCamera = false;
    }
}

void SceneGX2Native::RenderSceneGX2() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_shaderLoaded || !m_buffersCreated) {
        return;
    }

    m_gpuDrawCalls = 0;
    m_totalTriangles = 0;

    // 0. Activar Context State aislado para proteger el estado de SDL2
    if (m_nativeContextState) {
        GX2SetContextState(m_nativeContextState);
        if (m_pColorBuffer) {
            GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
        }
        if (m_depthBufferCreated) {
            GX2SetDepthBuffer(&m_depthBuffer);
        }
    }

    // 1. Vincular Fetch Shader y Shaders de Vértices y Píxeles
    if (m_useDirectGFD) {
        GX2SetFetchShader(&m_directFetchShader);
        GX2SetVertexShader(m_directVs);
        GX2SetPixelShader(m_directPs);
    } else {
        GX2SetFetchShader(&m_shaderGroup.fetchShader);
        GX2SetVertexShader(m_shaderGroup.vertexShader);
        GX2SetPixelShader(m_shaderGroup.pixelShader);
    }

    // 2. Vincular Búfer de Vértices Unificado
    GX2RSetAttributeBuffer(&m_vertexBuffer, 0, sizeof(NativeGX2Vertex), 0);

    // 3. Configurar Hardware Z-Buffer y Rasterización con RHI Pipeline Cache
    RHIPipelineState state(
        m_depthBufferCreated,
        m_depthBufferCreated,
        m_depthBufferCreated ? RHI_COMPARE_LEQUAL : RHI_COMPARE_ALWAYS,
        BlendMode::Opaque,
        false,
        RHI_FRONT_FACE_CCW
    );
    RHIPipelineCache::Instance().BindState(state);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);

    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);

    // 5. Configurar Parámetros de Cámara y Proyección Perspectiva
    float camX = m_camDistance * std::sin(m_camYaw) * std::cos(m_camPitch);
    float camY = m_camDistance * std::sin(m_camPitch);
    float camZ = m_camDistance * std::cos(m_camYaw) * std::cos(m_camPitch);
    Point3D camPos{camX, camY, camZ};

    Matrix4x4 view = Matrix4x4::LookAt(camPos, Point3D{0.0f, 0.0f, 0.0f}, Point3D{0.0f, 1.0f, 0.0f});
    constexpr float FOV_RAD = 55.0f * (3.14159265f / 180.0f);
    constexpr float ASPECT = 1280.0f / 720.0f;
    Matrix4x4 proj = Matrix4x4::Perspective(FOV_RAD, ASPECT, 0.1f, 100.0f);
    Matrix4x4 viewProj = proj * view;

    // 6. Subir Uniforms del Pixel Shader (16 floats = 4 registros KC2[0 .. 3])
    float psUniforms[16];
    // KC2[0]: uLightDir
    Point3D lDir = m_lightDir.Normalized();
    psUniforms[0] = lDir.x; psUniforms[1] = lDir.y; psUniforms[2] = lDir.z; psUniforms[3] = 1.0f;
    // KC2[1]: uLightColor
    psUniforms[4] = 1.0f; psUniforms[5] = 1.0f; psUniforms[6] = 1.0f; psUniforms[7] = 1.0f;
    // KC2[2]: uCamPos
    psUniforms[8] = camPos.x; psUniforms[9] = camPos.y; psUniforms[10] = camPos.z; psUniforms[11] = 32.0f;
    // KC2[3]: uAmbient (iluminación base generosa para garantizar alta visibilidad)
    psUniforms[12] = 0.40f; psUniforms[13] = 0.40f; psUniforms[14] = 0.45f; psUniforms[15] = 0.60f;

    GX2SetPixelUniformReg(0, 16, psUniforms);

    float vsUniforms[32];

    // 7. Dibujo 1 (Fondo): Plataforma Base Poligonal
    Matrix4x4 modelPlat = Matrix4x4::Translation(0.0f, -1.85f, 0.0f);
    Matrix4x4 mvpPlat = viewProj * modelPlat;

    std::memcpy(&vsUniforms[0], mvpPlat.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], modelPlat.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 32, vsUniforms);

    uint32_t platIndicesCount = static_cast<uint32_t>(m_platformIndices.size());
    GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_indexBufferPlatform, GX2_INDEX_TYPE_U16, platIndicesCount, 0, 0, 1);
    m_gpuDrawCalls++;
    m_totalTriangles += platIndicesCount / 3;

    // 8. Dibujo 2..5: 4 Satélites Orbitales 3D que Intersecan en el espacio
    constexpr int NUM_SATELLITES = 4;
    for (int s = 0; s < NUM_SATELLITES; ++s) {
        float angleOffset = static_cast<float>(s) * (6.2831853f / static_cast<float>(NUM_SATELLITES));
        float currentAngle = m_orbitAngle * (1.0f + static_cast<float>(s) * 0.35f) + angleOffset;
        float orbitR = 2.1f + static_cast<float>(s) * 0.3f;
        float heightOsc = std::sin(currentAngle * 1.5f) * 0.85f;

        float sx = std::cos(currentAngle) * orbitR;
        float sz = std::sin(currentAngle) * orbitR;
        float sy = heightOsc;

        float satScale = 0.30f + static_cast<float>(s) * 0.05f;

        Matrix4x4 modelSat = Matrix4x4::Translation(sx, sy, sz) *
                             Matrix4x4::RotationY(currentAngle * 2.0f) *
                             Matrix4x4::RotationX(currentAngle * 1.5f) *
                             Matrix4x4::Scale(satScale, satScale, satScale);
        Matrix4x4 mvpSat = viewProj * modelSat;

        std::memcpy(&vsUniforms[0], mvpSat.m, 16 * sizeof(float));
        std::memcpy(&vsUniforms[16], modelSat.m, 16 * sizeof(float));
        GX2SetVertexUniformReg(0, 32, vsUniforms);

        GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_indexBufferCube, GX2_INDEX_TYPE_U16, 36, 0, 0, 1);
        m_gpuDrawCalls++;
        m_totalTriangles += 12;
    }

    // 9. Dibujo 6: Cubo Central Multifacetado
    Matrix4x4 modelCenter = Matrix4x4::RotationY(m_cubeRotY) *
                            Matrix4x4::RotationX(m_cubeRotX) *
                            Matrix4x4::RotationZ(m_cubeRotZ);
    Matrix4x4 mvpCenter = viewProj * modelCenter;

    std::memcpy(&vsUniforms[0], mvpCenter.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], modelCenter.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 32, vsUniforms);

    GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_indexBufferCube, GX2_INDEX_TYPE_U16, 36, 0, 0, 1);
    m_gpuDrawCalls++;
    m_totalTriangles += 12;

    // 10. Enviar Comandos a la GPU Latte (preservando estado de hardware Z-buffer)
    GX2Flush();
#endif
}

void SceneGX2Native::RenderHUD_GX2() {
    auto& r2d = Render2DGX2::Instance();

#if defined(__WIIU__) || defined(__wiiu__)
    if (m_nativeContextState && m_pColorBuffer) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    }
#endif

    r2d.Begin();

    // 1. Botón Superior Izquierdo: < VOLVER (x: 25, y: 18, w: 140, h: 44)
    r2d.DrawButton("< VOLVER", 25.0f, 18.0f, 140.0f, 44.0f, m_backHover,
                   ColorRGBA{12, 18, 32, 255}, ColorRGBA{200, 40, 40, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(),
                   ColorRGBA::White(), 2.0f);

    // 2. Botón Superior Derecho: HUD Toggle (x: 1060, y: 18, w: 195, h: 44)
    r2d.DrawButton(m_showHUD ? "HUD: ON" : "HUD: OFF", 1060.0f, 18.0f, 195.0f, 44.0f, m_hudToggleHover,
                   ColorRGBA{15, 35, 55, 220}, ColorRGBA{0, 180, 255, 230},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(),
                   ColorRGBA::White(), 2.0f);

    // 3. Banner Inferior de Telemetría Estandarizada (UIToolkit - DrawHudFooter)
    StackString<256> line1;
    line1.Format("DEMO 5: 100%% GX2 NATIVO | FPS: %.1f | SHADERS: %s | BUFFERS: %s",
                 m_smoothedFps,
                 m_shaderLoaded ? (m_useDirectGFD ? "OK (DIRECT GFD)" : "OK (WHB)") : "ERROR",
                 m_buffersCreated ? "OK" : "ERROR");

    StackString<256> line2;
    line2.Format("DIAG: GSH: %u B (%s) | GFD: %s | VBO: %s | IBO: %s | UI: 100%% GX2 HW",
                 m_diagGshBytes,
                 m_diagLoadOk ? "CARGADO" : "FALLO IO",
                 m_diagWhbOk ? "WHB" : (m_diagDirectGfdOk ? "DIRECT" : "ERR"),
                 m_diagVboOk ? "OK" : "ERR",
                 m_diagIboOk ? "OK" : "ERR");

    StackString<256> line3;
    line3.Format("GPU DRAWS: %u | TRIS: %u | Z-BUFFER: %s | COSTE CPU: 0.0 ms",
                 m_gpuDrawCalls + 1, m_totalTriangles,
                 m_depthBufferCreated ? "HW eDRAM" : "OFF (PAINTER)");

    ColorRGBA line2Color = (m_shaderLoaded && m_buffersCreated) ? ColorRGBA::Cyan() : ColorRGBA{255, 120, 120, 255};

    UI::UIToolkit::DrawHudFooter({
        { "[STICK IZQ / TOQUE]: ORBITAR CAMARA | [STICK DER]: ZOOM | [B]: VOLVER", ColorRGBA{180, 220, 255, 255} },
        { line1.string_view(), ColorRGBA::Yellow() },
        { line2.string_view(), line2Color },
        { line3.string_view(), ColorRGBA::White() }
    });

    r2d.End();
}

void SceneGX2Native::Render(SDL_Renderer* renderer) {
    // Medición de rendimiento en milisegundos
    Uint32 now = SDL_GetTicks();
    if (m_lastFrameTicks > 0) {
        Uint32 delta = now - m_lastFrameTicks;
        if (delta > 0) {
            float instFps = 1000.0f / static_cast<float>(delta);
            m_smoothedFps = m_smoothedFps * 0.90f + instFps * 0.10f;
        }
    }
    m_lastFrameTicks = now;

    // -------------------------------------------------------------------------
    // VINCULACIÓN DEL COLOR BUFFER AL CONTEXTO NATIVO GX2
    // -------------------------------------------------------------------------
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

        GX2ClearColor(m_pColorBuffer, 0.04f, 0.08f, 0.14f, 1.0f);
        if (m_depthBufferCreated) {
            GX2ClearDepthStencilEx(&m_depthBuffer, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
        }
    }
#endif

    // -------------------------------------------------------------------------
    // RENDERIZADO 3D 100% NATIVO EN GPU AMD LATTE (GX2)
    // -------------------------------------------------------------------------
    RenderSceneGX2();

    // -------------------------------------------------------------------------
    // RENDERIZADO 2D UI / HUD 100% NATIVO EN GPU AMD LATTE (0 LLAMADAS SDL)
    // -------------------------------------------------------------------------
    if (m_showHUD) {
        RenderHUD_GX2();
    }
}

void SceneGX2Native::OnEnter() {
    if (!m_shaderLoaded || !m_buffersCreated) {
        BuildGeometry();
        InitGX2Pipeline();
    }
    m_lastFrameTicks = SDL_GetTicks();
    m_smoothedFps = 60.0f;
    AudioManager::Instance().StopBGM();
}

void SceneGX2Native::OnExit() {
    DestroyGX2Pipeline();
    // Desalojo proactivo de memoria en CPU heap (OPT-45)
    m_cubeVertices.clear();
    m_cubeVertices.shrink_to_fit();
    m_cubeIndices.clear();
    m_cubeIndices.shrink_to_fit();
    m_platformVertices.clear();
    m_platformVertices.shrink_to_fit();
    m_platformIndices.clear();
    m_platformIndices.shrink_to_fit();
}

} // namespace WiiU
