#include "Graphics/DebugDraw3D.hpp"
#include "Graphics/Render2D.hpp"
#include <cmath>

namespace Neleo {

namespace {
    // Estado estático de proyección para el cuadro de renderizado actual
    static Matrix4x4 s_vpMatrix = Matrix4x4::Identity();
    static float s_screenWidth = 1280.0f;
    static float s_screenHeight = 720.0f;
    static float s_nearClip = 0.15f;
}

void DebugDraw3D::SetViewProjection(const Matrix4x4& vpMatrix, float screenWidth, float screenHeight, float nearClip) noexcept {
    s_vpMatrix = vpMatrix;
    s_screenWidth = screenWidth;
    s_screenHeight = screenHeight;
    s_nearClip = nearClip;
}

void DebugDraw3D::SetMatrices(const Matrix4x4& view, const Matrix4x4& proj, float screenWidth, float screenHeight, float nearClip) noexcept {
    // En álgebra matricial column-major de CafeGLSL/OpenGL: M = Proj * View
    s_vpMatrix = proj * view;
    s_screenWidth = screenWidth;
    s_screenHeight = screenHeight;
    s_nearClip = nearClip;
}

bool DebugDraw3D::ProjectToScreen(const Point3D& worldPos, float& outScreenX, float& outScreenY) noexcept {
    // -------------------------------------------------------------------------
    // ALGORITMO: Proyección Perspectiva Homogénea (Column-Major Matrix Multiplication)
    // Fuente: Jason Gregory (Game Engine Architecture 3rd Ed., Cap. 10.1 "The Rendering Pipeline")
    // -------------------------------------------------------------------------
    // Los elementos de s_vpMatrix se almacenan por columnas:
    // [ m0  m4  m8  m12 ]   [ x ]   [ cx ]
    // [ m1  m5  m9  m13 ] * [ y ] = [ cy ]
    // [ m2  m6  m10 m14 ]   [ z ]   [ cz ]
    // [ m3  m7  m11 m15 ]   [ 1 ]   [ cw ]
    float cx = s_vpMatrix.m[0] * worldPos.x + s_vpMatrix.m[4] * worldPos.y + s_vpMatrix.m[8]  * worldPos.z + s_vpMatrix.m[12];
    float cy = s_vpMatrix.m[1] * worldPos.x + s_vpMatrix.m[5] * worldPos.y + s_vpMatrix.m[9]  * worldPos.z + s_vpMatrix.m[13];
    float cw = s_vpMatrix.m[3] * worldPos.x + s_vpMatrix.m[7] * worldPos.y + s_vpMatrix.m[11] * worldPos.z + s_vpMatrix.m[15];

    // GUARDA DE SEGURIDAD (James Blinn Near-Plane Clipping Guard):
    // Si cw <= nearClip (punto en o detrás del plano anterior de corte de la cámara),
    // la división de perspectiva generaría singularidades (división por cero) o puntos
    // reflejados detrás de la escena. Se descarta de inmediato sin alterar el estado GPU.
    if (cw <= s_nearClip) {
        return false;
    }

    // Normalización de Coordenadas de Dispositivo Normalizadas (Normalized Device Coordinates - NDC)
    float invW = 1.0f / cw;
    float ndcX = cx * invW;
    float ndcY = cy * invW;

    // Descarte de Frustum Fraccionario (Culling defensivo en NDC amplio [-4.0, 4.0])
    // Permite que lineas que atraviesan la pantalla no se descarten si un extremo sale del campo de vision
    if (ndcX < -4.0f || ndcX > 4.0f || ndcY < -4.0f || ndcY > 4.0f) {
        return false;
    }

    // Mapeo a Coordenadas de Pantalla (Viewport Transform):
    // En NDC, Y positivo apunta hacia arriba; en la pantalla de Wii U / 2D, Y positivo apunta hacia abajo.
    outScreenX = (ndcX * 0.5f + 0.5f) * s_screenWidth;
    outScreenY = (0.5f - ndcY * 0.5f) * s_screenHeight;
    return true;
}

void DebugDraw3D::DrawLine(const Point3D& a, const Point3D& b, float width, const ColorRGBA& color) noexcept {
    if (width <= 0.0f) return;
    // -------------------------------------------------------------------------
    // ALGORITMO: Recorte de Segmentos contra el Plano Anterior (Near-Plane Line Clipping)
    // Fuente: James Blinn (1991), "A Trip Down the Graphics Pipeline: Line Clipping"
    // -------------------------------------------------------------------------
    float cwa = s_vpMatrix.m[3] * a.x + s_vpMatrix.m[7] * a.y + s_vpMatrix.m[11] * a.z + s_vpMatrix.m[15];
    float cwb = s_vpMatrix.m[3] * b.x + s_vpMatrix.m[7] * b.y + s_vpMatrix.m[11] * b.z + s_vpMatrix.m[15];

    // Si ambos extremos están detrás del plano anterior de corte, descartar inmediatamente
    if (cwa <= s_nearClip && cwb <= s_nearClip) {
        return;
    }

    Point3D pa = a;
    Point3D pb = b;

    // Recortar punto 'a' si penetra el plano anterior
    if (cwa <= s_nearClip) {
        float t = (s_nearClip + 0.001f - cwa) / (cwb - cwa);
        pa = Point3D{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    }
    // Recortar punto 'b' si penetra el plano anterior
    else if (cwb <= s_nearClip) {
        float t = (s_nearClip + 0.001f - cwa) / (cwb - cwa);
        pb = Point3D{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    }

    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    if (ProjectToScreen(pa, x0, y0) && ProjectToScreen(pb, x1, y1)) {
        Render2D::DrawLine(static_cast<int>(std::lround(x0)),
                           static_cast<int>(std::lround(y0)),
                           static_cast<int>(std::lround(x1)),
                           static_cast<int>(std::lround(y1)),
                           color,
                           width);
    }
}

void DebugDraw3D::DrawAABB(const Point3D& boxMin, const Point3D& boxMax, float width, const ColorRGBA& color) noexcept {
    // -------------------------------------------------------------------------
    // ALGORITMO: Extracción de 8 Vértices y 12 Aristas de una Caja AABB
    // Fuente: Christer Ericson (Real-Time Collision Detection, Cap. 4.2 "AABBs")
    // -------------------------------------------------------------------------
    Point3D c[8] = {
        {boxMin.x, boxMin.y, boxMin.z}, // 0: Base inferior min
        {boxMax.x, boxMin.y, boxMin.z}, // 1: Base inferior x-max
        {boxMax.x, boxMin.y, boxMax.z}, // 2: Base inferior max
        {boxMin.x, boxMin.y, boxMax.z}, // 3: Base inferior z-max
        {boxMin.x, boxMax.y, boxMin.z}, // 4: Tapa superior min
        {boxMax.x, boxMax.y, boxMin.z}, // 5: Tapa superior x-max
        {boxMax.x, boxMax.y, boxMax.z}, // 6: Tapa superior max
        {boxMin.x, boxMax.y, boxMax.z}  // 7: Tapa superior z-max
    };

    // 4 Aristas de la Base Inferior (Y min)
    DrawLine(c[0], c[1], width, color);
    DrawLine(c[1], c[2], width, color);
    DrawLine(c[2], c[3], width, color);
    DrawLine(c[3], c[0], width, color);

    // 4 Aristas de la Tapa Superior (Y max)
    DrawLine(c[4], c[5], width, color);
    DrawLine(c[5], c[6], width, color);
    DrawLine(c[6], c[7], width, color);
    DrawLine(c[7], c[4], width, color);

    // 4 Aristas Verticales de Unión
    DrawLine(c[0], c[4], width, color);
    DrawLine(c[1], c[5], width, color);
    DrawLine(c[2], c[6], width, color);
    DrawLine(c[3], c[7], width, color);
}

void DebugDraw3D::DrawBox(const Point3D& center, const Point3D& halfExtents, float width, const ColorRGBA& color) noexcept {
    Point3D bMin = center - halfExtents;
    Point3D bMax = center + halfExtents;
    DrawAABB(bMin, bMax, width, color);
}

void DebugDraw3D::DrawCircleXZ(const Point3D& center, float radius, float yOffset, float width, const ColorRGBA& color, int segments) noexcept {
    // Acotar segmentos para prevenir saturación de la cola de vértices 2D
    if (segments < 4) segments = 4;
    if (segments > 64) segments = 64;

    constexpr float TWO_PI = 6.28318530718f;
    float step = TWO_PI / static_cast<float>(segments);
    Point3D prev{center.x + radius, center.y + yOffset, center.z};

    for (int i = 1; i <= segments; ++i) {
        float angle = static_cast<float>(i) * step;
        Point3D curr{
            center.x + radius * std::cos(angle),
            center.y + yOffset,
            center.z + radius * std::sin(angle)
        };
        DrawLine(prev, curr, width, color);
        prev = curr;
    }
}

void DebugDraw3D::DrawCylinder(const Point3D& baseCenter, float radius, float height, float width, const ColorRGBA& color, int segments) noexcept {
    // Trazar los tres anillos de referencia del cilindro: base, cintura media y tapa superior
    DrawCircleXZ(baseCenter, radius, 0.0f, width, color, segments);
    DrawCircleXZ(baseCenter, radius, height * 0.5f, width, color, segments);
    DrawCircleXZ(baseCenter, radius, height, width, color, segments);

    // Trazar 4 generatrices verticales en ángulos cuadrantes (0°, 90°, 180°, 270°)
    for (int k = 0; k < 4; ++k) {
        float ang = static_cast<float>(k) * 1.57079632679f;
        float cosA = std::cos(ang);
        float sinA = std::sin(ang);
        Point3D pBot{baseCenter.x + radius * cosA, baseCenter.y, baseCenter.z + radius * sinA};
        Point3D pTop{baseCenter.x + radius * cosA, baseCenter.y + height, baseCenter.z + radius * sinA};
        DrawLine(pBot, pTop, width, color);
    }
}

void DebugDraw3D::DrawSphere(const Point3D& center, float radius, float width, const ColorRGBA& color, int segments) noexcept {
    if (segments < 4) segments = 4;
    if (segments > 64) segments = 64;

    // 1. Círculo plano XZ (Ecuador horizontal)
    DrawCircleXZ(center, radius, 0.0f, width, color, segments);

    // 2. Círculo plano XY (Meridiano frontal)
    constexpr float TWO_PI = 6.28318530718f;
    float step = TWO_PI / static_cast<float>(segments);
    Point3D prevXY{center.x + radius, center.y, center.z};
    for (int i = 1; i <= segments; ++i) {
        float angle = static_cast<float>(i) * step;
        Point3D currXY{
            center.x + radius * std::cos(angle),
            center.y + radius * std::sin(angle),
            center.z
        };
        DrawLine(prevXY, currXY, width, color);
        prevXY = currXY;
    }

    // 3. Círculo plano YZ (Meridiano lateral)
    Point3D prevYZ{center.x, center.y + radius, center.z};
    for (int i = 1; i <= segments; ++i) {
        float angle = static_cast<float>(i) * step;
        Point3D currYZ{
            center.x,
            center.y + radius * std::cos(angle),
            center.z + radius * std::sin(angle)
        };
        DrawLine(prevYZ, currYZ, width, color);
        prevYZ = currYZ;
    }
}

void DebugDraw3D::DrawArrow(const Point3D& start, const Point3D& dir, float length, float width, const ColorRGBA& color) noexcept {
    // -------------------------------------------------------------------------
    // ALGORITMO: Construcción de Cabezal de Flecha Tridimensional por Producto Vectorial
    // Fuente: Christer Ericson (Cap. 2.2 "Vectors and Points"), Jason Gregory (Cap. 15.2)
    // -------------------------------------------------------------------------
    Point3D end{start.x + dir.x * length, start.y + dir.y * length, start.z + dir.z * length};
    DrawLine(start, end, width, color);

    // Longitud proporcional de la púa (28% de la longitud total, acotada a 0.45m máx)
    float headLen = length * 0.28f;
    if (headLen > 0.45f) headLen = 0.45f;
    Point3D back{end.x - dir.x * headLen, end.y - dir.y * headLen, end.z - dir.z * headLen};

    // Vector auxiliar 'up' para generar la base ortogonal
    Point3D up{0.0f, 1.0f, 0.0f};
    if (std::abs(dir.y) > 0.9f) {
        up = Point3D{1.0f, 0.0f, 0.0f}; // Evitar paralelismo degenerado en producto cruz
    }

    // side = dir x up
    Point3D side{
        dir.y * up.z - dir.z * up.y,
        dir.z * up.x - dir.x * up.z,
        dir.x * up.y - dir.y * up.x
    };
    float sideLen = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
    if (sideLen > 1e-4f) {
        float inv = (headLen * 0.55f) / sideLen;
        side.x *= inv;
        side.y *= inv;
        side.z *= inv;

        // Dos púas simétricas
        Point3D barb1{back.x + side.x, back.y + side.y, back.z + side.z};
        Point3D barb2{back.x - side.x, back.y - side.y, back.z - side.z};
        DrawLine(end, barb1, width, color);
        DrawLine(end, barb2, width, color);
    }
}

void DebugDraw3D::DrawRay(const Point3D& start, const Point3D& dirAndLength, float width, const ColorRGBA& color) noexcept {
    Point3D end{start.x + dirAndLength.x, start.y + dirAndLength.y, start.z + dirAndLength.z};
    DrawLine(start, end, width, color);
}

} // namespace Neleo
