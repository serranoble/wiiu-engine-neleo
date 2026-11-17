#include "Scene/SceneEnvironmentGX2.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneMenuGX2.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/StackString.hpp"
#include "Graphics/EnvironmentShaderBinary.hpp"
#include "Graphics/GX2StateHelper.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/event.h>
#include <proc_ui/procui.h>
#endif

#include <cmath>
#include <cstring>
#include <algorithm>

namespace WiiU {

// Paletas de Iluminación Solar según la Hora del Día
struct SunPreset {
    const char* name;
    Point3D dir;      // Dirección hacia el sol
    ColorRGBA color;  // Tinte solar
    float glow;       // Intensidad del glow
};

static const std::array<SunPreset, 3> SUN_PRESETS = {{
    { "MANANA",    Point3D{0.75f, 0.45f, -0.48f}.Normalized(), ColorRGBA{255, 210, 140, 255}, 1.25f },
    { "MEDIODIA",  Point3D{0.25f, 0.92f, -0.30f}.Normalized(), ColorRGBA{255, 245, 220, 255}, 1.00f },
    { "ATARDECER", Point3D{0.88f, 0.22f, 0.42f}.Normalized(),  ColorRGBA{255, 145,  75, 255}, 1.45f }
}};

// Cobertura de Nubes Procedurales
static const std::array<float, 3> CLOUD_COVERAGES = { 0.28f, 0.58f, 0.85f };
static const std::array<const char*, 3> CLOUD_NAMES = { "DESPEJADO", "PARCIAL", "CUBIERTO" };

// Velocidad del Viento
static const std::array<float, 3> WIND_SPEEDS = { 0.4f, 1.2f, 2.5f };
static const std::array<const char*, 3> WIND_NAMES = { "CALMA", "BRISA", "FUERTE" };

SceneEnvironmentGX2::SceneEnvironmentGX2() {
}

SceneEnvironmentGX2::~SceneEnvironmentGX2() {
    OnExit();
}

void SceneEnvironmentGX2::AddVertex(ArenaVector<EnvironmentVertex>& verts,
                                    float px, float py, float pz,
                                    float nx, float ny, float nz,
                                    float u, float v,
                                    const ColorRGBA& col) {
    EnvironmentVertex vtx;
    vtx.pos[0] = px; vtx.pos[1] = py; vtx.pos[2] = pz;
    vtx.normal[0] = nx; vtx.normal[1] = ny; vtx.normal[2] = nz;
    vtx.texCoord[0] = u; vtx.texCoord[1] = v;
    vtx.color[0] = static_cast<float>(col.r) / 255.0f;
    vtx.color[1] = static_cast<float>(col.g) / 255.0f;
    vtx.color[2] = static_cast<float>(col.b) / 255.0f;
    vtx.color[3] = static_cast<float>(col.a) / 255.0f;
    verts.push_back(vtx);
}

void SceneEnvironmentGX2::BuildTerrainMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices) {
    verts.clear();
    indices.clear();

    constexpr int GRID_STEPS = 32;
    constexpr float EXTENT = 12.0f;
    constexpr float STEP = (EXTENT * 2.0f) / static_cast<float>(GRID_STEPS - 1);

    auto evalHeight = [](float x, float z) -> float {
        float h1 = 0.70f * std::sin(0.32f * x) * std::cos(0.32f * z);
        float h2 = 0.35f * std::sin(0.65f * x + 0.40f * z);
        // Cuenca natural de la laguna en la esquina (+7.0, +7.0)
        float dx = x - 7.0f;
        float dz = z - 7.0f;
        float d2 = dx * dx + dz * dz;
        float basin = 1.35f * std::exp(-d2 / 16.0f);
        return h1 + h2 - basin;
    };

    // 1. Generar Vértices
    for (int iz = 0; iz < GRID_STEPS; ++iz) {
        float z = -EXTENT + static_cast<float>(iz) * STEP;
        for (int ix = 0; ix < GRID_STEPS; ++ix) {
            float x = -EXTENT + static_cast<float>(ix) * STEP;
            float y = evalHeight(x, z);

            // Normal analítica mediante diferencias finitas
            constexpr float EPS = 0.08f;
            float hL = evalHeight(x - EPS, z);
            float hR = evalHeight(x + EPS, z);
            float hD = evalHeight(x, z - EPS);
            float hU = evalHeight(x, z + EPS);
            Point3D n{-(hR - hL) / (2.0f * EPS), 1.0f, -(hU - hD) / (2.0f * EPS)};
            n = n.Normalized();

            // Mapeo UV para textura de pasto en mosaico suave
            float u = (x + EXTENT) * 0.25f;
            float v = (z + EXTENT) * 0.25f;

            // Color del vértice: pasto en la superficie, grava/arena de río en el lecho sumergido
            ColorRGBA vCol = ColorRGBA::White();
            float dx = x - 7.0f;
            float dz = z - 7.0f;
            float dLagoon2 = dx * dx + dz * dz;
            if (dLagoon2 < 20.0f && y < -0.35f) {
                // Cuenca sumergida bajo el agua: tinte de arena/guijarros oscuros de lecho de río
                float depth = std::clamp((-0.35f - y) / 1.0f, 0.0f, 1.0f);
                uint8_t rBed = static_cast<uint8_t>(155.0f - depth * 55.0f);
                uint8_t gBed = static_cast<uint8_t>(140.0f - depth * 50.0f);
                uint8_t bBed = static_cast<uint8_t>(110.0f - depth * 40.0f);
                vCol = ColorRGBA{rBed, gBed, bBed, 255};
            }

            AddVertex(verts, x, y, z, n.x, n.y, n.z, u, v, vCol);
        }
    }

    // 2. Generar Índices en recorrido serpentino (OPT-111)
    for (int iz = 0; iz < GRID_STEPS - 1; ++iz) {
        for (int step = 0; step < GRID_STEPS - 1; ++step) {
            int ix = (iz & 1) ? (GRID_STEPS - 2 - step) : step;
            uint16_t v0 = static_cast<uint16_t>(iz * GRID_STEPS + ix);
            uint16_t v1 = static_cast<uint16_t>(v0 + 1);
            uint16_t v2 = static_cast<uint16_t>((iz + 1) * GRID_STEPS + ix);
            uint16_t v3 = static_cast<uint16_t>(v2 + 1);

            indices.push_back(v0);
            indices.push_back(v2);
            indices.push_back(v1);

            indices.push_back(v1);
            indices.push_back(v2);
            indices.push_back(v3);
        }
    }
}

void SceneEnvironmentGX2::BuildWallMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices) {
    verts.clear();
    indices.clear();

    // Muralla de 1 metro de alto con textura de ladrillos
    // Dimensiones: Largo = 5.0m, Ancho = 0.8m, Alto = exactamente 1.0m
    constexpr float x0 = -6.0f, x1 = -1.0f;
    constexpr float z0 = -3.2f, z1 = -2.4f;
    constexpr float yBase = 0.45f;
    constexpr float yTop = yBase + 1.00f; // Exactamente 1.0 metro de altura
    constexpr ColorRGBA wallCol = ColorRGBA::White();

    auto addQuad = [this, &verts, &indices, &wallCol](float px0, float py0, float pz0,
                          float px1, float py1, float pz1,
                          float px2, float py2, float pz2,
                          float px3, float py3, float pz3,
                          float nx, float ny, float nz,
                          float uMax, float vMax) {
        uint16_t base = static_cast<uint16_t>(verts.size());
        AddVertex(verts, px0, py0, pz0, nx, ny, nz, 0.0f, 0.0f, wallCol);
        AddVertex(verts, px1, py1, pz1, nx, ny, nz, uMax, 0.0f, wallCol);
        AddVertex(verts, px2, py2, pz2, nx, ny, nz, uMax, vMax, wallCol);
        AddVertex(verts, px3, py3, pz3, nx, ny, nz, 0.0f, vMax, wallCol);

        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    };

    // Cara Frontal (+Z)
    addQuad(x0, yBase, z1,  x1, yBase, z1,  x1, yTop, z1,  x0, yTop, z1,  0.0f, 0.0f, 1.0f, 4.0f, 1.0f);
    // Cara Trasera (-Z)
    addQuad(x1, yBase, z0,  x0, yBase, z0,  x0, yTop, z0,  x1, yTop, z0,  0.0f, 0.0f, -1.0f, 4.0f, 1.0f);
    // Cara Izquierda (-X)
    addQuad(x0, yBase, z0,  x0, yBase, z1,  x0, yTop, z1,  x0, yTop, z0, -1.0f, 0.0f, 0.0f, 0.8f, 1.0f);
    // Cara Derecha (+X)
    addQuad(x1, yBase, z1,  x1, yBase, z0,  x1, yTop, z0,  x1, yTop, z1,  1.0f, 0.0f, 0.0f, 0.8f, 1.0f);
    // Cara Superior (+Y)
    addQuad(x0, yTop, z1,  x1, yTop, z1,  x1, yTop, z0,  x0, yTop, z0,  0.0f, 1.0f, 0.0f, 4.0f, 0.8f);
}

void SceneEnvironmentGX2::BuildRockMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices) {
    verts.clear();
    indices.clear();

    // Bloque de roca facetado irregular
    // Centro: (-3.5, 0.65, 3.8), Escala: (1.6 x 1.2 x 1.4)
    constexpr float cx = -3.5f, cy = 0.65f, cz = 3.8f;
    constexpr ColorRGBA rockCol = ColorRGBA::White();

    // Vértices de poliedro perturbado
    const Point3D pts[10] = {
        Point3D{cx - 0.8f, cy - 0.5f, cz - 0.7f},
        Point3D{cx + 0.85f, cy - 0.5f, cz - 0.6f},
        Point3D{cx + 0.75f, cy - 0.5f, cz + 0.75f},
        Point3D{cx - 0.75f, cy - 0.5f, cz + 0.65f},
        Point3D{cx - 0.65f, cy + 0.55f, cz - 0.45f},
        Point3D{cx + 0.6f, cy + 0.62f, cz - 0.4f},
        Point3D{cx + 0.5f, cy + 0.58f, cz + 0.5f},
        Point3D{cx - 0.55f, cy + 0.52f, cz + 0.45f},
        Point3D{cx - 0.1f, cy + 0.75f, cz - 0.05f}, // Cima 1
        Point3D{cx + 0.15f, cy + 0.72f, cz + 0.1f}  // Cima 2
    };

    auto addRockFace = [this, &verts, &indices, &rockCol](const Point3D& p0, const Point3D& p1, const Point3D& p2) {
        Point3D e1 = p1 - p0;
        Point3D e2 = p2 - p0;
        Point3D n = Point3D::Cross(e1, e2).Normalized();

        uint16_t base = static_cast<uint16_t>(verts.size());
        AddVertex(verts, p0.x, p0.y, p0.z, n.x, n.y, n.z, 0.0f, 0.0f, rockCol);
        AddVertex(verts, p1.x, p1.y, p1.z, n.x, n.y, n.z, 1.0f, 0.0f, rockCol);
        AddVertex(verts, p2.x, p2.y, p2.z, n.x, n.y, n.z, 0.5f, 1.0f, rockCol);

        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    };

    // Facetas laterales
    addRockFace(pts[0], pts[1], pts[5]); addRockFace(pts[0], pts[5], pts[4]);
    addRockFace(pts[1], pts[2], pts[6]); addRockFace(pts[1], pts[6], pts[5]);
    addRockFace(pts[2], pts[3], pts[7]); addRockFace(pts[2], pts[7], pts[6]);
    addRockFace(pts[3], pts[0], pts[4]); addRockFace(pts[3], pts[4], pts[7]);
    // Cima facetada
    addRockFace(pts[4], pts[5], pts[8]);
    addRockFace(pts[5], pts[6], pts[9]); addRockFace(pts[5], pts[9], pts[8]);
    addRockFace(pts[6], pts[7], pts[9]);
    addRockFace(pts[7], pts[4], pts[8]); addRockFace(pts[7], pts[8], pts[9]);

    // Segunda roca: Bloque fluvial parcialmente sumergido dentro de la laguna
    // Centro: (6.2, -0.45, 6.4), descansando en el lecho a y = -1.10 y emergiendo hasta y = +0.32
    constexpr float cx2 = 6.2f, cy2 = -0.45f, cz2 = 6.4f;
    const Point3D pts2[8] = {
        Point3D{cx2 - 0.70f, cy2 - 0.65f, cz2 - 0.60f}, // Base sumergida en el lecho fluvial (y ~ -1.10)
        Point3D{cx2 + 0.65f, cy2 - 0.65f, cz2 - 0.55f},
        Point3D{cx2 + 0.60f, cy2 - 0.65f, cz2 + 0.65f},
        Point3D{cx2 - 0.65f, cy2 - 0.65f, cz2 + 0.60f},
        Point3D{cx2 - 0.45f, cy2 + 0.15f, cz2 - 0.35f}, // Cintura intermedia a nivel de agua (y ~ -0.30)
        Point3D{cx2 + 0.40f, cy2 + 0.20f, cz2 - 0.30f},
        Point3D{cx2 + 0.35f, cy2 + 0.15f, cz2 + 0.40f},
        Point3D{cx2 - 0.40f, cy2 + 0.12f, cz2 + 0.35f}
    };
    Point3D topPeak{cx2 + 0.05f, cy2 + 0.77f, cz2 + 0.05f}; // Cima que emerge al aire seco (y = +0.32)

    // Base y laterales sumergidos
    addRockFace(pts2[0], pts2[1], pts2[5]); addRockFace(pts2[0], pts2[5], pts2[4]);
    addRockFace(pts2[1], pts2[2], pts2[6]); addRockFace(pts2[1], pts2[6], pts2[5]);
    addRockFace(pts2[2], pts2[3], pts2[7]); addRockFace(pts2[2], pts2[7], pts2[6]);
    addRockFace(pts2[3], pts2[0], pts2[4]); addRockFace(pts2[3], pts2[4], pts2[7]);
    // Cima que emerge por encima del agua
    addRockFace(pts2[4], pts2[5], topPeak);
    addRockFace(pts2[5], pts2[6], topPeak);
    addRockFace(pts2[6], pts2[7], topPeak);
    addRockFace(pts2[7], pts2[4], topPeak);
}

