#pragma once

#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"

namespace Neleo {

/**
 * @brief Subsistema Universal de Renderizado Inmediato de Depuración 3D (Debug Wireframe Renderer).
 * 
 * @author Wii U Game Engine Architecture Team
 * @date Noviembre 2026 (Hito 119)
 * 
 * @par Fundamento Teórico y Literatura de Referencia:
 * - **Jason Gregory** (*Game Engine Architecture*, 3ª Edición, Cap. 13 "Rendering Systems" & Cap. 15.2 "Debug Facilities").
 * - **James Blinn** (*"A Trip Down the Graphics Pipeline: Line Clipping"*, IEEE CG&A 1991).
 * - **Christer Ericson** (*Real-Time Collision Detection*, Morgan Kaufmann 2004, Cap. 13 "Geometrical Robustness").
 * 
 * @par Filosofía y Rendimiento en Silicio (Nintendo Wii U):
 * - **Cero Asignaciones Dinámicas (0 Heap Allocs a 60 FPS):** Emite vértices directamente hacia el búfer estático
 *   pre-reservado de 48,000 vértices de `Render2DGX2`.
 * - **Desacoplamiento Absoluto:** Funciona de manera estática y universal para cualquier escena del motor (heredera de `GameScene3D`).
 * - **Guarda de Seguridad de Plano Anterior (Near-Plane Clip):** Descarta analíticamente puntos con $cw \le 0.15\text{ m}$
 *   para erradicar divisiones por cero, singularidades detrás de la cámara y coordenadas NaN en los rasterizadores de Latte GX2.
 */
class DebugDraw3D {
public:
    /**
     * @brief Configura la matriz combinada View-Projection para la proyección de primitivas en el frame actual.
     * @param vpMatrix Matriz precalculada $M = \text{Proj} \times \text{View}$ en convención column-major.
     * @param screenWidth Ancho del lienzo virtual en píxeles físicos (por defecto 1280.0f).
     * @param screenHeight Alto del lienzo virtual en píxeles físicos (por defecto 720.0f).
     * @param nearClip Distancia de corte del plano anterior para descarte de seguridad (por defecto 0.15m).
     */
    static void SetViewProjection(const Matrix4x4& vpMatrix, 
                                  float screenWidth = 1280.0f, 
                                  float screenHeight = 720.0f, 
                                  float nearClip = 0.15f) noexcept;

    /**
     * @brief Configura las matrices de vista y proyección independientes, calculando su producto internamente.
     * @param view Matriz de vista de la cámara orbital o cinemática (World Space -> View Space).
     * @param proj Matriz de proyección en perspectiva (View Space -> Clip Space, ángulos en radianes).
     * @param screenWidth Ancho del lienzo en píxeles (1280.0f).
     * @param screenHeight Alto del lienzo en píxeles (720.0f).
     * @param nearClip Distancia mínima de seguridad en Z para recorte de primitivas (0.15m).
     */
    static void SetMatrices(const Matrix4x4& view, 
                            const Matrix4x4& proj, 
                            float screenWidth = 1280.0f, 
                            float screenHeight = 720.0f, 
                            float nearClip = 0.15f) noexcept;

    /**
     * @brief Proyecta analíticamente una coordenada 3D en espacio de mundo hacia coordenadas de pantalla 2D.
     * 
     * @param worldPos Posición tridimensional en el mundo $(x, y, z)$.
     * @param[out] outScreenX Coordenada horizontal resultante en espacio de pantalla $[0, \text{screenWidth}]$.
     * @param[out] outScreenY Coordenada vertical resultante en espacio de pantalla $[0, \text{screenHeight}]$ (origen superior-izquierdo).
     * @return true si el punto es visible dentro del volumen de visión y delante del plano anterior; false si se descartó.
     * 
     * @note Aplica división de perspectiva por la coordenada homogénea $cw$. Si $cw \le nearClip$, el punto se descarta
     * para prevenir divisiones por cero y artefactos de proyección invertida en el rasterizador de silicio.
     */
    static bool ProjectToScreen(const Point3D& worldPos, float& outScreenX, float& outScreenY) noexcept;

    /**
     * @brief Dibuja un segmento de línea recta 3D entre dos puntos del espacio de mundo.
     * @param a Extremo inicial del segmento en coordenadas de mundo.
     * @param b Extremo final del segmento en coordenadas de mundo.
     * @param width Grosor de rasterizado de la línea en píxeles (típicamente 1.5f a 4.0f).
     * @param color Tinte cromático RGBA (soporta opacidad alfa).
     */
    static void DrawLine(const Point3D& a, const Point3D& b, float width, const ColorRGBA& color) noexcept;

