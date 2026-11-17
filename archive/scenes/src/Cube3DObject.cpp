#include "Scene/Cube3DObject.hpp"
#include <algorithm>
#include <vector>

namespace WiiU {

Cube3DObject::Cube3DObject() {
    constexpr float HALF_SIZE = 0.25f;
    m_vertices[0] = {-HALF_SIZE, -HALF_SIZE, -HALF_SIZE};
    m_vertices[1] = { HALF_SIZE, -HALF_SIZE, -HALF_SIZE};
    m_vertices[2] = { HALF_SIZE,  HALF_SIZE, -HALF_SIZE};
    m_vertices[3] = {-HALF_SIZE,  HALF_SIZE, -HALF_SIZE};
    m_vertices[4] = {-HALF_SIZE, -HALF_SIZE,  HALF_SIZE};
    m_vertices[5] = { HALF_SIZE, -HALF_SIZE,  HALF_SIZE};
    m_vertices[6] = { HALF_SIZE,  HALF_SIZE,  HALF_SIZE};
    m_vertices[7] = {-HALF_SIZE,  HALF_SIZE,  HALF_SIZE};

    m_faces[0] = {{0, 1, 2, 3}, ColorRGBA::Red(),    0.0f, {0.0f, 0.0f, -1.0f}};
    m_faces[1] = {{5, 4, 7, 6}, ColorRGBA::Cyan(),   0.0f, {0.0f, 0.0f,  1.0f}};
    m_faces[2] = {{4, 0, 3, 7}, ColorRGBA::Orange(), 0.0f, {-1.0f, 0.0f, 0.0f}};
    m_faces[3] = {{1, 5, 6, 2}, ColorRGBA::Magenta(),0.0f, {1.0f, 0.0f,  0.0f}};
    m_faces[4] = {{3, 2, 6, 7}, ColorRGBA::Green(),  0.0f, {0.0f, 1.0f,  0.0f}};
    m_faces[5] = {{4, 5, 1, 0}, ColorRGBA::Yellow(), 0.0f, {0.0f, -1.0f, 0.0f}};
}

void Cube3DObject::Update(float dt) {
    m_rotY += 90.0f * dt;
    m_rotX += 60.0f * dt;
}

void Cube3DObject::ScrambleFaceColors() {
    ScrambleFaceColors(m_prng);
}

void Cube3DObject::ScrambleFaceColors(PRNG& prng) {
    for (int i = 0; i < 6; ++i) {
        m_faces[i].baseColor = ColorRGBA::Random(prng);
    }
}

void Cube3DObject::Render(SDL_Renderer* renderer, int screenWidth, int screenHeight, const DirectionalLight& light) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    RotationMatrix rot = RotationMatrix::FromEuler(m_rotX, m_rotY, m_rotZ);

    float centerX = screenWidth / 2.0f;
    float centerY = screenHeight / 2.0f;
    float zOffset = 2.5f;

    Point2D proj[8];
    for (int i = 0; i < 8; ++i) {
        Point3D t = rot.transform(m_vertices[i]);
        proj[i] = projectTransformed3D(t, zOffset, centerX, centerY, 2200.0f);
    }

    SDL_Vertex batchedVerts[24];
    int batchedIndices[36];
    int visibleVertCount = 0;
    int visibleIdxCount = 0;

    struct LineEdge { float x1, y1, x2, y2; };
    LineEdge visibleEdges[24];
    int visibleEdgeCount = 0;

    for (int f = 0; f < 6; ++f) {
        int* v = m_faces[f].v;

        Point3D rotatedNormal = rot.transform(m_faces[f].rawNormal);
        
        Point3D faceCenter = (m_vertices[v[0]] + m_vertices[v[1]] + m_vertices[v[2]] + m_vertices[v[3]]) * 0.25f;
        Point3D rotatedCenter = rot.transform(faceCenter);
        Point3D viewVector = Point3D{0.0f, 0.0f, 0.0f} - (rotatedCenter + Point3D{0.0f, 0.0f, zOffset});

        // BACK-FACE CULLING
        if (Point3D::dot(rotatedNormal, viewVector) <= 0.0f) {
            continue;
        }

        ColorRGBA litColor = calculateLighting(m_faces[f].baseColor, rotatedNormal, light);
        SDL_Color col = litColor.toSDLColor();

        int vertOffset = visibleVertCount;
        batchedVerts[vertOffset + 0] = { { proj[v[0]].x, proj[v[0]].y }, col, { 0.0f, 0.0f } };
        batchedVerts[vertOffset + 1] = { { proj[v[1]].x, proj[v[1]].y }, col, { 0.0f, 0.0f } };
        batchedVerts[vertOffset + 2] = { { proj[v[2]].x, proj[v[2]].y }, col, { 0.0f, 0.0f } };
        batchedVerts[vertOffset + 3] = { { proj[v[3]].x, proj[v[3]].y }, col, { 0.0f, 0.0f } };
        visibleVertCount += 4;

        int idxOffset = visibleIdxCount;
        batchedIndices[idxOffset + 0] = vertOffset + 0;
        batchedIndices[idxOffset + 1] = vertOffset + 1;
        batchedIndices[idxOffset + 2] = vertOffset + 2;
        batchedIndices[idxOffset + 3] = vertOffset + 0;
        batchedIndices[idxOffset + 4] = vertOffset + 2;
        batchedIndices[idxOffset + 5] = vertOffset + 3;
        visibleIdxCount += 6;

        visibleEdges[visibleEdgeCount++] = { proj[v[0]].x, proj[v[0]].y, proj[v[1]].x, proj[v[1]].y };
        visibleEdges[visibleEdgeCount++] = { proj[v[1]].x, proj[v[1]].y, proj[v[2]].x, proj[v[2]].y };
        visibleEdges[visibleEdgeCount++] = { proj[v[2]].x, proj[v[2]].y, proj[v[3]].x, proj[v[3]].y };
        visibleEdges[visibleEdgeCount++] = { proj[v[3]].x, proj[v[3]].y, proj[v[0]].x, proj[v[0]].y };
    }

    if (visibleVertCount > 0) {
        SDL_RenderGeometry(renderer, nullptr, batchedVerts, visibleVertCount, batchedIndices, visibleIdxCount);
    }

    SDL_SetRenderDrawColor(renderer, 15, 15, 15, 255);
    for (int e = 0; e < visibleEdgeCount; ++e) {
        SDL_RenderDrawLineF(renderer, visibleEdges[e].x1, visibleEdges[e].y1, visibleEdges[e].x2, visibleEdges[e].y2);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

} // namespace WiiU