void SceneEnvironmentGX2::BuildLagoonMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices) {
    verts.clear();
    indices.clear();

    // Superficie plana de agua en la cuenca de la esquina (+7.0, +7.0)
    constexpr int SUBDIV = 16;
    constexpr float rWater = 4.2f;
    constexpr float cx = 7.0f, cz = 7.0f;
    constexpr float yWater = -0.35f;
    constexpr ColorRGBA waterCol{220, 240, 255, 200};

    for (int iz = 0; iz <= SUBDIV; ++iz) {
        float fz = static_cast<float>(iz) / static_cast<float>(SUBDIV);
        float z = (cz - rWater) + fz * (2.0f * rWater);
        for (int ix = 0; ix <= SUBDIV; ++ix) {
            float fx = static_cast<float>(ix) / static_cast<float>(SUBDIV);
            float x = (cx - rWater) + fx * (2.0f * rWater);

            float u = fx * 2.0f;
            float v = fz * 2.0f;
            AddVertex(verts, x, yWater, z, 0.0f, 1.0f, 0.0f, u, v, waterCol);
        }
    }

    for (int iz = 0; iz < SUBDIV; ++iz) {
        for (int ix = 0; ix < SUBDIV; ++ix) {
            uint16_t v0 = static_cast<uint16_t>(iz * (SUBDIV + 1) + ix);
            uint16_t v1 = static_cast<uint16_t>(v0 + 1);
            uint16_t v2 = static_cast<uint16_t>((iz + 1) * (SUBDIV + 1) + ix);
            uint16_t v3 = static_cast<uint16_t>(v2 + 1);

            indices.push_back(v0);
            indices.push_back(v2);
            indices.push_back(v1);

            indices.push_back(v1);
            indices.push_back(v2);
            indices.push_back(v3);
        }
    }
}

void SceneEnvironmentGX2::BuildSkyDomeMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices) {
    verts.clear();
    indices.clear();

    // Bóveda celeste envolvente extendida bajo el horizonte (R = 55.0m)
    constexpr float R = 55.0f;
    constexpr int SLICES = 20;
    constexpr int STACKS = 12;
    constexpr ColorRGBA skyCol = ColorRGBA::White();

    for (int i = 0; i <= STACKS; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(STACKS);
        // Esfera celeste envolvente completa (-90 grados nadir a +90 grados cenit)
        float phi = -1.5707963f + t * 3.14159265f;
        float py = R * std::sin(phi);
        float rLevel = R * std::cos(phi);

        for (int j = 0; j <= SLICES; ++j) {
            float theta = (static_cast<float>(j) / static_cast<float>(SLICES)) * (2.0f * 3.14159265f);
            float px = rLevel * std::cos(theta);
            float pz = rLevel * std::sin(theta);

            // Normales invertidas hacia el interior
            Point3D n{-px / R, -py / R, -pz / R};
            float u = static_cast<float>(j) / static_cast<float>(SLICES);
            float v = t;

            AddVertex(verts, px, py, pz, n.x, n.y, n.z, u, v, skyCol);
        }
    }

    for (int i = 0; i < STACKS; ++i) {
        for (int j = 0; j < SLICES; ++j) {
            uint16_t row1 = static_cast<uint16_t>(i * (SLICES + 1) + j);
            uint16_t row2 = static_cast<uint16_t>((i + 1) * (SLICES + 1) + j);

            // Orden CCW invertido para ver caras interiores
            indices.push_back(row1);
            indices.push_back(row1 + 1);
            indices.push_back(row2);

            indices.push_back(row1 + 1);
            indices.push_back(row2 + 1);
            indices.push_back(row2);
        }
    }
}

void SceneEnvironmentGX2::BuildSunOrbMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices) {
    verts.clear();
    indices.clear();

    constexpr float ORB_RADIUS = 1.25f;
    constexpr int SLICES = 12;
    constexpr int STACKS = 8;
    constexpr ColorRGBA orbCol{255, 245, 200, 255};

    for (int i = 0; i <= STACKS; ++i) {
        float phi = (static_cast<float>(i) / static_cast<float>(STACKS)) * 3.14159265f;
        float ny = std::cos(phi);
        float rStack = std::sin(phi);

        for (int j = 0; j <= SLICES; ++j) {
            float theta = (static_cast<float>(j) / static_cast<float>(SLICES)) * (2.0f * 3.14159265f);
            float nx = rStack * std::cos(theta);
            float nz = rStack * std::sin(theta);

            AddVertex(verts,
                      nx * ORB_RADIUS, ny * ORB_RADIUS, nz * ORB_RADIUS,
                      nx, ny, nz, 0.0f, 0.0f, orbCol);
        }
    }

    for (int i = 0; i < STACKS; ++i) {
        for (int j = 0; j < SLICES; ++j) {
            uint16_t row1 = static_cast<uint16_t>(i * (SLICES + 1) + j);
            uint16_t row2 = static_cast<uint16_t>((i + 1) * (SLICES + 1) + j);

            indices.push_back(row1);
            indices.push_back(row2);
            indices.push_back(row1 + 1);

            indices.push_back(row1 + 1);
            indices.push_back(row2);
            indices.push_back(row2 + 1);
        }
    }
}

void SceneEnvironmentGX2::BuildPlayerMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices) {
    verts.clear();
    indices.clear();

    // Modelo tridimensional estilizado de avatar/explorador cinemático (altura total 1.75m, base en y = 0.0)
    // Paleta: Azul Cobalto de exploración espacial con acentos dorados y visor cian luminoso
    constexpr ColorRGBA torsoCol{45, 110, 225, 255};
    constexpr ColorRGBA goldCol{245, 195, 40, 255};
    constexpr ColorRGBA legCol{55, 60, 70, 255};
    constexpr ColorRGBA helmetCol{30, 40, 60, 255};
    constexpr ColorRGBA visorCol{30, 245, 240, 255}; // Cian fluorescente brillante

    auto addPrism = [this, &verts, &indices](float y0, float y1, float r0, float r1, int sides, const ColorRGBA& col) {
        uint16_t baseIdx = static_cast<uint16_t>(verts.size());
        for (int i = 0; i <= sides; ++i) {
            float angle = (static_cast<float>(i) / static_cast<float>(sides)) * 6.2831853f;
            float ca = std::cos(angle);
            float sa = std::sin(angle);
            AddVertex(verts, ca * r0, y0, sa * r0, ca, 0.0f, sa, static_cast<float>(i), 0.0f, col);
            AddVertex(verts, ca * r1, y1, sa * r1, ca, 0.0f, sa, static_cast<float>(i), 1.0f, col);
        }
        for (int i = 0; i < sides; ++i) {
            uint16_t v0 = baseIdx + static_cast<uint16_t>(i * 2);
            uint16_t v1 = baseIdx + static_cast<uint16_t>(i * 2 + 1);
            uint16_t v2 = baseIdx + static_cast<uint16_t>(i * 2 + 2);
            uint16_t v3 = baseIdx + static_cast<uint16_t>(i * 2 + 3);

            indices.push_back(v0);
            indices.push_back(v1);
            indices.push_back(v2);

            indices.push_back(v2);
            indices.push_back(v1);
            indices.push_back(v3);
        }
    };

    // 1. Piernas (base de apoyo)
    addPrism(0.0f, 0.40f, 0.12f, 0.14f, 6, legCol);

    // 2. Torso principal
    addPrism(0.40f, 1.25f, 0.26f, 0.30f, 6, torsoCol);

    // 3. Cinturón y hombreras doradas
    addPrism(0.40f, 0.48f, 0.28f, 0.28f, 6, goldCol);
    addPrism(1.18f, 1.26f, 0.32f, 0.32f, 6, goldCol);

    // 4. Casco / Cabeza
    addPrism(1.28f, 1.68f, 0.22f, 0.18f, 6, helmetCol);

    // 5. Visor cian brillante frontal (apuntando en dirección +Z hacia donde mira el personaje)
    uint16_t vBase = static_cast<uint16_t>(verts.size());
    AddVertex(verts, -0.12f, 1.42f, 0.21f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, visorCol);
    AddVertex(verts,  0.12f, 1.42f, 0.21f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, visorCol);
    AddVertex(verts,  0.12f, 1.58f, 0.20f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, visorCol);
    AddVertex(verts, -0.12f, 1.58f, 0.20f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, visorCol);

    indices.push_back(vBase);
    indices.push_back(vBase + 1);
    indices.push_back(vBase + 2);
    indices.push_back(vBase);
    indices.push_back(vBase + 2);
    indices.push_back(vBase + 3);
}

bool SceneEnvironmentGX2::CreateProceduralTextures() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_texturesCreated) return true;

    constexpr uint32_t TEX_SIZE = 128;

    auto initTex = [this](ResourceHandle<GX2Texture>& handle, const char* name, auto genPixel) -> bool {
        handle = m_texturePool.Emplace();
        GX2Texture* tex = m_texturePool.Get(handle);
        if (!tex) return false;

        std::memset(tex, 0, sizeof(GX2Texture));
        tex->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        tex->surface.width = TEX_SIZE;
        tex->surface.height = TEX_SIZE;
        tex->surface.depth = 1;
        tex->surface.mipLevels = 1;
        tex->surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        tex->surface.aa = GX2_AA_MODE1X;
        tex->surface.use = GX2_SURFACE_USE_TEXTURE;
        tex->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

        GX2CalcSurfaceSizeAndAlignment(&tex->surface);
        tex->surface.image = MemoryManager::Instance().AllocAligned(
            tex->surface.alignment > 256 ? tex->surface.alignment : 256,
            tex->surface.imageSize,
            MemoryTag::Texture,
            name
        );

        if (!tex->surface.image) {
            m_texturePool.Destroy(handle);
            return false;
        }

        uint8_t* pImage = static_cast<uint8_t*>(tex->surface.image);
        uint32_t pitchBytes = tex->surface.pitch * 4;

        for (uint32_t y = 0; y < TEX_SIZE; ++y) {
            uint8_t* row = pImage + y * pitchBytes;
            for (uint32_t x = 0; x < TEX_SIZE; ++x) {
                std::array<uint8_t, 4> px = genPixel(x, y);
                row[x * 4 + 0] = px[0]; // R
                row[x * 4 + 1] = px[1]; // G
                row[x * 4 + 2] = px[2]; // B
                row[x * 4 + 3] = px[3]; // A
            }
        }

        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, tex->surface.image, tex->surface.imageSize);

        tex->viewFirstMip = 0;
        tex->viewNumMips = 1;
        tex->viewFirstSlice = 0;
        tex->viewNumSlices = 1;
        tex->compMap = 0x00010203; // R, G, B, A mapping estándar

        GX2InitTextureRegs(tex);
        return true;
    };

    // 1. Textura de Pasto (Mosaico verde esmeralda con motas orgánicas)
    bool ok1 = initTex(m_hGrassTexture, "Tex_Grass", [](uint32_t x, uint32_t y) -> std::array<uint8_t, 4> {
        // Ruido simple determinista de alta frecuencia
        uint32_t h = (x * 127 + y * 311) ^ (x * y * 43);
        float n1 = static_cast<float>((h ^ (h >> 5)) & 0xFF) / 255.0f;
        float n2 = static_cast<float>(((x & 7) * 31 + (y & 7) * 23) & 0xFF) / 255.0f;

        uint8_t r = static_cast<uint8_t>(38.0f + n1 * 22.0f);
        uint8_t g = static_cast<uint8_t>(130.0f + n1 * 50.0f + n2 * 25.0f);
        uint8_t b = static_cast<uint8_t>(32.0f + n1 * 18.0f);
        return { r, g, b, 255 };
    });

    // 2. Textura de Ladrillos (Mampostería con hiladas alternadas y mortero gris)
    bool ok2 = initTex(m_hBrickTexture, "Tex_Brick", [](uint32_t x, uint32_t y) -> std::array<uint8_t, 4> {
        constexpr uint32_t BRICK_H = 16;
        constexpr uint32_t BRICK_W = 32;
        constexpr uint32_t MORTAR = 2;

        uint32_t row = y / BRICK_H;
        uint32_t rowY = y % BRICK_H;
        uint32_t shiftX = (row & 1) ? (BRICK_W / 2) : 0;
        uint32_t colX = (x + shiftX) % BRICK_W;

        bool isMortar = (rowY < MORTAR) || (colX < MORTAR);
        if (isMortar) {
            uint8_t m = 185;
            return { m, m, m, 255 };
        }

        // Tono terracota con textura granular
        uint32_t h = (x * 73 + y * 197) & 0xFF;
        float var = (static_cast<float>(h) / 255.0f) * 30.0f - 15.0f;
        uint8_t r = static_cast<uint8_t>(std::clamp(175.0f + var, 120.0f, 230.0f));
        uint8_t g = static_cast<uint8_t>(std::clamp(68.0f + var * 0.6f, 40.0f, 110.0f));
        uint8_t b = static_cast<uint8_t>(std::clamp(48.0f + var * 0.4f, 25.0f, 85.0f));
        return { r, g, b, 255 };
    });

    // 3. Textura de Roca (Piedra granito con vetas y microrelieve)
    bool ok3 = initTex(m_hRockTexture, "Tex_Rock", [](uint32_t x, uint32_t y) -> std::array<uint8_t, 4> {
        uint32_t h = (x * 157 + y * 263 + (x ^ y) * 97) & 0xFF;
        float n = static_cast<float>(h) / 255.0f;
        float streak = std::sin(static_cast<float>(x + y * 2) * 0.15f) * 0.5f + 0.5f;

        uint8_t base = static_cast<uint8_t>(std::clamp(85.0f + n * 45.0f + streak * 30.0f, 40.0f, 210.0f));
        uint8_t r = base;
        uint8_t g = static_cast<uint8_t>(base * 0.98f);
        uint8_t b = static_cast<uint8_t>(base * 1.05f); // Tono ligeramente frío
        return { r, g, b, 255 };
    });

    if (ok1 && ok2 && ok3) {
        GX2InitSampler(&m_sampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_POINT);
        m_texturesCreated = true;
        return true;
    }
