#include "Graphics/Mesh/ProceduralMeshBuilder.hpp"
#include <cmath>
#include <vector>

namespace WiiU::Graphics::ProceduralMeshBuilder {

namespace {

constexpr float kPi = 3.14159265358979323846f;

MeshVertex MakeVertex(float px, float py, float pz,
                      float nx, float ny, float nz,
                      float u, float v,
                      float r, float g, float b) noexcept {
    MeshVertex vert{};
    vert.pos[0] = px; vert.pos[1] = py; vert.pos[2] = pz;
    vert.normal[0] = nx; vert.normal[1] = ny; vert.normal[2] = nz;
    vert.texCoord[0] = u; vert.texCoord[1] = v;
    vert.color[0] = r; vert.color[1] = g; vert.color[2] = b; vert.color[3] = 1.0f;
    return vert;
}

} // namespace

bool CreatePlane(StaticMesh& outMesh,
                 float width,
                 float depth,
                 float uvScaleX,
                 float uvScaleZ,
                 uint32_t subdivX,
                 uint32_t subdivZ,
                 float r,
                 float g,
                 float b) {
    if (width <= 0.0f || depth <= 0.0f) return false;
    const uint32_t sx = (subdivX == 0u) ? 1u : (subdivX > 128u ? 128u : subdivX);
    const uint32_t sz = (subdivZ == 0u) ? 1u : (subdivZ > 128u ? 128u : subdivZ);

    const uint32_t cols = sx + 1u;
    const uint32_t rows = sz + 1u;
    std::vector<MeshVertex> vertices;
    vertices.reserve(static_cast<size_t>(cols) * static_cast<size_t>(rows));

    const float halfW = width * 0.5f;
    const float halfD = depth * 0.5f;

    for (uint32_t z = 0; z < rows; ++z) {
        const float tz = static_cast<float>(z) / static_cast<float>(sz);
        const float pz = -halfD + tz * depth;
        const float v = tz * uvScaleZ;
        for (uint32_t x = 0; x < cols; ++x) {
            const float tx = static_cast<float>(x) / static_cast<float>(sx);
            const float px = -halfW + tx * width;
            const float u = tx * uvScaleX;
            vertices.push_back(MakeVertex(px, 0.0f, pz, 0.0f, 1.0f, 0.0f, u, v, r, g, b));
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(sx) * static_cast<size_t>(sz) * 6u);
    for (uint32_t z = 0; z < sz; ++z) {
        for (uint32_t x = 0; x < sx; ++x) {
            const uint32_t i0 = z * cols + x;
            const uint32_t i1 = i0 + 1u;
            const uint32_t i2 = i0 + cols;
            const uint32_t i3 = i2 + 1u;
            // Winding CCW visto desde +Y
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    return outMesh.CreateFromVertices(vertices, indices);
}

bool CreateCube(StaticMesh& outMesh,
                float size,
                float uvScale,
                float r,
                float g,
                float b) {
    if (size <= 0.0f) return false;
    const float h = size * 0.5f;

    struct FaceDesc {
        float n[3];
        float p[4][3];
    };

    const FaceDesc faces[6] = {
        // +Z (Frente)
        {{ 0.0f,  0.0f,  1.0f}, {{-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h}}},
        // -Z (Atras)
        {{ 0.0f,  0.0f, -1.0f}, {{ h, -h, -h}, {-h, -h, -h}, {-h,  h, -h}, { h,  h, -h}}},
        // +X (Derecha)
        {{ 1.0f,  0.0f,  0.0f}, {{ h, -h,  h}, { h, -h, -h}, { h,  h, -h}, { h,  h,  h}}},
        // -X (Izquierda)
        {{-1.0f,  0.0f,  0.0f}, {{-h, -h, -h}, {-h, -h,  h}, {-h,  h,  h}, {-h,  h, -h}}},
        // +Y (Arriba)
        {{ 0.0f,  1.0f,  0.0f}, {{-h,  h,  h}, { h,  h,  h}, { h,  h, -h}, {-h,  h, -h}}},
        // -Y (Abajo)
        {{ 0.0f, -1.0f,  0.0f}, {{-h, -h, -h}, { h, -h, -h}, { h, -h,  h}, {-h, -h,  h}}}
    };

    const float uvs[4][2] = {
        {0.0f,    uvScale},
        {uvScale, uvScale},
        {uvScale, 0.0f},
        {0.0f,    0.0f}
    };

    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(24u);
    indices.reserve(36u);

    for (uint32_t f = 0; f < 6u; ++f) {
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        for (uint32_t v = 0; v < 4u; ++v) {
            vertices.push_back(MakeVertex(
                faces[f].p[v][0], faces[f].p[v][1], faces[f].p[v][2],
                faces[f].n[0], faces[f].n[1], faces[f].n[2],
                uvs[v][0], uvs[v][1],
                r, g, b
            ));
        }
        indices.push_back(base + 0u);
        indices.push_back(base + 1u);
        indices.push_back(base + 2u);
        indices.push_back(base + 0u);
        indices.push_back(base + 2u);
        indices.push_back(base + 3u);
    }

    return outMesh.CreateFromVertices(vertices, indices);
}

bool CreateUVSphere(StaticMesh& outMesh,
                    float radius,
                    uint32_t slices,
                    uint32_t stacks,
                    float r,
                    float g,
                    float b) {
    if (radius <= 0.0f) return false;
    const uint32_t sl = (slices < 6u) ? 6u : (slices > 64u ? 64u : slices);
    const uint32_t st = (stacks < 4u) ? 4u : (stacks > 64u ? 64u : stacks);

    const uint32_t stride = sl + 1u;
    std::vector<MeshVertex> vertices;
    vertices.reserve(static_cast<size_t>(st + 1u) * static_cast<size_t>(stride));

    for (uint32_t y = 0; y <= st; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(st);
        const float phi = v * kPi;
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);

        for (uint32_t x = 0; x <= sl; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(sl);
            const float theta = u * 2.0f * kPi;
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            const float nx = sinPhi * cosTheta;
            const float ny = cosPhi;
            const float nz = sinPhi * sinTheta;
            vertices.push_back(MakeVertex(
                nx * radius, ny * radius, nz * radius,
                nx, ny, nz,
                u, v,
                r, g, b
            ));
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(st) * static_cast<size_t>(sl) * 6u);
    for (uint32_t y = 0; y < st; ++y) {
        for (uint32_t x = 0; x < sl; ++x) {
            const uint32_t i0 = y * stride + x;
            const uint32_t i1 = i0 + 1u;
            const uint32_t i2 = i0 + stride;
            const uint32_t i3 = i2 + 1u;
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i3);
            indices.push_back(i2);
        }
    }

    return outMesh.CreateFromVertices(vertices, indices);
}

float EvaluateTerrainHeight(float x, float z, float maxHeight) noexcept {
    // Meseta plana central alrededor del origen (r < 7m) para exhibir esculturas y hoguera sin hundimiento
    const float r = std::sqrt(x * x + z * z);
    const float plateauMask = (r <= 5.5f) ? 0.0f : ((r >= 13.5f) ? 1.0f : ((r - 5.5f) / 8.0f));
    const float smoothMask = plateauMask * plateauMask * (3.0f - 2.0f * plateauMask);

    // Relieve multi-octava determinista (colinas + crestas montañosas perimetrales)
    const float h1 = std::sin(x * 0.14f + 0.4f) * std::cos(z * 0.13f - 0.3f) * 0.55f + 0.55f;
    const float h2 = std::sin(x * 0.29f - z * 0.24f) * 0.25f + 0.25f;
    const float h3 = std::cos(x * 0.52f + z * 0.47f) * 0.10f + 0.10f;
    const float rimBoost = (r > 18.0f) ? ((r - 18.0f) * 0.045f) : 0.0f;

    float hNorm = (h1 * 0.55f + h2 * 0.30f + h3 * 0.15f) + rimBoost;
    if (hNorm > 1.25f) hNorm = 1.25f;
    return hNorm * maxHeight * smoothMask;
}

bool CreateHeightmapTerrain(StaticMesh& outMesh,
                            float width,
                            float depth,
                            uint32_t gridX,
                            uint32_t gridZ,
                            float maxHeight) {
    if (width <= 0.0f || depth <= 0.0f || gridX < 2u || gridZ < 2u || maxHeight <= 0.0f) {
        return false;
    }

    const uint32_t gx = (gridX > 96u) ? 96u : gridX;
    const uint32_t gz = (gridZ > 96u) ? 96u : gridZ;
    const uint32_t vxCount = gx + 1u;
    const uint32_t vzCount = gz + 1u;

    std::vector<MeshVertex> vertices;
    vertices.reserve(static_cast<size_t>(vxCount) * static_cast<size_t>(vzCount));

    const float halfW = width * 0.5f;
    const float halfD = depth * 0.5f;
    const float eps = 0.25f;
    const float invMaxH = 1.0f / maxHeight;

    for (uint32_t iz = 0u; iz <= gz; ++iz) {
        const float tz = static_cast<float>(iz) / static_cast<float>(gz);
        const float z = -halfD + tz * depth;
        for (uint32_t ix = 0u; ix <= gx; ++ix) {
            const float tx = static_cast<float>(ix) / static_cast<float>(gx);
            const float x = -halfW + tx * width;

            const float y = EvaluateTerrainHeight(x, z, maxHeight);
            const float hL = EvaluateTerrainHeight(x - eps, z, maxHeight);
            const float hR = EvaluateTerrainHeight(x + eps, z, maxHeight);
            const float hD = EvaluateTerrainHeight(x, z - eps, maxHeight);
            const float hU = EvaluateTerrainHeight(x, z + eps, maxHeight);

            float nx = hL - hR;
            float ny = 2.0f * eps;
            float nz = hD - hU;
            const float invLen = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= invLen; ny *= invLen; nz *= invLen;

            float hNorm = y * invMaxH;
            if (hNorm < 0.0f) hNorm = 0.0f;
            if (hNorm > 1.0f) hNorm = 1.0f;
            const float slope = 1.0f - ny;

            vertices.push_back(MakeVertex(
                x, y, z,
                nx, ny, nz,
                tx * 8.0f, tz * 8.0f,
                hNorm, slope, 0.0f
            ));
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(gx) * static_cast<size_t>(gz) * 6u);
    for (uint32_t iz = 0u; iz < gz; ++iz) {
        for (uint32_t ix = 0u; ix < gx; ++ix) {
            const uint32_t i0 = iz * vxCount + ix;
            const uint32_t i1 = i0 + 1u;
            const uint32_t i2 = i0 + vxCount;
            const uint32_t i3 = i2 + 1u;
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    return outMesh.CreateFromVertices(vertices, indices);
}

} // namespace WiiU::Graphics::ProceduralMeshBuilder