    /**
     * @brief Dibuja una caja envolvente alineada a los ejes coordenados del mundo (Axis-Aligned Bounding Box - AABB).
     * 
     * Genera las 12 aristas de la caja (4 en base inferior, 4 en tapa superior y 4 pilares verticales de unión).
     * 
     * @param boxMin Esquina mínima de la caja $(\min_x, \min_y, \min_z)$ en espacio de mundo.
     * @param boxMax Esquina máxima de la caja $(\max_x, \max_y, \max_z)$ en espacio de mundo.
     * @param width Grosor de línea en pantalla.
     * @param color Color cromático de las aristas.
     */
    static void DrawAABB(const Point3D& boxMin, const Point3D& boxMax, float width, const ColorRGBA& color) noexcept;

    /**
     * @brief Dibuja una caja 3D orientada por centro geométrico y semiextensiones.
     * @param center Centro geométrico de la caja en el mundo.
     * @param halfExtents Distancias desde el centro a cada cara $(\Delta x, \Delta y, \Delta z)$.
     * @param width Grosor de línea.
     * @param color Tinte RGBA.
     */
    static void DrawBox(const Point3D& center, const Point3D& halfExtents, float width, const ColorRGBA& color) noexcept;

    /**
     * @brief Dibuja una esfera analítica mediante 3 anillos circulares ortogonales correspondientes a sus meridianos.
     * 
     * Traza el ecuador en el plano horizontal $XZ$, el meridiano frontal en el plano $XY$ y el meridiano lateral en $YZ$.
     * 
     * @param center Centro tridimensional de la esfera en espacio de mundo.
     * @param radius Radio métrico de la esfera ($r > 0$).
     * @param width Grosor de rasterizado en píxeles.
     * @param color Color de visualización (ej. verde en reposo, rojo brillante en colisión).
     * @param segments Cantidad de subdivisiones angulares por círculo (entre 4 y 64; recomendado 16 a 20).
     */
    static void DrawSphere(const Point3D& center, float radius, float width, const ColorRGBA& color, int segments = 16) noexcept;

    /**
     * @brief Dibuja un anillo circular horizontal paralelo al plano XZ con elevación vertical opcional.
     * @param center Centro horizontal de referencia.
     * @param radius Radio métrico del anillo.
     * @param yOffset Desplazamiento vertical en el eje Y respecto al centro (útil para anillos de triggers en el suelo).
     * @param width Grosor de línea.
     * @param color Color cromático.
     * @param segments Cantidad de segmentos de aproximación lineal.
     */
    static void DrawCircleXZ(const Point3D& center, float radius, float yOffset, float width, const ColorRGBA& color, int segments = 16) noexcept;

    /**
     * @brief Dibuja un cilindro vertical envolvente (3 círculos horizontales y 4 generatrices ortogonales).
     * 
     * Ideal para representar pilares de piedra, árboles, cápsulas de personajes y obstáculos cilíndricos.
     * 
     * @param baseCenter Centro de la base inferior del cilindro.
     * @param radius Radio cilíndrico métrico.
     * @param height Altura total vertical en el eje Y positivo.
     * @param width Grosor de aristas.
     * @param color Color cromático de trazado.
     * @param segments Cantidad de subdivisiones circulares (por defecto 16).
     */
    static void DrawCylinder(const Point3D& baseCenter, float radius, float height, float width, const ColorRGBA& color, int segments = 16) noexcept;

    /**
     * @brief Dibuja un vector direccional 3D terminado en una punta de flecha cónica ortogonal.
     * 
     * @author James Blinn / Jason Gregory (Cap. 15.2).
     * @param start Punto de origen del vector (ej. centro del colisionador o punto de contacto).
     * @param dir Vector direccional normalizado indicando la dirección de empuje o velocidad.
     * @param length Longitud métrica total de la flecha.
     * @param width Grosor del trazo en píxeles.
     * @param color Color de la flecha (ej. amarillo brillante para el vector de deslizamiento MTV).
     * 
     * @note El cabezal de la flecha se calcula mediante el producto vectorial $\mathbf{side} = \mathbf{dir} \times \mathbf{up}$,
     * garantizando púas tridimensionales visibles desde cualquier ángulo de cámara.
     */
    static void DrawArrow(const Point3D& start, const Point3D& dir, float length, float width, const ColorRGBA& color) noexcept;

    /**
     * @brief Dibuja un rayo de inspección directa sumando inicio y vector total de desplazamiento.
     * @param start Origen del rayo en espacio de mundo.
     * @param dirAndLength Vector que encapsula dirección y magnitud del rayo simultáneamente.
     * @param width Grosor del rayo en pantalla.
     * @param color Color de visualización.
     */
    static void DrawRay(const Point3D& start, const Point3D& dirAndLength, float width, const ColorRGBA& color) noexcept;
};

} // namespace Neleo