#endif
    return false;
}

void SceneEnvironmentGX2::DestroyProceduralTextures() {
#if defined(__WIIU__) || defined(__wiiu__)
    auto freeTexHandle = [this](ResourceHandle<GX2Texture>& handle) {
        if (GX2Texture* tex = m_texturePool.Get(handle)) {
            if (tex->surface.image) {
                MemoryManager::Instance().FreeAligned(tex->surface.image);
                tex->surface.image = nullptr;
            }
            m_texturePool.Destroy(handle);
        }
    };

    freeTexHandle(m_hGrassTexture);
    freeTexHandle(m_hBrickTexture);
    freeTexHandle(m_hRockTexture);
    m_texturesCreated = false;
#endif
}

void SceneEnvironmentGX2::OnEnter() {
    m_animTime = 0.0f;
    m_lastFrameTicks = SDL_GetTicks();
    m_smoothedFps = 60.0f;
    AudioManager::Instance().StopBGM();

    InitGX2Pipeline();

    // 1. Inicialización y Registro de Entidades en ECSWorld (OPT-251..OPT-254)
    m_ecsWorld.Clear();

    // 1.1 Entidad Terreno: TransformComponent + MeshComponent + MaterialComponent + BoundingVolumeComponent (OPT-252, OPT-256)
    m_terrainEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_terrainEntity, Point3D{0.0f, 0.0f, 0.0f});
#if defined(__WIIU__) || defined(__wiiu__)
    m_ecsWorld.EmplaceComponent<MeshComponent>(m_terrainEntity,
        static_cast<void*>(&m_terrainVBO), static_cast<void*>(&m_terrainIBO),
        m_terrainIBO.elemCount, m_terrainVBO.elemCount,
        GX2_PRIMITIVE_MODE_TRIANGLES, sizeof(EnvironmentVertex), 0);
    auto& terrainMat = m_ecsWorld.EmplaceComponent<MaterialComponent>(m_terrainEntity,
        1.0f, 3.5f, 0.85f, -0.35f, BlendMode::Opaque, true, true);
    terrainMat.textureHandle = m_hGrassTexture;
#else
    m_ecsWorld.EmplaceComponent<MeshComponent>(m_terrainEntity);
    m_ecsWorld.EmplaceComponent<MaterialComponent>(m_terrainEntity, 1.0f, 3.5f, 0.85f, -0.35f);
#endif
    m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_terrainEntity, Point3D{0.0f, 0.0f, 0.0f}, 25.0f, false, false);

    // 1.2 Entidad Muralla: TransformComponent + MeshComponent + MaterialComponent + BoundingVolumeComponent (OPT-252, OPT-256)
    m_wallEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_wallEntity, Point3D{0.0f, 0.0f, 0.0f});
#if defined(__WIIU__) || defined(__wiiu__)
    m_ecsWorld.EmplaceComponent<MeshComponent>(m_wallEntity,
        static_cast<void*>(&m_wallVBO), static_cast<void*>(&m_wallIBO),
        m_wallIBO.elemCount, m_wallVBO.elemCount,
        GX2_PRIMITIVE_MODE_TRIANGLES, sizeof(EnvironmentVertex), 0);
    auto& wallMat = m_ecsWorld.EmplaceComponent<MaterialComponent>(m_wallEntity,
        1.0f, 1.0f, 0.70f, -0.35f, BlendMode::Opaque, true, true);
    wallMat.textureHandle = m_hBrickTexture;
#else
    m_ecsWorld.EmplaceComponent<MeshComponent>(m_wallEntity);
    m_ecsWorld.EmplaceComponent<MaterialComponent>(m_wallEntity, 1.0f, 1.0f, 0.70f, -0.35f);
#endif
    m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_wallEntity, Point3D{-3.5f, 0.95f, -2.8f}, 3.2f, true, true);

    // 1.3 Entidad Roca: TransformComponent + MeshComponent + MaterialComponent + BoundingVolumeComponent (OPT-252, OPT-256)
    m_rockEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_rockEntity, Point3D{0.0f, 0.0f, 0.0f});
#if defined(__WIIU__) || defined(__wiiu__)
    m_ecsWorld.EmplaceComponent<MeshComponent>(m_rockEntity,
        static_cast<void*>(&m_rockVBO), static_cast<void*>(&m_rockIBO),
        m_rockIBO.elemCount, m_rockVBO.elemCount,
        GX2_PRIMITIVE_MODE_TRIANGLES, sizeof(EnvironmentVertex), 0);
    auto& rockMat = m_ecsWorld.EmplaceComponent<MaterialComponent>(m_rockEntity,
        1.0f, 1.2f, 0.35f, -0.35f, BlendMode::Opaque, true, true);
    rockMat.textureHandle = m_hRockTexture;
#else
    m_ecsWorld.EmplaceComponent<MeshComponent>(m_rockEntity);
    m_ecsWorld.EmplaceComponent<MaterialComponent>(m_rockEntity, 1.0f, 1.2f, 0.35f, -0.35f);
#endif
    m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_rockEntity, Point3D{-3.5f, 0.65f, 3.8f}, 1.8f, true, true);

    // 1.4 Entidad Laguna: TransformComponent + MeshComponent + MaterialComponent + AudioEmitterComponent + BoundingVolumeComponent
    Point3D lagoonPos{7.0f, -0.30f, 7.0f};
    m_lagoonEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_lagoonEntity, lagoonPos);
#if defined(__WIIU__) || defined(__wiiu__)
    m_ecsWorld.EmplaceComponent<MeshComponent>(m_lagoonEntity,
        static_cast<void*>(&m_lagoonVBO), static_cast<void*>(&m_lagoonIBO),
        m_lagoonIBO.elemCount, m_lagoonVBO.elemCount,
        GX2_PRIMITIVE_MODE_TRIANGLES, sizeof(EnvironmentVertex), 0);
#else
    m_ecsWorld.EmplaceComponent<MeshComponent>(m_lagoonEntity);
#endif
    m_ecsWorld.EmplaceComponent<MaterialComponent>(m_lagoonEntity,
        2.0f, 1.0f, 0.10f, -0.35f, BlendMode::AlphaBlend, true, false);
    m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_lagoonEntity, Point3D{0.0f, 0.0f, 0.0f}, 4.5f, true, true);
    auto& lagoonAudio = m_ecsWorld.EmplaceComponent<AudioEmitterComponent>(m_lagoonEntity,
        &AudioEngine3D::Instance().GetWaterAmbienceClip(), 0.85f, 2.5f, 32.0f, true, 60);
    lagoonAudio.emitter.position = lagoonPos;
    lagoonAudio.emitter.rolloffFactor = 1.0f;
    lagoonAudio.Play();

    // 1.5 Entidad Sol: TransformComponent + MeshComponent + MaterialComponent + LightComponent + AudioEmitterComponent + BoundingVolumeComponent
    Point3D initialSunPos = SUN_PRESETS[m_timeOfDayIndex].dir * 25.0f;
    m_sunEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_sunEntity, initialSunPos);
#if defined(__WIIU__) || defined(__wiiu__)
    m_ecsWorld.EmplaceComponent<MeshComponent>(m_sunEntity,
        static_cast<void*>(&m_sunOrbVBO), static_cast<void*>(&m_sunOrbIBO),
        m_sunOrbIBO.elemCount, m_sunOrbVBO.elemCount,
        GX2_PRIMITIVE_MODE_TRIANGLES, sizeof(EnvironmentVertex), 0);
#else
    m_ecsWorld.EmplaceComponent<MeshComponent>(m_sunEntity);
#endif
    m_ecsWorld.EmplaceComponent<MaterialComponent>(m_sunEntity,
        3.0f, 1.0f, 0.0f, 0.0f, BlendMode::Opaque, false, false);
    m_ecsWorld.EmplaceComponent<LightComponent>(m_sunEntity,
        SUN_PRESETS[m_timeOfDayIndex].color, SUN_PRESETS[m_timeOfDayIndex].glow, 65.0f, LightType::Directional);
    m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_sunEntity, Point3D{0.0f, 0.0f, 0.0f}, 4.0f, false, false);
    auto& sunAudio = m_ecsWorld.EmplaceComponent<AudioEmitterComponent>(m_sunEntity,
        &AudioEngine3D::Instance().GetBeaconHumClip(), 0.45f, 6.0f, 65.0f, true, 40);
    sunAudio.emitter.position = initialSunPos;
    sunAudio.emitter.rolloffFactor = 0.6f;
    sunAudio.Play();

    // 1.6 Entidad Cámara Principal TV: TransformComponent + CameraComponent (OPT-253, OPT-257)
    float curCosP = std::cos(m_camPitch);
    float curSinP = std::sin(m_camPitch);
    float curCosY = std::cos(m_camYaw);
    float curSinY = std::sin(m_camYaw);
    Point3D initialCamPos{
        m_camTarget.x + m_camDistance * curSinY * curCosP,
        m_camTarget.y + m_camDistance * curSinP,
        m_camTarget.z - m_camDistance * curCosY * curCosP
    };
    m_cameraEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_cameraEntity, initialCamPos);
    m_ecsWorld.EmplaceComponent<CameraComponent>(m_cameraEntity,
        65.0f, 1280.0f / 720.0f, 0.25f, 120.0f, m_camTarget, DisplayTarget::TV);

    // 1.7 Entidad Cámara Secundaria DRC GamePad (Táctica Cenital Asimétrica) (OPT-257)
    Point3D initialDrcCamPos{0.0f, 24.0f, 8.5f};
    m_drcCameraEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_drcCameraEntity, initialDrcCamPos);
    auto& drcCamComp = m_ecsWorld.EmplaceComponent<CameraComponent>(m_drcCameraEntity,
        50.0f, 1280.0f / 720.0f, 0.25f, 150.0f, Point3D{0.0f, 0.0f, 0.0f}, DisplayTarget::DRC);
    drcCamComp.up = Point3D{0.0f, 1.0f, 0.0f};

    // 1.8 Entidad Viento: TransformComponent + AudioEmitterComponent + HierarchyComponent (OPT-254)
    Point3D initialWindOffset{0.0f, 5.0f, 0.0f};
    m_windEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_windEntity, initialWindOffset);
    m_ecsWorld.EmplaceComponent<HierarchyComponent>(m_windEntity, m_cameraEntity, 1);
    auto& windAudio = m_ecsWorld.EmplaceComponent<AudioEmitterComponent>(m_windEntity,
        &AudioEngine3D::Instance().GetWindAmbienceClip(), 0.35f, 5.0f, 45.0f, true, 30);
    windAudio.emitter.position = initialCamPos + initialWindOffset;
    windAudio.Play();

    // 1.9 Entidad Jugador: TransformComponent + KinematicCharacterComponent + MeshComponent + MaterialComponent + BoundingVolumeComponent (Hito 4 / OPT-207)
    Point3D initialPlayerPos{0.0f, 0.875f, 0.0f};
    initialPlayerPos.y = KinematicCharacterSystem::GetTerrainHeight(0.0f, 0.0f) + 0.875f;
    m_playerEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_playerEntity, initialPlayerPos);
    m_ecsWorld.EmplaceComponent<KinematicCharacterComponent>(m_playerEntity, 0.35f, 1.75f, 4.0f, 7.5f, 5.8f, 16.0f);
    m_ecsWorld.EmplaceComponent<BoundingVolumeComponent>(m_playerEntity, Point3D{0.0f, 0.875f, 0.0f}, 1.0f, true, true);
#if defined(__WIIU__) || defined(__wiiu__)
    m_ecsWorld.EmplaceComponent<MeshComponent>(m_playerEntity,
        static_cast<void*>(&m_playerVBO), static_cast<void*>(&m_playerIBO),
        m_playerIBO.elemCount, m_playerVBO.elemCount,
        GX2_PRIMITIVE_MODE_TRIANGLES, sizeof(EnvironmentVertex), 0);
    auto& playerMat = m_ecsWorld.EmplaceComponent<MaterialComponent>(m_playerEntity,
        1.0f, 1.0f, 0.45f, -0.35f, BlendMode::Opaque, true, true);
    playerMat.textureHandle = m_hRockTexture;
#else
    m_ecsWorld.EmplaceComponent<MeshComponent>(m_playerEntity);
    m_ecsWorld.EmplaceComponent<MaterialComponent>(m_playerEntity, 1.0f, 1.0f, 0.45f, -0.35f);
#endif

    // Inicializar Cámara en Modo Tercera Persona siguiendo al Jugador
    m_cameraMode = CameraMode::ThirdPersonFollow;
    m_camTarget = Point3D{initialPlayerPos.x, initialPlayerPos.y + 0.35f, initialPlayerPos.z};
    m_targetCamTarget = m_camTarget;
    m_camDistance = 6.5f;
    m_targetCamDistance = 6.5f;
    m_camPitch = 0.38f;
    m_targetCamPitch = 0.38f;

    // 2. Ejecutar paso inicial de sistemas cinemático, cámara, culling y acústico
    TransformSystem::Update(m_ecsWorld);
    CameraSystem::Update(m_ecsWorld);
    if (const auto* tvCam = m_ecsWorld.GetComponent<CameraComponent>(m_cameraEntity)) {
        CullingSystem::Update(m_ecsWorld, *tvCam, 1280.0f, 720.0f);
    }
    AudioSpatialSystem::Update(m_ecsWorld);
}

