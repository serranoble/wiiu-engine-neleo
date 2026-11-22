#pragma once

#include "Graphics/Math3D.hpp"
#include "Core/ECS/Entity.hpp"
#include <cstdint>

namespace WiiU {

/**
 * @brief Capas de filtrado de colisión mediante máscara de bits (Bitmask Collision Filtering).
 * 
 * @par Literatura de Referencia:
 * - **Jason Gregory** (*Game Engine Architecture*, 3ª Edición, Cap. 13.7.4 "Collision Filtering").
 * - **Christer Ericson** (*Real-Time Collision Detection*, Morgan Kaufmann 2004, Cap. 1.5).
 */
enum CollisionLayer : uint16_t {
    Layer_None        = 0,
    Layer_Player      = 1 << 0, ///< Capa asignada al personaje controlable por el jugador.
    Layer_StaticWorld = 1 << 1, ///< Capa para la arquitectura estática inamovible (muros, casas, pilares).
    Layer_Trigger     = 1 << 2, ///< Capa para volúmenes detectores no sólidos.
    Layer_Collectible = 1 << 3, ///< Capa para orbes, balizas y objetos recolectables.
    Layer_DynamicBody = 1 << 4, ///< Capa para cuerpos rígidos dinámicos y articulaciones físicas (Cap. 14).
    Layer_All         = 0xFFFF  ///< Máscara universal que colisiona con todas las capas.
};

/**
 * @brief Primitivas analíticas de colisión soportadas en tiempo real.
 */
enum class ColliderShape : uint8_t {
    Sphere = 0, ///< Esfera analítica delimitada por centro y radio métrico escalar.
    AABB   = 1  ///< Caja delimitadora alineada a los ejes coordenados (Axis-Aligned Bounding Box).
};

/**
 * @brief [Hito 116] Componente ECS de Colisión Física (ColliderComponent).
 * 
 * Estructura POD contigua alineada a línea de caché L1D (32 bytes) del procesador IBM Espresso PPC 750CL:
 * - Soporta volúmenes Esféricos (radio + centro local) y Cajas AABB (semi-extensiones + centro local).
 * - Define si el cuerpo es estático (inamovible) o cinemático móvil (controlado por el jugador).
 * - Registra telemetría de contacto del frame actual (vector normal y penetración) con CERO alocaciones de heap.
 * 
 * @par Algoritmos de Contacto y Resolución:
 * - Esfera vs Esfera: Distancia euclídea al cuadrado (Christer Ericson, Cap. 4.3.1).
 * - Esfera vs AABB: Algoritmo de James Arvo (Graphics Gems 1990) y Christer Ericson (Cap. 5.1.3).
 */
struct alignas(32) ColliderComponent {
    Point3D localOffset{0.0f, 0.0f, 0.0f};      ///< Desplazamiento local respecto a `TransformComponent::position`.
    Point3D halfExtents{0.5f, 0.5f, 0.5f};      ///< Semiextensiones $(\Delta x, \Delta y, \Delta z)$ cuando `shape == AABB`.
    float radius{0.5f};                         ///< Radio métrico de la esfera envolvente cuando `shape == Sphere`.

    ColliderShape shape{ColliderShape::Sphere}; ///< Forma geométrica analítica.
    bool isStatic{true};                        ///< true para muros/pilares inamovibles; false para actores dinámicos.
    bool isSolid{true};                         ///< true si resuelve penetración física mediante vector MTV (Slide response).

    uint16_t layer{Layer_StaticWorld};          ///< Capa a la que pertenece esta entidad.
    uint16_t collisionMask{Layer_All};          ///< Máscara de bits con qué capas interactúa físicamente.

    // -------------------------------------------------------------------------
    // TELEMETRÍA DE CONTACTO RESUELTO EN EL FRAME ACTUAL (Sin costo de memoria)
    // -------------------------------------------------------------------------
    bool isColliding{false};                    ///< true si la entidad sufrió una colisión en el frame actual.
    Point3D contactNormal{0.0f, 1.0f, 0.0f};    ///< Vector normal de expulsión (MTV Normal) normalizado en el plano de impacto.
    float penetrationDepth{0.0f};               ///< Profundidad de penetración geométrica resuelta en metros.
    Entity collidedEntity{Entity::Null};        ///< Identificador de la entidad con la que se entró en contacto físico.
    const char* tag{"Untagged"};                ///< Etiqueta de depuración para visualización en HUD / consola.

    constexpr ColliderComponent() noexcept = default;

    /**
     * @brief Factoría estática para construir un colisionador esférico.
     * @param rad Radio de la esfera en metros ($r > 0$).
     * @param offset Desplazamiento respecto al pivote de la entidad.
     * @param staticBody true para obstáculos fijos del mundo; false para actores móviles.
     * @param ownLayer Capa de colisión asignada.
     * @param debugTag Etiqueta textual para diagnóstico en telemetría.
     */
    static constexpr ColliderComponent MakeSphere(float rad, const Point3D& offset,
                                                  bool staticBody, uint16_t ownLayer,
                                                  const char* debugTag = "Sphere") noexcept {
        ColliderComponent c{};
        c.localOffset = offset;
        c.radius = rad;
        c.shape = ColliderShape::Sphere;
        c.isStatic = staticBody;
        c.isSolid = true;
        c.layer = ownLayer;
        c.tag = debugTag;
        return c;
    }

    /**
     * @brief Factoría estática para construir un colisionador de caja AABB.
     * @param extents Semiextensiones $(\Delta x, \Delta y, \Delta z)$ desde el centro de la caja.
     * @param offset Desplazamiento respecto al pivote de la entidad.
     * @param staticBody true para arquitectura fija; false para cajas móviles.
     * @param ownLayer Capa de colisión asignada.
     * @param debugTag Etiqueta textual para diagnóstico.
     */
    static constexpr ColliderComponent MakeAABB(const Point3D& extents, const Point3D& offset,
                                                bool staticBody, uint16_t ownLayer,
                                                const char* debugTag = "AABB") noexcept {
        ColliderComponent c{};
        c.localOffset = offset;
        c.halfExtents = extents;
        c.shape = ColliderShape::AABB;
        c.isStatic = staticBody;
        c.isSolid = true;
        c.layer = ownLayer;
        c.tag = debugTag;
        return c;
    }
};

} // namespace WiiU
