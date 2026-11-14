#pragma once

#include "Graphics/Math3D.hpp"
#include "Core/ECS/Entity.hpp"
#include <cstdint>

namespace WiiU {

/**
 * @brief Tipos semánticos de volumen Trigger espacial.
 * 
 * @par Literatura de Referencia:
 * - **Jason Gregory** (*Game Engine Architecture*, 3ª Edición, Cap. 13.7.5 & Cap. 16 "Gameplay Foundation & Triggers").
 */
enum class TriggerType : uint8_t {
    Collectible = 0, ///< Baliza, moneda u orbe recolectable (se consume tras el primer contacto).
    Zone        = 1, ///< Zona espacial continua (ej. Altar de meta, área de evento, porche de casa).
    BouncePad   = 2  ///< Plataforma impulsora que inyecta velocidad vertical instantánea al actor.
};

/**
 * @brief [Hito 116] Componente ECS de Volumen Disparador de Eventos (TriggerComponent).
 * 
 * Detecta solapamiento espacial con actores dinámicos (jugador/NPCs) sin bloquear su paso físico.
 * Mantiene un autómata de estados de flanco (`OnTriggerEnter`, `OnTriggerStay`, `OnTriggerExit`)
 * con CERO punteros virtuales en heap y CERO alocaciones dinámicas a 60 FPS.
 */
struct TriggerComponent {
    Point3D localOffset{0.0f, 0.0f, 0.0f};      ///< Centro del volumen relativo a `TransformComponent::position`.
    float radius{1.0f};                         ///< Radio de detección esférico en metros.
    float bounceImpulse{10.5f};                 ///< Velocidad vertical $v_y$ inyectada cuando `type == BouncePad`.

    TriggerType type{TriggerType::Collectible}; ///< Tipología semántica del trigger.
    uint32_t triggerId{0};                      ///< Identificador numérico único para la lógica del nivel.
    bool isActive{true};                        ///< true si el trigger está activo para evaluar solapamientos.
    bool oneShot{true};                         ///< true si se desactiva permanentemente tras activarse una vez.

    // -------------------------------------------------------------------------
    // BANDERAS DE ESTADO DEL FLANCO TEMPORAL (Actualizadas por CollisionSystem)
    // -------------------------------------------------------------------------
    bool isInside{false};                       ///< true si el actor se encuentra actualmente dentro del volumen.
    bool wasInside{false};                      ///< true si el actor se encontraba dentro en el frame anterior.
    bool enteredThisFrame{false};               ///< true en el flanco de subida (OnTriggerEnter).
    bool exitedThisFrame{false};                ///< true en el flanco de bajada (OnTriggerExit).
    bool isCollected{false};                    ///< true si el trigger era de tipo Collectible y ya fue recogido.
    uint32_t activationCount{0};                ///< Contador acumulado de veces que fue activado.
    Entity triggeringEntity{Entity::Null};      ///< Entidad móvil que detonó el evento en el frame actual.
    const char* name{"Trigger"};                ///< Nombre legible para inspección y depuración en pantalla.

    constexpr TriggerComponent() noexcept = default;

    /**
     * @brief Constructor explícito con parámetros configurables.
     * @param t Tipología del trigger (Collectible, Zone, BouncePad).
     * @param rad Radio esférico de detección en metros.
     * @param offset Desplazamiento local respecto al centro del objeto.
     * @param singleUse true para consumo único (one-shot); false para zonas persistentes.
     * @param impulse Magnitud de velocidad de rebote (si aplica).
     * @param label Nombre identificativo del trigger.
     * @param id Identificador numérico para lógica de juego.
     */
    constexpr TriggerComponent(TriggerType t, float rad,
                               const Point3D& offset = Point3D{0.0f, 0.5f, 0.0f},
                               bool singleUse = true, float impulse = 0.0f,
                               const char* label = "Trigger", uint32_t id = 0) noexcept
        : localOffset(offset), radius(rad), bounceImpulse(impulse), type(t), triggerId(id),
          isActive(true), oneShot(singleUse), name(label) {}
};

} // namespace WiiU