void SceneEnvironmentGX2::OnExit() {
    // 1. Detener deterministamente todos los emisores acústicos en ECS
    if (auto* lagoonAudio = m_ecsWorld.GetComponent<AudioEmitterComponent>(m_lagoonEntity)) {
        lagoonAudio->Stop();
    }
    if (auto* sunAudio = m_ecsWorld.GetComponent<AudioEmitterComponent>(m_sunEntity)) {
        sunAudio->Stop();
    }
    if (auto* windAudio = m_ecsWorld.GetComponent<AudioEmitterComponent>(m_windEntity)) {
        windAudio->Stop();
    }
    AudioSpatialSystem::Update(m_ecsWorld);

    // 2. Limpieza de entidades y componentes en ECSWorld
    m_ecsWorld.Clear();

    // 3. Destrucción de pipeline gráfico y texturas en silicio
    DestroyGX2Pipeline();
    DestroyProceduralTextures();
}

void SceneEnvironmentGX2::Update(float dt) {
    m_animTime += dt;

    // 1. Telemetría de FPS
    uint32_t currentTicks = SDL_GetTicks();
    uint32_t deltaTicks = currentTicks - m_lastFrameTicks;
    m_lastFrameTicks = currentTicks;
    if (deltaTicks > 0) {
        float instantFps = 1000.0f / static_cast<float>(deltaTicks);
        m_smoothedFps = m_smoothedFps * 0.92f + instantFps * 0.08f;
    }

    // 2. Giro automático opcional de la cámara (sin wrap brusco a 2*pi para evitar rebote con el suavizado exponencial)
    if (m_autoRotateCam && !m_isDraggingCamera) {
        m_targetCamYaw += dt * 0.15f;
    }

    // 3. Ergonomía táctil GamePad
    auto& input = InputManager::Instance();
    const auto& touch = input.GetTouchState();

    if (touch.active) {
        float tx = static_cast<float>(touch.x);
        float ty = static_cast<float>(touch.y);

        // Control de cámara en el Viewport 3D despejado (Y in [105, 590])
        if (ty >= 105.0f && ty <= 590.0f) {
            if (touch.justPressed) {
                m_isDraggingCamera = true;
                m_dragStartX = tx;
                m_dragStartY = ty;
                m_initialYaw = m_targetCamYaw;
                m_initialPitch = m_targetCamPitch;
            } else if (m_isDraggingCamera) {
                float dx = tx - m_dragStartX;
                float dy = ty - m_dragStartY;
                m_targetCamYaw = m_initialYaw + dx * 0.006f;
                m_targetCamPitch = std::clamp(m_initialPitch - dy * 0.005f, 0.05f, 1.45f);
            }
        }

        // ==========================================
        // FILA 1 DE BOTONES (Y in [14, 54], H = 40):
        // ==========================================
        // Botón 1: < VOLVER [25, 14, 140, 40]
        if (touch.justPressed && !m_isDraggingCamera && tx >= 25.0f && tx <= 165.0f && ty >= 14.0f && ty <= 54.0f) {
            AudioManager::Instance().PlayClick();
            SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuGX2>());
            return;
        }

        // Botón 2: NUBES [180, 14, 270, 40]
        if (touch.justPressed && !m_isDraggingCamera && tx >= 180.0f && tx <= 450.0f && ty >= 14.0f && ty <= 54.0f) {
            m_cloudCoverageIndex = (m_cloudCoverageIndex + 1) % static_cast<int>(CLOUD_COVERAGES.size());
            AudioManager::Instance().PlayClick();
        }

        // Botón 3: VIENTO [465, 14, 260, 40]
        if (touch.justPressed && !m_isDraggingCamera && tx >= 465.0f && tx <= 725.0f && ty >= 14.0f && ty <= 54.0f) {
            m_windSpeedIndex = (m_windSpeedIndex + 1) % static_cast<int>(WIND_SPEEDS.size());
            AudioManager::Instance().PlayClick();
        }

        // Botón 4: HORA [740, 14, 260, 40]
        if (touch.justPressed && !m_isDraggingCamera && tx >= 740.0f && tx <= 1000.0f && ty >= 14.0f && ty <= 54.0f) {
            m_timeOfDayIndex = (m_timeOfDayIndex + 1) % static_cast<int>(SUN_PRESETS.size());
            AudioManager::Instance().PlayClick();
        }

        // Botón 5: CAMARA RESET [1015, 14, 240, 40]
        if (touch.justPressed && !m_isDraggingCamera && tx >= 1015.0f && tx <= 1255.0f && ty >= 14.0f && ty <= 54.0f) {
            m_targetCamTarget = Point3D{0.0f, 0.8f, 0.0f};
            m_targetCamYaw = 0.85f;
            m_targetCamPitch = 0.48f;
            m_targetCamDistance = 15.5f;
            AudioManager::Instance().PlayClick();
        }

        // ==========================================
        // FILA 2 DE BOTONES (Y in [58, 98], H = 40):
        // ==========================================
        // Botón 6: AGUA TOGGLE [25, 58, 240, 40]
        if (touch.justPressed && !m_isDraggingCamera && tx >= 25.0f && tx <= 265.0f && ty >= 58.0f && ty <= 98.0f) {
            m_waterActive = !m_waterActive;
            AudioManager::Instance().PlayClick();
        }

        // Botón 7: SOL GLOW TOGGLE [280, 58, 250, 40]
        if (touch.justPressed && !m_isDraggingCamera && tx >= 280.0f && tx <= 530.0f && ty >= 58.0f && ty <= 98.0f) {
            m_sunGlowActive = !m_sunGlowActive;
            AudioManager::Instance().PlayClick();
        }

        // Botón 8: GIRO CAMARA [545, 58, 240, 40]
        if (touch.justPressed && !m_isDraggingCamera && tx >= 545.0f && tx <= 785.0f && ty >= 58.0f && ty <= 98.0f) {
            m_autoRotateCam = !m_autoRotateCam;
            AudioManager::Instance().PlayClick();
        }

        // Botón 9: VISTA / COMPOSITOR [800, 58, 250, 40] (OPT-257)
        if (touch.justPressed && !m_isDraggingCamera && tx >= 800.0f && tx <= 1050.0f && ty >= 58.0f && ty <= 98.0f) {
            DisplayCompositor::Instance().CycleMode();
            AudioManager::Instance().PlayClick();
        }

        // Botón 10: MODO CAMARA [1060, 58, 200, 40] (Hito 4: 3ra Persona KCC / Orbita Libre)
        if (touch.justPressed && !m_isDraggingCamera && tx >= 1060.0f && tx <= 1260.0f && ty >= 58.0f && ty <= 98.0f) {
            m_cameraMode = (m_cameraMode == CameraMode::ThirdPersonFollow) ? CameraMode::FreeOrbit : CameraMode::ThirdPersonFollow;
            if (m_cameraMode == CameraMode::ThirdPersonFollow) {
                m_targetCamDistance = 6.5f;
                m_targetCamPitch = 0.38f;
            }
            AudioManager::Instance().PlayClick();
        }
    } else {
        m_isDraggingCamera = false;
    }

    // 4. Control Físico (GamePad VPAD, Pro Controller KPAD, Teclado/Mando SDL)
    const auto& stickL = input.GetLeftStick();
    const auto& stickR = input.GetRightStick();

    // Orientación en el plano XZ para desplazamiento relativo a la cámara
    float cosY = std::cos(m_targetCamYaw);
    float sinY = std::sin(m_targetCamYaw);
    float fwdX = -sinY;
    float fwdZ = cosY;
    float rightX = cosY;
    float rightZ = sinY;

    // Conmutación de Modo de Cámara con Botón R3 (Stick Derecho)
    if (input.IsButtonTriggered(VPAD_BUTTON_STICK_R) || input.IsButtonTriggered(WPAD_PRO_BUTTON_STICK_R)) {
        m_cameraMode = (m_cameraMode == CameraMode::ThirdPersonFollow) ? CameraMode::FreeOrbit : CameraMode::ThirdPersonFollow;
        if (m_cameraMode == CameraMode::ThirdPersonFollow) {
            m_targetCamDistance = 6.5f;
            m_targetCamPitch = 0.38f;
        }
        AudioManager::Instance().PlayClick();
    }

    if (m_cameraMode == CameraMode::ThirdPersonFollow) {
        // Modo Tercera Persona: Stick Izquierdo controla al Personaje Cinemático en 3D
        float moveX = rightX * stickL.x + fwdX * stickL.y;
        float moveZ = rightZ * stickL.x + fwdZ * stickL.y;
        bool sprint = input.IsButtonHeld(VPAD_BUTTON_L) || input.IsButtonHeld(WPAD_PRO_BUTTON_L);

        if (auto* kcc = m_ecsWorld.GetComponent<KinematicCharacterComponent>(m_playerEntity)) {
            kcc->SetMovementInput(moveX, moveZ, sprint);

            // Solicitud de impulso de salto con Botón B, Botón A o Espacio
            if (input.IsButtonTriggered(VPAD_BUTTON_B) || input.IsButtonTriggered(WPAD_PRO_BUTTON_B) ||
                input.IsButtonTriggered(VPAD_BUTTON_A) || input.IsButtonTriggered(WPAD_PRO_BUTTON_A)) {
                kcc->RequestJump();
            }
        }
    } else {
        // Modo Órbita Libre: Desplazamiento horizontal directo del pivote de cámara
        float moveSpeed = 8.0f; // m/s
        if (input.IsButtonHeld(VPAD_BUTTON_L) || input.IsButtonHeld(WPAD_PRO_BUTTON_L)) {
            moveSpeed = 16.0f; // Turbo sprint
        }

        if (std::abs(stickL.x) > 0.05f || std::abs(stickL.y) > 0.05f) {
            m_targetCamTarget.x += (fwdX * stickL.y + rightX * stickL.x) * (moveSpeed * dt);
            m_targetCamTarget.z += (fwdZ * stickL.y + rightZ * stickL.x) * (moveSpeed * dt);
            m_targetCamTarget.x = std::clamp(m_targetCamTarget.x, -14.0f, 14.0f);
            m_targetCamTarget.z = std::clamp(m_targetCamTarget.z, -14.0f, 14.0f);
        }
    }

    // Actualización del Sistema Cinemático de Personaje (Hito 4: OPT-207)
    KinematicCharacterSystem::Update(m_ecsWorld, dt);

    if (m_cameraMode == CameraMode::ThirdPersonFollow) {
        // Seguimiento suave del pivote de cámara hacia la posición tridimensional del personaje
        if (const auto* playerTrans = m_ecsWorld.GetComponent<TransformComponent>(m_playerEntity)) {
            m_targetCamTarget = Point3D{playerTrans->position.x, playerTrans->position.y + 0.35f, playerTrans->position.z};
        }
        if (const auto* kcc = m_ecsWorld.GetComponent<KinematicCharacterComponent>(m_playerEntity)) {
            m_playerHorizontalSpeed = kcc->horizontalSpeed;
            m_playerVerticalSpeed = kcc->velocity.y;
            m_playerGrounded = kcc->isGrounded;
        }
    }

    // Elevación vertical del pivote (D-Pad Arriba/Abajo) en modo libre
    if (m_cameraMode == CameraMode::FreeOrbit) {
        if (input.IsButtonHeld(VPAD_BUTTON_UP) || input.IsButtonHeld(WPAD_PRO_BUTTON_UP)) {
            m_targetCamTarget.y += 4.5f * dt;
        }
        if (input.IsButtonHeld(VPAD_BUTTON_DOWN) || input.IsButtonHeld(WPAD_PRO_BUTTON_DOWN)) {
            m_targetCamTarget.y -= 4.5f * dt;
        }
        m_targetCamTarget.y = std::clamp(m_targetCamTarget.y, -0.6f, 6.0f);
    }

    // Rotación orbital y cabeceo (Stick Derecho / Flechas / IJKL)
    if (std::abs(stickR.x) > 0.05f || std::abs(stickR.y) > 0.05f) {
        m_targetCamYaw += stickR.x * (1.8f * dt);
        m_targetCamPitch = std::clamp(m_targetCamPitch + stickR.y * (1.4f * dt), -0.40f, 1.42f);
    }

    // Zoom orbital (Gatillos ZL / ZR o Teclas Q / E)
    if (input.IsButtonHeld(VPAD_BUTTON_ZL) || input.IsButtonHeld(WPAD_PRO_BUTTON_ZL)) {
        float minZoom = (m_cameraMode == CameraMode::ThirdPersonFollow) ? 2.2f : 1.4f;
        m_targetCamDistance = std::clamp(m_targetCamDistance - 9.0f * dt, minZoom, 28.0f);
    }
    if (input.IsButtonHeld(VPAD_BUTTON_ZR) || input.IsButtonHeld(WPAD_PRO_BUTTON_ZR)) {
        m_targetCamDistance = std::clamp(m_targetCamDistance + 9.0f * dt, 1.4f, 28.0f);
    }

    // Reset de cámara con Botón Y
    if (input.IsButtonTriggered(VPAD_BUTTON_Y) || input.IsButtonTriggered(WPAD_PRO_BUTTON_Y)) {
        if (m_cameraMode == CameraMode::ThirdPersonFollow) {
            m_targetCamYaw = 0.85f;
            m_targetCamPitch = 0.38f;
            m_targetCamDistance = 6.5f;
        } else {
            m_targetCamTarget = Point3D{0.0f, 0.8f, 0.0f};
            m_targetCamYaw = 0.85f;
            m_targetCamPitch = 0.48f;
            m_targetCamDistance = 15.5f;
        }
        AudioManager::Instance().PlayClick();
    }

    // Conmutar modo de vista asimétrica (TV / DRC / PiP / Split) con Botón X (OPT-257)
    if (input.IsButtonTriggered(VPAD_BUTTON_X) || input.IsButtonTriggered(WPAD_PRO_BUTTON_X)) {
        DisplayCompositor::Instance().CycleMode();
        AudioManager::Instance().PlayClick();
    }

    // 5. Amortiguamiento exponencial analítico independiente de dt (OPT-164)
    float smoothAlpha = std::clamp(1.0f - std::exp(-12.0f * dt), 0.0f, 1.0f);
    m_camTarget.x += (m_targetCamTarget.x - m_camTarget.x) * smoothAlpha;
    m_camTarget.y += (m_targetCamTarget.y - m_camTarget.y) * smoothAlpha;
    m_camTarget.z += (m_targetCamTarget.z - m_camTarget.z) * smoothAlpha;
    m_camYaw += (m_targetCamYaw - m_camYaw) * smoothAlpha;
    m_camPitch += (m_targetCamPitch - m_camPitch) * smoothAlpha;
    m_camDistance += (m_targetCamDistance - m_camDistance) * smoothAlpha;

    // Renormalización acoplada en fase de (m_targetCamYaw, m_camYaw) en [-2*pi, +2*pi]
    // Mantiene la diferencia (m_targetCamYaw - m_camYaw) invariante sin saltos ni rebotes de 360 grados
    constexpr float TWO_PI = 6.283185307f;
    if (m_camYaw > TWO_PI && m_targetCamYaw > TWO_PI) {
        m_camYaw -= TWO_PI;
        m_targetCamYaw -= TWO_PI;
    } else if (m_camYaw < -TWO_PI && m_targetCamYaw < -TWO_PI) {
        m_camYaw += TWO_PI;
        m_targetCamYaw += TWO_PI;
    }

    // 6. [OPT-205] Barrido de Colisión Analítica de Cámara contra Terreno y Obstáculos
    auto evalTerrainHeight = [](float x, float z) -> float {
        float h1 = 0.70f * std::sin(0.32f * x) * std::cos(0.32f * z);
        float h2 = 0.35f * std::sin(0.65f * x + 0.40f * z);
        float dx = x - 7.0f;
        float dz = z - 7.0f;
        float d2 = dx * dx + dz * dz;
        float basin = 1.35f * std::exp(-d2 / 16.0f);
        return h1 + h2 - basin;
    };

    // Asegurar que el pivote objetivo no se hunda bajo el terreno
    float groundAtTarget = evalTerrainHeight(m_camTarget.x, m_camTarget.z);
    if (m_camTarget.y < groundAtTarget + 0.40f) {
        m_camTarget.y = groundAtTarget + 0.40f;
        m_targetCamTarget.y = std::max(m_targetCamTarget.y, groundAtTarget + 0.40f);
    }

    // Comprobar posición de cámara en el espacio
    float curCosP = std::cos(m_camPitch);
    float curSinP = std::sin(m_camPitch);
    float curCosY = std::cos(m_camYaw);
    float curSinY = std::sin(m_camYaw);
    Point3D curCamPos{
        m_camTarget.x + m_camDistance * curSinY * curCosP,
        m_camTarget.y + m_camDistance * curSinP,
        m_camTarget.z - m_camDistance * curCosY * curCosP
    };

    // Prevenir que la cámara atraviese la superficie del suelo (Blindaje Anti-NaN)
    float groundAtCam = evalTerrainHeight(curCamPos.x, curCamPos.z);
    if (curCamPos.y < groundAtCam + 0.48f) {
        float requiredY = groundAtCam + 0.48f - m_camTarget.y;
        // Blindaje matemático: el dominio de std::asin es [-1.0, 1.0].
        // sin(-0.40 rad) = -0.3894f, sin(1.42 rad) = 0.9887f.
        float sinRatio = std::clamp(requiredY / std::max(m_camDistance, 1.4f), -0.389f, 0.988f);
        float safePitch = std::asin(sinRatio);
        if (m_camPitch < safePitch) {
            m_camPitch = safePitch;
            m_targetCamPitch = std::max(m_targetCamPitch, safePitch);
        }
    }

    // Prevenir penetración dentro de la muralla de ladrillos real (X in [-6.0, -1.0], Z in [-3.2, -2.4], Y in [0.45, 1.45])
    // Se aplica margen de seguridad de 0.35m (radio envolvente de cámara)
    if (curCamPos.x >= -6.35f && curCamPos.x <= -0.65f &&
        curCamPos.z >= -3.55f && curCamPos.z <= -2.05f &&
        curCamPos.y >=  0.10f && curCamPos.y <=  1.80f) {
        m_camDistance = std::max(m_camDistance * 0.94f, 1.4f);
        m_targetCamDistance = m_camDistance;
    }

    // Prevenir penetración en la roca facetada en (-3.5, 0.65, 3.8) con radio 1.45m
    float dxRock1 = curCamPos.x - (-3.5f);
    float dyRock1 = curCamPos.y - 0.65f;
    float dzRock1 = curCamPos.z - 3.8f;
    float dRock1Sq = dxRock1 * dxRock1 + dyRock1 * dyRock1 + dzRock1 * dzRock1;
    if (dRock1Sq < 1.96f) { // 1.4^2
        m_camDistance = std::max(m_camDistance * 0.92f, 1.4f);
        m_targetCamDistance = m_camDistance;
    }

    // 7. Actualización del Subsistema de Audio Espacial y Cinemática ECS (OPT-251)
    Point3D camForward = (m_camTarget - curCamPos).Normalized();
    Point3D camUp{0.0f, 1.0f, 0.0f};

    AudioListener3D listener;
    listener.position = curCamPos;
    listener.forward = camForward;
    listener.up = camUp;

    // Comprobar si la cámara está sumergida bajo la superficie de la laguna
    // Laguna: centro (7.0, -0.35, 7.0), radio rWater = 4.2m
    float dxLagoon = curCamPos.x - 7.0f;
    float dzLagoon = curCamPos.z - 7.0f;
    bool isSubmerged = (dxLagoon * dxLagoon + dzLagoon * dzLagoon < 17.64f && curCamPos.y < -0.32f);
    listener.isSubmerged = isSubmerged;
    AudioEngine3D::Instance().SetListener(listener);

    // Oclusión acústica analítica de la laguna (evaluada contra el terreno intermedio, muralla y peñascos) (OPT-207)
    Point3D lagoonPos{7.0f, -0.30f, 7.0f};
    KinematicCharacterSystem::CheckLineOfSight(lagoonPos, curCamPos, m_measuredLagoonOcclusion);

    // Actualizar parámetros acústicos de la Laguna en ECS
    if (auto* lagoonAudio = m_ecsWorld.GetComponent<AudioEmitterComponent>(m_lagoonEntity)) {
        lagoonAudio->SetVolume(m_waterActive ? 0.85f : 0.0f);
        lagoonAudio->SetOcclusion(m_measuredLagoonOcclusion);
    }

    // Actualizar cinemática, iluminación y acústica del Sol en ECS
    Point3D sunPos = curCamPos + SUN_PRESETS[m_timeOfDayIndex].dir * 38.0f;
    float sunLosOcclusion = 0.0f;
    KinematicCharacterSystem::CheckLineOfSight(sunPos, curCamPos, sunLosOcclusion);
    float totalSunOcclusion = std::max(sunLosOcclusion, m_measuredSunOcclusion * 0.65f);
    if (auto* sunTransform = m_ecsWorld.GetComponent<TransformComponent>(m_sunEntity)) {
        sunTransform->SetPosition(sunPos);
    }
    if (auto* sunLight = m_ecsWorld.GetComponent<LightComponent>(m_sunEntity)) {
        sunLight->color = SUN_PRESETS[m_timeOfDayIndex].color;
        sunLight->intensity = m_sunGlowActive ? SUN_PRESETS[m_timeOfDayIndex].glow : 0.0f;
    }
    if (auto* sunAudio = m_ecsWorld.GetComponent<AudioEmitterComponent>(m_sunEntity)) {
        sunAudio->SetVolume(m_sunGlowActive ? 0.45f : 0.0f);
        sunAudio->SetOcclusion(totalSunOcclusion);
    }

    // Actualizar cinemática, target y aspect ratio de la Cámara Principal TV en ECS (OPT-253, OPT-257)
    if (auto* camTransform = m_ecsWorld.GetComponent<TransformComponent>(m_cameraEntity)) {
        camTransform->SetPosition(curCamPos);
    }
    if (auto* camComp = m_ecsWorld.GetComponent<CameraComponent>(m_cameraEntity)) {
        camComp->target = m_camTarget;
        camComp->aspectRatio = (DisplayCompositor::Instance().GetMode() == CompositorMode::SplitDualView)
            ? (640.0f / 720.0f) : (1280.0f / 720.0f);
        camComp->MarkDirty();
    }

    // Actualizar cinemática, target y aspect ratio de la Cámara Táctica DRC GamePad en ECS (OPT-257)
    if (auto* drcTransform = m_ecsWorld.GetComponent<TransformComponent>(m_drcCameraEntity)) {
        // Elevación táctica cenital (~70 grados) centrada en el jugador con Z offset seguro contra gimbal lock
        drcTransform->SetPosition(Point3D{m_camTarget.x, 24.0f, m_camTarget.z + 8.5f});
    }
    if (auto* drcComp = m_ecsWorld.GetComponent<CameraComponent>(m_drcCameraEntity)) {
        drcComp->target = m_camTarget;
        drcComp->up = Point3D{0.0f, 1.0f, 0.0f};
        drcComp->aspectRatio = (DisplayCompositor::Instance().GetMode() == CompositorMode::SplitDualView)
            ? (640.0f / 720.0f) : (1280.0f / 720.0f);
        drcComp->MarkDirty();
    }

    // Actualizar offset local de la Brisa atmosférica acoplada jerárquicamente a la Cámara (OPT-254)
    if (auto* windTransform = m_ecsWorld.GetComponent<TransformComponent>(m_windEntity)) {
        windTransform->SetPosition(Point3D{0.0f, 5.0f, 0.0f});
    }
    if (auto* windAudio = m_ecsWorld.GetComponent<AudioEmitterComponent>(m_windEntity)) {
        windAudio->SetVolume(0.20f * WIND_SPEEDS[m_windSpeedIndex]);
    }

    // Despacho determinista de sistemas ECS desacoplados (OPT-245, OPT-253, OPT-256, OPT-246, OPT-254)
    TransformSystem::Update(m_ecsWorld);
    CameraSystem::Update(m_ecsWorld);
    CompositorMode curCompMode = DisplayCompositor::Instance().GetMode();
    if (curCompMode == CompositorMode::DRCOnly) {
        if (const auto* drcCam = m_ecsWorld.GetComponent<CameraComponent>(m_drcCameraEntity)) {
            CullingSystem::Update(m_ecsWorld, *drcCam, 1280.0f, 720.0f);
        }
    } else if (const auto* tvCam = m_ecsWorld.GetComponent<CameraComponent>(m_cameraEntity)) {
        CullingSystem::Update(m_ecsWorld, *tvCam, 1280.0f, 720.0f);
    }
    AudioSpatialSystem::Update(m_ecsWorld);

    // Retorno por hardware con botón 'B'
    if (input.IsBackRequested()) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuGX2>());
    }
}

void SceneEnvironmentGX2::BuildAndUploadMeshes() {
#if defined(__WIIU__) || defined(__wiiu__)
    auto& arena = MemoryManager::Instance().GetSceneArena();

    auto uploadVBO = [](GX2RBuffer& vbo, const ArenaVector<EnvironmentVertex>& verts) -> bool {
        if (verts.empty()) return true;
        vbo.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_VERTEX_BUFFER |
            GX2R_RESOURCE_USAGE_CPU_WRITE |
            GX2R_RESOURCE_USAGE_GPU_READ
        );
        vbo.elemSize = sizeof(EnvironmentVertex);
        vbo.elemCount = static_cast<uint32_t>(verts.size());
        if (GX2RCreateBuffer(&vbo)) {
            void* p = GX2RLockBufferEx(&vbo, GX2R_RESOURCE_BIND_NONE);
            if (p) {
                std::memcpy(p, verts.data(), verts.size() * sizeof(EnvironmentVertex));
                GX2RUnlockBufferEx(&vbo, GX2R_RESOURCE_BIND_NONE);
                return true;
            }
        }
        return false;
    };

    auto uploadIBO = [](GX2RBuffer& ibo, const ArenaVector<uint16_t>& indices) -> bool {
        if (indices.empty()) return true;
        ibo.flags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_INDEX_BUFFER |
            GX2R_RESOURCE_USAGE_CPU_WRITE |
            GX2R_RESOURCE_USAGE_GPU_READ
        );
        ibo.elemSize = sizeof(uint16_t);
        ibo.elemCount = static_cast<uint32_t>(indices.size());
        if (GX2RCreateBuffer(&ibo)) {
            void* p = GX2RLockBufferEx(&ibo, GX2R_RESOURCE_BIND_NONE);
            if (p) {
                std::memcpy(p, indices.data(), indices.size() * sizeof(uint16_t));
                GX2RUnlockBufferEx(&ibo, GX2R_RESOURCE_BIND_NONE);
                return true;
            }
        }
        return false;
    };

    // 1. Terreno 3D
    {
        constexpr size_t maxVerts = 32 * 32;
        constexpr size_t maxIndices = 31 * 31 * 6;
        auto* vMem = static_cast<EnvironmentVertex*>(arena.Alloc(maxVerts * sizeof(EnvironmentVertex), 256));
        auto* iMem = static_cast<uint16_t*>(arena.Alloc(maxIndices * sizeof(uint16_t), 32));
        ArenaVector<EnvironmentVertex> verts(vMem, maxVerts);
        ArenaVector<uint16_t> indices(iMem, maxIndices);
        BuildTerrainMesh(verts, indices);
        uploadVBO(m_terrainVBO, verts);
        uploadIBO(m_terrainIBO, indices);
    }
    // 2. Muralla de Ladrillos
    {
        constexpr size_t maxVerts = 64;
        constexpr size_t maxIndices = 64;
        auto* vMem = static_cast<EnvironmentVertex*>(arena.Alloc(maxVerts * sizeof(EnvironmentVertex), 256));
        auto* iMem = static_cast<uint16_t*>(arena.Alloc(maxIndices * sizeof(uint16_t), 32));
        ArenaVector<EnvironmentVertex> verts(vMem, maxVerts);
        ArenaVector<uint16_t> indices(iMem, maxIndices);
        BuildWallMesh(verts, indices);
        uploadVBO(m_wallVBO, verts);
        uploadIBO(m_wallIBO, indices);
    }
    // 3. Roca Facetada
    {
        constexpr size_t maxVerts = 128;
        constexpr size_t maxIndices = 256;
        auto* vMem = static_cast<EnvironmentVertex*>(arena.Alloc(maxVerts * sizeof(EnvironmentVertex), 256));
        auto* iMem = static_cast<uint16_t*>(arena.Alloc(maxIndices * sizeof(uint16_t), 32));
        ArenaVector<EnvironmentVertex> verts(vMem, maxVerts);
        ArenaVector<uint16_t> indices(iMem, maxIndices);
        BuildRockMesh(verts, indices);
        uploadVBO(m_rockVBO, verts);
        uploadIBO(m_rockIBO, indices);
    }
    // 4. Laguna
    {
        constexpr size_t maxVerts = (16 + 1) * (16 + 1);
        constexpr size_t maxIndices = 16 * 16 * 6;
        auto* vMem = static_cast<EnvironmentVertex*>(arena.Alloc(maxVerts * sizeof(EnvironmentVertex), 256));
        auto* iMem = static_cast<uint16_t*>(arena.Alloc(maxIndices * sizeof(uint16_t), 32));
        ArenaVector<EnvironmentVertex> verts(vMem, maxVerts);
        ArenaVector<uint16_t> indices(iMem, maxIndices);
        BuildLagoonMesh(verts, indices);
        uploadVBO(m_lagoonVBO, verts);
        uploadIBO(m_lagoonIBO, indices);
    }
    // 5. Bóveda Celeste (SkyDome)
    {
        constexpr size_t maxVerts = (12 + 1) * (20 + 1);
        constexpr size_t maxIndices = 12 * 20 * 6;
        auto* vMem = static_cast<EnvironmentVertex*>(arena.Alloc(maxVerts * sizeof(EnvironmentVertex), 256));
        auto* iMem = static_cast<uint16_t*>(arena.Alloc(maxIndices * sizeof(uint16_t), 32));
        ArenaVector<EnvironmentVertex> verts(vMem, maxVerts);
        ArenaVector<uint16_t> indices(iMem, maxIndices);
        BuildSkyDomeMesh(verts, indices);
        uploadVBO(m_skyDomeVBO, verts);
        uploadIBO(m_skyDomeIBO, indices);
    }
    // 6. Orbe Solar (SunOrb)
    {
        constexpr size_t maxVerts = (8 + 1) * (12 + 1);
        constexpr size_t maxIndices = 8 * 12 * 6;
        auto* vMem = static_cast<EnvironmentVertex*>(arena.Alloc(maxVerts * sizeof(EnvironmentVertex), 256));
        auto* iMem = static_cast<uint16_t*>(arena.Alloc(maxIndices * sizeof(uint16_t), 32));
        ArenaVector<EnvironmentVertex> verts(vMem, maxVerts);
        ArenaVector<uint16_t> indices(iMem, maxIndices);
        BuildSunOrbMesh(verts, indices);
        uploadVBO(m_sunOrbVBO, verts);
        uploadIBO(m_sunOrbIBO, indices);
    }
    // 7. Avatar del Jugador (Kinematic Player Proxy)
    {
        constexpr size_t maxVerts = 64;
        constexpr size_t maxIndices = 180;
        auto* vMem = static_cast<EnvironmentVertex*>(arena.Alloc(maxVerts * sizeof(EnvironmentVertex), 256));
        auto* iMem = static_cast<uint16_t*>(arena.Alloc(maxIndices * sizeof(uint16_t), 32));
        ArenaVector<EnvironmentVertex> verts(vMem, maxVerts);
        ArenaVector<uint16_t> indices(iMem, maxIndices);
        BuildPlayerMesh(verts, indices);
        uploadVBO(m_playerVBO, verts);
        uploadIBO(m_playerIBO, indices);
    }

    m_buffersCreated = true;

    // Sincronizar descriptores de malla en ECS si las entidades ya fueron registradas
    auto syncMesh = [this](Entity e, GX2RBuffer& vbo, GX2RBuffer& ibo) {
        if (e.IsValid()) {
            if (auto* mesh = m_ecsWorld.GetComponent<MeshComponent>(e)) {
                mesh->vbo = static_cast<void*>(&vbo);
                mesh->ibo = static_cast<void*>(&ibo);
                mesh->indexCount = ibo.elemCount;
                mesh->vertexCount = vbo.elemCount;
            }
        }
    };
    syncMesh(m_terrainEntity, m_terrainVBO, m_terrainIBO);
    syncMesh(m_wallEntity, m_wallVBO, m_wallIBO);
    syncMesh(m_rockEntity, m_rockVBO, m_rockIBO);
    syncMesh(m_lagoonEntity, m_lagoonVBO, m_lagoonIBO);
    syncMesh(m_sunEntity, m_sunOrbVBO, m_sunOrbIBO);
    syncMesh(m_playerEntity, m_playerVBO, m_playerIBO);
#endif
}

bool SceneEnvironmentGX2::InitGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_shaderLoaded && m_buffersCreated && m_texturesCreated) {
        return true;
    }

    // 1. Cargar Texturas Nativas
    if (!CreateProceduralTextures()) {
        return false;
    }

    // 2. Cargar Shaders CafeGLSL
    if (!m_shaderLoaded) {
        std::vector<uint8_t> shaderData;
        bool loaded = AssetManager::LoadBinaryFile("shaders/environment_gx2.gsh", shaderData);
        if (!loaded || shaderData.empty()) {
            shaderData.assign(ENVIRONMENT_GX2_GSH_DATA, ENVIRONMENT_GX2_GSH_DATA + ENVIRONMENT_GX2_GSH_SIZE);
            loaded = true;
        }

        if (loaded && !shaderData.empty()) {
            uint32_t vsHeaderSize = GFDGetVertexShaderHeaderSize(0, shaderData.data());
            uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
            uint32_t psHeaderSize = GFDGetPixelShaderHeaderSize(0, shaderData.data());
            uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());

            if (vsHeaderSize > 0 && vsProgSize > 0 && psHeaderSize > 0 && psProgSize > 0) {
                m_directVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHeaderSize, MemoryTag::ShaderProgram, "Env_VS_Hdr"));
                m_directVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "Env_VS_Prog");
                m_directPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHeaderSize, MemoryTag::ShaderProgram, "Env_PS_Hdr"));
                m_directPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::ShaderProgram, "Env_PS_Prog");

                if (m_directVs && m_directVsProgram && m_directPs && m_directPsProgram) {
                    BOOL vsOk = GFDGetVertexShader(m_directVs, m_directVsProgram, 0, shaderData.data());
                    BOOL psOk = GFDGetPixelShader(m_directPs, m_directPsProgram, 0, shaderData.data());
                    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directVsProgram, vsProgSize);
                    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directPsProgram, psProgSize);

                    if (vsOk && psOk) {
                        GX2AttribStream attrs[4];
                        std::memset(attrs, 0, sizeof(attrs));
                        attrs[0].location = 0;
                        attrs[0].buffer = 0;
                        attrs[0].offset = offsetof(EnvironmentVertex, pos);
                        attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                        attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[0].aluDivisor = 0;
                        attrs[0].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                        attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                        attrs[1].location = 1;
                        attrs[1].buffer = 0;
                        attrs[1].offset = offsetof(EnvironmentVertex, normal);
                        attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                        attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[1].aluDivisor = 0;
                        attrs[1].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
                        attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                        attrs[2].location = 2;
                        attrs[2].buffer = 0;
                        attrs[2].offset = offsetof(EnvironmentVertex, texCoord);
                            attrs[2].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
                            attrs[2].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attrs[2].aluDivisor = 0;
                            attrs[2].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                            attrs[2].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            attrs[3].location = 3;
                            attrs[3].buffer = 0;
                            attrs[3].offset = offsetof(EnvironmentVertex, color);
                            attrs[3].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
                            attrs[3].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attrs[3].aluDivisor = 0;
                            attrs[3].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                            attrs[3].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            uint32_t fsSize = GX2CalcFetchShaderSizeEx(4, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                            m_directFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "Env_FS_Prog");
                            if (m_directFsProgram) {
                                GX2InitFetchShaderEx(&m_directFs, static_cast<uint8_t*>(m_directFsProgram), 4, attrs,
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

    // 3. Crear Búferes VRAM
    if (m_shaderLoaded && !m_buffersCreated) {
        BuildAndUploadMeshes();
    }

    // 4. Crear Depth Buffer en eDRAM y Context State
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

    if (!m_nativeContextState) {
        m_nativeContextState = static_cast<GX2ContextState*>(
            MemoryManager::Instance().AllocAligned(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState), MemoryTag::ContextState, "EnvContextState")
        );
        if (m_nativeContextState) {
            GX2SetupContextStateEx(m_nativeContextState, TRUE);
        }
    }

    return m_shaderLoaded && m_buffersCreated && m_texturesCreated && m_depthBufferCreated;
#else
    return true;
#endif
}

void SceneEnvironmentGX2::DestroyGX2Pipeline() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown()) {
        GX2DrawDone();
    }

    if (m_buffersCreated) {
        GX2RDestroyBufferEx(&m_terrainVBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_terrainIBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_wallVBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_wallIBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_rockVBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_rockIBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_lagoonVBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_lagoonIBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_skyDomeVBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_skyDomeIBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_sunOrbVBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_sunOrbIBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_playerVBO, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_playerIBO, GX2R_RESOURCE_BIND_NONE);
        m_buffersCreated = false;
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

    if (m_shaderLoaded) {
        if (!m_useDirectGFD) {
            WHBGfxFreeShaderGroup(&m_shaderGroup);
        } else {
            void* rawVs = static_cast<void*>(m_directVs);
            MemoryManager::Instance().Free(rawVs);
            m_directVs = nullptr;
            MemoryManager::Instance().FreeAligned(m_directVsProgram);
            m_directVsProgram = nullptr;
            void* rawPs = static_cast<void*>(m_directPs);
            MemoryManager::Instance().Free(rawPs);
            m_directPs = nullptr;
            MemoryManager::Instance().FreeAligned(m_directPsProgram);
            m_directPsProgram = nullptr;
            MemoryManager::Instance().FreeAligned(m_directFsProgram);
            m_directFsProgram = nullptr;
        }
        m_shaderLoaded = false;
        m_useDirectGFD = false;
    }

    m_pColorBuffer = nullptr;
#endif
}

void SceneEnvironmentGX2::BindColorBuffer(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    m_pColorBuffer = GetSDL2ColorBuffer(renderer);
#else
    (void)renderer;
#endif
}

void SceneEnvironmentGX2::Render(SDL_Renderer* renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_shaderLoaded || !m_buffersCreated || !m_texturesCreated) {
        if (!InitGX2Pipeline()) return;
    }

    BindColorBuffer(renderer);
    RenderScene3D_GX2();
    RenderHUD_GX2();
#else
    (void)renderer;
#endif
}

void SceneEnvironmentGX2::RenderScene3D_GX2() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_nativeContextState || !m_pColorBuffer) return;

    GX2SetContextState(m_nativeContextState);
    GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    if (m_depthBufferCreated) {
        GX2SetDepthBuffer(&m_depthBuffer);
    }

    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);

    // Limpieza de Color Buffer y Depth Buffer
    GX2ClearColor(m_pColorBuffer, 0.20f, 0.45f, 0.85f, 1.0f);
    if (m_depthBufferCreated) {
        GX2ClearDepthStencilEx(&m_depthBuffer, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
    }

    // Re-vincular context state tras limpieza de buffers en silicio
    GX2SetContextState(m_nativeContextState);
    GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    if (m_depthBufferCreated) {
        GX2SetDepthBuffer(&m_depthBuffer);
    }
    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);

    // 1. Vincular Shaders
    if (!m_useDirectGFD) {
        GX2SetFetchShader(&m_shaderGroup.fetchShader);
        GX2SetVertexShader(m_shaderGroup.vertexShader);
        GX2SetPixelShader(m_shaderGroup.pixelShader);
    } else {
        GX2SetFetchShader(&m_directFs);
        GX2SetVertexShader(m_directVs);
        GX2SetPixelShader(m_directPs);
    }

    GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, FALSE,
                         GX2_POLYGON_MODE_TRIANGLE, GX2_POLYGON_MODE_TRIANGLE,
                         TRUE, TRUE, FALSE);

    // 2. [OPT-255, OPT-257] Gestión de Compositor Multi-Pantalla y Viewports Asimétricos
    RHIPipelineCache::Instance().Invalidate();

    const auto& sunPreset = SUN_PRESETS[m_timeOfDayIndex];
    Point3D sunDir = sunPreset.dir;
    float sunGlow = m_sunGlowActive ? sunPreset.glow : 0.0f;
    float cloudCoverage = CLOUD_COVERAGES[m_cloudCoverageIndex];
    float windSpeed = WIND_SPEEDS[m_windSpeedIndex];
    Point3D windDir{windSpeed, windSpeed * 0.4f, 0.0f};

    const auto* tvCam = m_ecsWorld.GetComponent<CameraComponent>(m_cameraEntity);
    const auto* tvTrans = m_ecsWorld.GetComponent<TransformComponent>(m_cameraEntity);
    Point3D tvCamPos = tvTrans ? tvTrans->position : Point3D{0.0f, 2.0f, 15.0f};

    const auto* drcCam = m_ecsWorld.GetComponent<CameraComponent>(m_drcCameraEntity);
    const auto* drcTrans = m_ecsWorld.GetComponent<TransformComponent>(m_drcCameraEntity);
    Point3D drcCamPos = drcTrans ? drcTrans->position : Point3D{0.0f, 26.0f, 0.05f};

    // Medición analítica del glow y oclusión para telemetría
    Point3D fwd = (m_camTarget - tvCamPos).Normalized();
    float cosSunCam = Point3D::Dot(fwd, sunDir);
    m_measuredSunGlow = std::clamp(std::pow(std::max(cosSunCam, 0.0f), 6.0f) * sunGlow, 0.0f, 2.0f);
    m_measuredSunOcclusion = cloudCoverage * 100.0f;

    CompositorMode compMode = DisplayCompositor::Instance().GetMode();

    if (compMode == CompositorMode::DRCOnly && drcCam) {
        // Renderizado 100% Pantalla GamePad DRC 480p
        DisplayCompositor::Instance().ApplyViewport(*drcCam);
        if (m_depthBufferCreated) {
            GX2ClearDepthStencilEx(&m_depthBuffer, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
        }
        RenderPass3D(*drcCam, drcCamPos, drcCam->viewProjMatrix, sunDir, sunGlow, cloudCoverage, windDir);
    } else if ((compMode == CompositorMode::PictureInPicture || compMode == CompositorMode::SplitDualView) && tvCam && drcCam) {
        // Pase 1: Cámara Principal TV 720p
        DisplayCompositor::Instance().ApplyViewport(*tvCam);
        RenderPass3D(*tvCam, tvCamPos, tvCam->viewProjMatrix, sunDir, sunGlow, cloudCoverage, windDir);

        // Pase 2: Cámara Táctica Asimétrica DRC GamePad (PiP o Split)
        DisplayCompositor::Instance().ApplyViewport(*drcCam);
        if (m_depthBufferCreated) {
            GX2ClearDepthStencilEx(&m_depthBuffer, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);
        }
        RHIPipelineCache::Instance().Invalidate();
        RenderPass3D(*drcCam, drcCamPos, drcCam->viewProjMatrix, sunDir, sunGlow, cloudCoverage, windDir);
    } else if (tvCam) {
        // TV Only por defecto
        DisplayCompositor::Instance().ApplyViewport(*tvCam);
        RenderPass3D(*tvCam, tvCamPos, tvCam->viewProjMatrix, sunDir, sunGlow, cloudCoverage, windDir);
    }

    // Restaurar Viewport completo para la UI 2D
    DisplayCompositor::Instance().SetExplicitViewport(0.0f, 0.0f, 1280.0f, 720.0f);
    GX2Flush();
#endif
}

void SceneEnvironmentGX2::RenderPass3D(const CameraComponent& camera,
                                       const Point3D& camPos,
                                       const Matrix4x4& vpMatrix,
                                       const Point3D& sunDir,
                                       float sunGlow,
                                       float cloudCoverage,
                                       const Point3D& windDir) {
#if defined(__WIIU__) || defined(__wiiu__)
    Matrix4x4 modelIdentity = Matrix4x4::Identity();
    Matrix4x4 mvpMatrix = vpMatrix * modelIdentity;

    alignas(256) float vsUniforms[32];
    std::memcpy(&vsUniforms[0], mvpMatrix.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], modelIdentity.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 32, vsUniforms);

    alignas(256) float psUniforms[20];
    psUniforms[0] = camPos.x; psUniforms[1] = camPos.y; psUniforms[2] = camPos.z; psUniforms[3] = m_animTime;
    psUniforms[4] = sunDir.x; psUniforms[5] = sunDir.y; psUniforms[6] = sunDir.z; psUniforms[7] = 1.0f;
    const auto& sunPreset = SUN_PRESETS[m_timeOfDayIndex];
    psUniforms[8] = static_cast<float>(sunPreset.color.r) / 255.0f;
    psUniforms[9] = static_cast<float>(sunPreset.color.g) / 255.0f;
    psUniforms[10] = static_cast<float>(sunPreset.color.b) / 255.0f;
    psUniforms[11] = sunGlow;
    psUniforms[12] = windDir.x; psUniforms[13] = windDir.y; psUniforms[14] = 1.0f; psUniforms[15] = cloudCoverage;

    GX2SetPixelSampler(&m_sampler, 0);

    // Bandera de bypass de frustum culling: la cámara cenital táctica DRC abarca todo el mapa desde arriba
    bool isDrcCamera = (camera.displayTarget == DisplayTarget::DRC);

    // =========================================================================
    // PASE 1: BÓVEDA CELESTE (SKY DOME) - PSO Caching (OPT-255)
    // =========================================================================
    RHIPipelineCache::Instance().BindState(RHIPipelineState(false, false, 7, BlendMode::Opaque, false, 1));

    Matrix4x4 skyModel = Matrix4x4::Translation(camPos.x, camPos.y, camPos.z);
    Matrix4x4 skyMVP = vpMatrix * skyModel;
    std::memcpy(&vsUniforms[0], skyMVP.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], skyModel.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 32, vsUniforms);

    psUniforms[16] = 0.0f; // Sky Dome
    psUniforms[17] = 1.0f;
    psUniforms[18] = 0.0f;
    psUniforms[19] = 0.0f;
    GX2SetPixelUniformReg(0, 20, psUniforms);

    GX2RSetAttributeBuffer(&m_skyDomeVBO, 0, sizeof(EnvironmentVertex), 0);
    GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_skyDomeIBO, GX2_INDEX_TYPE_U16, m_skyDomeIBO.elemCount, 0, 0, 1);

    // Restaurar matrices del mundo base
    std::memcpy(&vsUniforms[0], mvpMatrix.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], modelIdentity.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 32, vsUniforms);

    // =========================================================================
    // PASE 2: GEOMETRÍAS OPACAS (PSO Caching OPT-255 + Frustum/Small Culling OPT-256)
    // =========================================================================
    // Sub-Pase 2.1: Terreno 3D con Textura de Pasto
    const auto* terrainMesh = m_ecsWorld.GetComponent<MeshComponent>(m_terrainEntity);
    if (!terrainMesh || terrainMesh->isVisible || isDrcCamera) {
        const auto* terrainMat = m_ecsWorld.GetComponent<MaterialComponent>(m_terrainEntity);
        if (terrainMat) {
            RHIPipelineCache::Instance().BindState(RHIPipelineState::FromMaterial(*terrainMat));
        } else {
            RHIPipelineCache::Instance().BindState(RHIPipelineState(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque));
        }

        psUniforms[16] = terrainMat ? terrainMat->materialMode : 1.0f;
        psUniforms[17] = terrainMat ? terrainMat->uvTiling : 3.5f;
        psUniforms[18] = terrainMat ? terrainMat->customParam : -0.35f;
        psUniforms[19] = terrainMat ? terrainMat->roughness : 0.85f;
        GX2SetPixelUniformReg(0, 20, psUniforms);

        ResourceHandle<GX2Texture> grassTexHandle = (terrainMat && terrainMat->textureHandle.IsValid()) ? terrainMat->textureHandle : m_hGrassTexture;
        if (const GX2Texture* tex = m_texturePool.Get(grassTexHandle)) {
            GX2SetPixelTexture(tex, 0);
        }

        if (terrainMesh && terrainMesh->vbo && terrainMesh->ibo) {
            GX2RSetAttributeBuffer(static_cast<GX2RBuffer*>(terrainMesh->vbo), 0, sizeof(EnvironmentVertex), 0);
            GX2RDrawIndexed(static_cast<GX2PrimitiveMode>(terrainMesh->primitiveMode),
                            static_cast<GX2RBuffer*>(terrainMesh->ibo),
                            GX2_INDEX_TYPE_U16, terrainMesh->indexCount, 0, 0, 1);
        } else {
            GX2RSetAttributeBuffer(&m_terrainVBO, 0, sizeof(EnvironmentVertex), 0);
            GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_terrainIBO, GX2_INDEX_TYPE_U16, m_terrainIBO.elemCount, 0, 0, 1);
        }
    }

    // Sub-Pase 2.2: Muralla de 1 Metro con Textura de Ladrillos
    const auto* wallMesh = m_ecsWorld.GetComponent<MeshComponent>(m_wallEntity);
    if (!wallMesh || wallMesh->isVisible || isDrcCamera) {
        const auto* wallMat = m_ecsWorld.GetComponent<MaterialComponent>(m_wallEntity);
        if (wallMat) {
            RHIPipelineCache::Instance().BindState(RHIPipelineState::FromMaterial(*wallMat));
        }

        psUniforms[16] = wallMat ? wallMat->materialMode : 1.0f;
        psUniforms[17] = wallMat ? wallMat->uvTiling : 1.0f;
        psUniforms[18] = wallMat ? wallMat->customParam : -0.35f;
        psUniforms[19] = wallMat ? wallMat->roughness : 0.70f;
        GX2SetPixelUniformReg(0, 20, psUniforms);

        ResourceHandle<GX2Texture> brickTexHandle = (wallMat && wallMat->textureHandle.IsValid()) ? wallMat->textureHandle : m_hBrickTexture;
        if (const GX2Texture* tex = m_texturePool.Get(brickTexHandle)) {
            GX2SetPixelTexture(tex, 0);
        }

        if (wallMesh && wallMesh->vbo && wallMesh->ibo) {
            GX2RSetAttributeBuffer(static_cast<GX2RBuffer*>(wallMesh->vbo), 0, sizeof(EnvironmentVertex), 0);
            GX2RDrawIndexed(static_cast<GX2PrimitiveMode>(wallMesh->primitiveMode),
                            static_cast<GX2RBuffer*>(wallMesh->ibo),
                            GX2_INDEX_TYPE_U16, wallMesh->indexCount, 0, 0, 1);
        } else {
            GX2RSetAttributeBuffer(&m_wallVBO, 0, sizeof(EnvironmentVertex), 0);
            GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_wallIBO, GX2_INDEX_TYPE_U16, m_wallIBO.elemCount, 0, 0, 1);
        }
    }

    // Sub-Pase 2.3: Roca Facetada con Textura de Piedra
    const auto* rockMesh = m_ecsWorld.GetComponent<MeshComponent>(m_rockEntity);
    if (!rockMesh || rockMesh->isVisible || isDrcCamera) {
        const auto* rockMat = m_ecsWorld.GetComponent<MaterialComponent>(m_rockEntity);
        if (rockMat) {
            RHIPipelineCache::Instance().BindState(RHIPipelineState::FromMaterial(*rockMat));
        }

        psUniforms[16] = rockMat ? rockMat->materialMode : 1.0f;
        psUniforms[17] = rockMat ? rockMat->uvTiling : 1.2f;
        psUniforms[18] = rockMat ? rockMat->customParam : -0.35f;
        psUniforms[19] = rockMat ? rockMat->roughness : 0.35f;
        GX2SetPixelUniformReg(0, 20, psUniforms);

        ResourceHandle<GX2Texture> rockTexHandle = (rockMat && rockMat->textureHandle.IsValid()) ? rockMat->textureHandle : m_hRockTexture;
        if (const GX2Texture* tex = m_texturePool.Get(rockTexHandle)) {
            GX2SetPixelTexture(tex, 0);
        }

        if (rockMesh && rockMesh->vbo && rockMesh->ibo) {
            GX2RSetAttributeBuffer(static_cast<GX2RBuffer*>(rockMesh->vbo), 0, sizeof(EnvironmentVertex), 0);
            GX2RDrawIndexed(static_cast<GX2PrimitiveMode>(rockMesh->primitiveMode),
                            static_cast<GX2RBuffer*>(rockMesh->ibo),
                            GX2_INDEX_TYPE_U16, rockMesh->indexCount, 0, 0, 1);
        } else {
            GX2RSetAttributeBuffer(&m_rockVBO, 0, sizeof(EnvironmentVertex), 0);
            GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_rockIBO, GX2_INDEX_TYPE_U16, m_rockIBO.elemCount, 0, 0, 1);
        }
    }

    // Sub-Pase 2.4: Avatar del Jugador Cinemático (OPT-207)
    const auto* playerMesh = m_ecsWorld.GetComponent<MeshComponent>(m_playerEntity);
    if (!playerMesh || playerMesh->isVisible || isDrcCamera) {
        const auto* playerMat = m_ecsWorld.GetComponent<MaterialComponent>(m_playerEntity);
        if (playerMat) {
            RHIPipelineCache::Instance().BindState(RHIPipelineState::FromMaterial(*playerMat));
        } else {
            RHIPipelineCache::Instance().BindState(RHIPipelineState(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque));
        }

        const auto* playerTransform = m_ecsWorld.GetComponent<TransformComponent>(m_playerEntity);
        Matrix4x4 playerModel = playerTransform ? playerTransform->worldMatrix : Matrix4x4::Identity();
        Matrix4x4 playerMVP = vpMatrix * playerModel;
        std::memcpy(&vsUniforms[0], playerMVP.m, 16 * sizeof(float));
        std::memcpy(&vsUniforms[16], playerModel.m, 16 * sizeof(float));
        GX2SetVertexUniformReg(0, 32, vsUniforms);

        psUniforms[16] = 1.0f; // Sombreado difuso estándar con color de vértice
        psUniforms[17] = 1.0f;
        psUniforms[18] = -0.35f;
        psUniforms[19] = 0.50f;
        GX2SetPixelUniformReg(0, 20, psUniforms);

        if (const GX2Texture* tex = m_texturePool.Get(m_hRockTexture)) {
            GX2SetPixelTexture(tex, 0);
        }

        if (playerMesh && playerMesh->vbo && playerMesh->ibo) {
            GX2RSetAttributeBuffer(static_cast<GX2RBuffer*>(playerMesh->vbo), 0, sizeof(EnvironmentVertex), 0);
            GX2RDrawIndexed(static_cast<GX2PrimitiveMode>(playerMesh->primitiveMode),
                            static_cast<GX2RBuffer*>(playerMesh->ibo),
                            GX2_INDEX_TYPE_U16, playerMesh->indexCount, 0, 0, 1);
        } else {
            GX2RSetAttributeBuffer(&m_playerVBO, 0, sizeof(EnvironmentVertex), 0);
            GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_playerIBO, GX2_INDEX_TYPE_U16, m_playerIBO.elemCount, 0, 0, 1);
        }

        // Restaurar matrices del mundo base
        std::memcpy(&vsUniforms[0], mvpMatrix.m, 16 * sizeof(float));
        std::memcpy(&vsUniforms[16], modelIdentity.m, 16 * sizeof(float));
        GX2SetVertexUniformReg(0, 32, vsUniforms);
    }

    // =========================================================================
    // PASE 3: LAGUNA DE AGUA TRANSPARENTE (Alpha Blending en eDRAM)
    // =========================================================================
    if (m_waterActive) {
        const auto* lagoonMesh = m_ecsWorld.GetComponent<MeshComponent>(m_lagoonEntity);
        if (!lagoonMesh || lagoonMesh->isVisible || isDrcCamera) {
            const auto* lagoonMat = m_ecsWorld.GetComponent<MaterialComponent>(m_lagoonEntity);
            if (lagoonMat) {
                RHIPipelineCache::Instance().BindState(RHIPipelineState::FromMaterial(*lagoonMat));
            } else {
                RHIPipelineCache::Instance().BindState(RHIPipelineState(true, false, RHI_COMPARE_LEQUAL, BlendMode::AlphaBlend));
            }

            psUniforms[16] = lagoonMat ? lagoonMat->materialMode : 2.0f;
            psUniforms[17] = lagoonMat ? lagoonMat->uvTiling : 1.0f;
            psUniforms[18] = lagoonMat ? lagoonMat->customParam : -0.35f;
            psUniforms[19] = lagoonMat ? lagoonMat->roughness : 0.10f;
            GX2SetPixelUniformReg(0, 20, psUniforms);

            if (lagoonMesh && lagoonMesh->vbo && lagoonMesh->ibo) {
                GX2RSetAttributeBuffer(static_cast<GX2RBuffer*>(lagoonMesh->vbo), 0, sizeof(EnvironmentVertex), 0);
                GX2RDrawIndexed(static_cast<GX2PrimitiveMode>(lagoonMesh->primitiveMode),
                                static_cast<GX2RBuffer*>(lagoonMesh->ibo),
                                GX2_INDEX_TYPE_U16, lagoonMesh->indexCount, 0, 0, 1);
            } else {
                GX2RSetAttributeBuffer(&m_lagoonVBO, 0, sizeof(EnvironmentVertex), 0);
                GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_lagoonIBO, GX2_INDEX_TYPE_U16, m_lagoonIBO.elemCount, 0, 0, 1);
            }
        }
    }

    // =========================================================================
    // PASE 4: ORBE SOLAR FÍSICO EMISIVO EN EL CIELO
    // =========================================================================
    const auto* sunMesh = m_ecsWorld.GetComponent<MeshComponent>(m_sunEntity);
    if (!sunMesh || sunMesh->isVisible || isDrcCamera) {
        RHIPipelineCache::Instance().BindState(RHIPipelineState(false, false, 7, BlendMode::Opaque, false, 1));

        const auto* sunTransform = m_ecsWorld.GetComponent<TransformComponent>(m_sunEntity);
        Point3D sunPos = camPos + sunDir * 38.0f;
        Matrix4x4 sunModel = sunTransform ? sunTransform->worldMatrix : Matrix4x4::Translation(sunPos.x, sunPos.y, sunPos.z);
        Matrix4x4 sunMVP = vpMatrix * sunModel;
        std::memcpy(&vsUniforms[0], sunMVP.m, 16 * sizeof(float));
        std::memcpy(&vsUniforms[16], sunModel.m, 16 * sizeof(float));
        GX2SetVertexUniformReg(0, 32, vsUniforms);

        const auto* sunMat = m_ecsWorld.GetComponent<MaterialComponent>(m_sunEntity);
        psUniforms[16] = sunMat ? sunMat->materialMode : 3.0f;
        psUniforms[17] = sunMat ? sunMat->uvTiling : 1.0f;
        psUniforms[18] = sunMat ? sunMat->customParam : 0.0f;
        psUniforms[19] = sunMat ? sunMat->roughness : 0.0f;
        GX2SetPixelUniformReg(0, 20, psUniforms);

        if (sunMesh && sunMesh->vbo && sunMesh->ibo) {
            GX2RSetAttributeBuffer(static_cast<GX2RBuffer*>(sunMesh->vbo), 0, sizeof(EnvironmentVertex), 0);
            GX2RDrawIndexed(static_cast<GX2PrimitiveMode>(sunMesh->primitiveMode),
                            static_cast<GX2RBuffer*>(sunMesh->ibo),
                            GX2_INDEX_TYPE_U16, sunMesh->indexCount, 0, 0, 1);
        } else {
            GX2RSetAttributeBuffer(&m_sunOrbVBO, 0, sizeof(EnvironmentVertex), 0);
            GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &m_sunOrbIBO, GX2_INDEX_TYPE_U16, m_sunOrbIBO.elemCount, 0, 0, 1);
        }
    }
#endif
}

void SceneEnvironmentGX2::RenderHUD_GX2() {
    auto& r2d = Render2DGX2::Instance();

#if defined(__WIIU__) || defined(__wiiu__)
    if (m_nativeContextState && m_pColorBuffer) {
        GX2SetContextState(m_nativeContextState);
        GX2SetColorBuffer(m_pColorBuffer, GX2_RENDER_TARGET_0);
    }
#endif

    r2d.Begin();

    // =========================================================================
    // FILA 1 DE BOTONES (Y in [14, 54], H = 40):
    // =========================================================================
    // Botón 1: < VOLVER [25, 14, 140, 40]
    r2d.DrawButton("< VOLVER", 25.0f, 14.0f, 140.0f, 40.0f, false,
                   ColorRGBA{15, 20, 35, 230}, ColorRGBA{180, 40, 40, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    // Botón 2: NUBES [180, 14, 270, 40]
    StackString<64> nubesLabel;
    nubesLabel.Format("NUBES: [%s]", CLOUD_NAMES[m_cloudCoverageIndex]);
    r2d.DrawButton(nubesLabel.c_str(), 180.0f, 14.0f, 270.0f, 40.0f, false,
                   ColorRGBA{20, 28, 48, 220}, ColorRGBA{45, 65, 110, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    // Botón 3: VIENTO [465, 14, 260, 40]
    StackString<64> vientoLabel;
    vientoLabel.Format("VIENTO: [%s]", WIND_NAMES[m_windSpeedIndex]);
    r2d.DrawButton(vientoLabel.c_str(), 465.0f, 14.0f, 260.0f, 40.0f, false,
                   ColorRGBA{20, 28, 48, 220}, ColorRGBA{45, 65, 110, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    // Botón 4: HORA DEL DÍA [740, 14, 260, 40]
    StackString<64> horaLabel;
    horaLabel.Format("HORA: [%s]", SUN_PRESETS[m_timeOfDayIndex].name);
    r2d.DrawButton(horaLabel.c_str(), 740.0f, 14.0f, 260.0f, 40.0f, false,
                   ColorRGBA{20, 28, 48, 220}, ColorRGBA{45, 65, 110, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    // Botón 5: CAMARA RESET [1015, 14, 240, 40]
    r2d.DrawButton("CAMARA: RESET", 1015.0f, 14.0f, 240.0f, 40.0f, false,
                   ColorRGBA{20, 28, 48, 220}, ColorRGBA{45, 65, 110, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    // =========================================================================
    // FILA 2 DE BOTONES (Y in [58, 98], H = 40):
    // =========================================================================
    // Botón 6: AGUA TOGGLE [25, 58, 240, 40]
    StackString<64> aguaLabel;
    aguaLabel.Format("AGUA: [%s]", m_waterActive ? "ACTIVA" : "APAGADA");
    r2d.DrawButton(aguaLabel.c_str(), 25.0f, 58.0f, 240.0f, 40.0f, false,
                   ColorRGBA{18, 38, 55, 220}, ColorRGBA{30, 85, 120, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(),
                   m_waterActive ? ColorRGBA{100, 230, 255, 255} : ColorRGBA{160, 170, 180, 255}, 2.0f);

    // Botón 7: SOL GLOW TOGGLE [280, 58, 250, 40]
    StackString<64> glowLabel;
    glowLabel.Format("SOL GLOW: [%s]", m_sunGlowActive ? "ENCENDIDO" : "APAGADO");
    r2d.DrawButton(glowLabel.c_str(), 280.0f, 58.0f, 250.0f, 40.0f, false,
                   ColorRGBA{20, 28, 48, 220}, ColorRGBA{45, 65, 110, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(),
                   m_sunGlowActive ? ColorRGBA::Yellow() : ColorRGBA{160, 170, 180, 255}, 2.0f);

    // Botón 8: GIRO CAMARA [545, 58, 240, 40]
    StackString<64> giroLabel;
    giroLabel.Format("GIRO: [%s]", m_autoRotateCam ? "ON" : "OFF");
    r2d.DrawButton(giroLabel.c_str(), 545.0f, 58.0f, 240.0f, 40.0f, false,
                   ColorRGBA{20, 28, 48, 220}, ColorRGBA{45, 65, 110, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    // Botón 9: VISTA / COMPOSITOR [800, 58, 250, 40] (OPT-257)
    StackString<64> vistaLabel;
    vistaLabel.Format("VISTA: [%s]", DisplayCompositor::Instance().GetModeName());
    r2d.DrawButton(vistaLabel.c_str(), 800.0f, 58.0f, 250.0f, 40.0f, false,
                   ColorRGBA{20, 28, 48, 220}, ColorRGBA{45, 65, 110, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White(), 2.0f);

    // Botón 10: MODO CAMARA (Hito 4: 3ra Persona KCC / Orbita Libre) [1060, 58, 195, 40]
    StackString<64> kccModeLabel;
    kccModeLabel.Format("CAM: [%s]", (m_cameraMode == CameraMode::ThirdPersonFollow) ? "3RA PERS" : "LIBRE");
    r2d.DrawButton(kccModeLabel.c_str(), 1060.0f, 58.0f, 195.0f, 40.0f, false,
                   ColorRGBA{20, 28, 48, 220}, ColorRGBA{45, 65, 110, 240},
                   ColorRGBA::Cyan(), ColorRGBA::Yellow(),
                   (m_cameraMode == CameraMode::ThirdPersonFollow) ? ColorRGBA{120, 255, 180, 255} : ColorRGBA::White(), 2.0f);

    // =========================================================================
    // OVERLAYS Y MARCOS DEL COMPOSITOR DE VIEWPORTS (OPT-257)
    // =========================================================================
    CompositorMode curMode = DisplayCompositor::Instance().GetMode();
    if (curMode == CompositorMode::PictureInPicture) {
        // Marco exterior para Viewport PiP DRC GamePad
        r2d.DrawBox(874.0f, 406.0f, 388.0f, 220.0f, ColorRGBA{0, 0, 0, 0}, ColorRGBA{0, 220, 255, 230}, 2.0f);
        r2d.DrawBox(874.0f, 384.0f, 260.0f, 22.0f, ColorRGBA{10, 18, 32, 230}, ColorRGBA{0, 200, 255, 180});
        r2d.DrawText("GAMEPAD DRC (TACTICA)", 884.0f, 388.0f, 2.0f, ColorRGBA::Cyan());
    } else if (curMode == CompositorMode::SplitDualView) {
        // Divisor central vertical para SplitDualView
        r2d.DrawLine(640.0f, 102.0f, 640.0f, 632.0f, 2.0f, ColorRGBA{0, 200, 255, 180});
        // Etiquetas de vistas
        r2d.DrawBox(20.0f, 104.0f, 280.0f, 22.0f, ColorRGBA{10, 18, 32, 210}, ColorRGBA{0, 200, 255, 140});
        r2d.DrawText("TV: PRINCIPAL 3RA PERS", 28.0f, 108.0f, 2.0f, ColorRGBA{180, 240, 255, 240});

        r2d.DrawBox(660.0f, 104.0f, 280.0f, 22.0f, ColorRGBA{10, 18, 32, 210}, ColorRGBA{0, 200, 255, 140});
        r2d.DrawText("DRC: TACTICA CENITAL", 668.0f, 108.0f, 2.0f, ColorRGBA{180, 240, 255, 240});
    } else if (curMode == CompositorMode::DRCOnly) {
        r2d.DrawBox(20.0f, 104.0f, 340.0f, 22.0f, ColorRGBA{10, 18, 32, 210}, ColorRGBA{0, 200, 255, 140});
        r2d.DrawText("VISTA DRC GAMEPAD (TACTICA)", 28.0f, 108.0f, 2.0f, ColorRGBA::Yellow());
    }

    // =========================================================================
    // BARRA DE GUÍA Y TELEMETRÍA ESTANDARIZADA (UIToolkit - DrawHudFooter)
    // =========================================================================
    uint32_t totalTris = static_cast<uint32_t>(
        (m_terrainIBO.elemCount + m_wallIBO.elemCount + m_rockIBO.elemCount +
         m_lagoonIBO.elemCount + m_skyDomeIBO.elemCount + m_sunOrbIBO.elemCount + m_playerIBO.elemCount) / 3
    );

    auto audioTelem = AudioEngine3D::Instance().GetTelemetry();
    const char* lpfStr = audioTelem.listenerSubmerged ? "SUBACUATICO" : (audioTelem.maxOcclusionInScene > 0.35f ? "OCLUIDO" : "LIBRE");

    StackString<256> telemetria;
    telemetria.Format("FPS: %.1f | KCC: [%.1f m/s] | TRIS: %u | OCC: AGUA %.0f%% [%s] | ECS: %u | BYP: %u",
                      m_smoothedFps, m_playerHorizontalSpeed,
                      totalTris,
                      m_measuredLagoonOcclusion * 100.0f, lpfStr,
                      m_ecsWorld.GetEntityCount(), RHIPipelineCache::Instance().GetRedundantBypasses());

    const char* guideStr = (m_cameraMode == CameraMode::ThirdPersonFollow)
        ? "KCC: STICK I: MOVER | B/A: SALTAR | L: CORRER | STICK D: ORBITAR | ZL/ZR: ZOOM | R3: MODO CAM | Y: RESET"
        : "LIBRE: STICK I: DESPLAZAR | STICK D: ROTAR/TILT | ZL/ZR: ZOOM | DPAD AR/AB: ALTURA | R3: MODO CAM | Y: RESET";

    UI::UIToolkit::DrawHudFooter({
        { guideStr, ColorRGBA{220, 245, 255, 240} },
        { telemetria.string_view(), ColorRGBA::Cyan() }
    });

    r2d.End();
}

} // namespace WiiU
