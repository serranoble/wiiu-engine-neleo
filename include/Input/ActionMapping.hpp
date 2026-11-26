#pragma once

#include "Input/VirtualGamepad.hpp"
#include <cstdint>

namespace Neleo {

/**
 * @brief [Jason Gregory Cap. 8.3 & 8.5: "Action Mapping & Context Decoupling"]
 * Acciones semánticas abstractas del juego, independientes del tipo de hardware.
 */
enum class InputAction : uint16_t {
    MoveX = 0,          // Desplazamiento lateral (Izquierda/Derecha)
    MoveY = 1,          // Desplazamiento longitudinal (Adelante/Atrás)
    LookX = 2,          // Órbita horizontal de cámara
    LookY = 3,          // Órbita vertical de cámara
    ZoomIn = 4,         // Acercamiento de cámara (ZR / R / Gatillos)
    ZoomOut = 5,        // Alejamiento de cámara (ZL / L / Gatillos)
    Jump = 6,           // Salto o acción principal (Botón A / Botón 2)
    Interact = 7,       // Interacción o acción secundaria (Botón Y / Botón 1)
    Action1 = 8,        // Disparador 1 (Clip Salto)
    Action2 = 9,        // Disparador 2 (Clip Piar)
    Action3 = 10,       // Disparador 3 (Clip Mirar)
    Action4 = 11,       // Disparador 4 (Clip Todo)
    Pause = 12,         // Menú de Pausa / Opciones (Botón + / Start / P)
    Back = 13,          // Regreso / Cancelar (Botón B / Escape)
    ToggleTelemetry = 14, // Conmutar monitor de telemetría (Botón - / Select)
    ResetCamera = 15     // Centrado de cámara (R3 / Stick R)
};

/**
 * @brief Contexto activo de control para filtrado y enrutamiento jerárquico.
 */
enum class InputContext : uint8_t {
    Gameplay3D = 0,     // Control completo de avatar y física en mundo 3D
    UIModal = 1,        // Menús de opciones, cuadros de diálogo y modales de pausa
    Viewer3D = 2,       // Modo visor e inspección de modelos con cámara orbital
    DebugConsole = 3    // Terminal de desarrollo / CVars en vivo
};

[[nodiscard]] inline constexpr uint32_t ActionToBit(InputAction action) noexcept {
    return (static_cast<uint32_t>(action) < 32u) ? (1u << static_cast<uint32_t>(action)) : 0u;
}

/**
 * @brief Mapeador canónico de acciones lógicas a partir de un VirtualGamepad.
 */
class ActionMap {
public:
    [[nodiscard]] static float GetAxis(const VirtualGamepad& pad, InputAction action) noexcept {
        switch (action) {
            case InputAction::MoveX:
                if (std::abs(pad.leftStick.x) > 0.001f) return pad.leftStick.x;
                if (pad.IsHeld(VirtualButton::DPadRight)) return 1.0f;
                if (pad.IsHeld(VirtualButton::DPadLeft)) return -1.0f;
                return 0.0f;

            case InputAction::MoveY:
                if (std::abs(pad.leftStick.y) > 0.001f) return pad.leftStick.y;
                if (pad.IsHeld(VirtualButton::DPadUp)) return 1.0f;
                if (pad.IsHeld(VirtualButton::DPadDown)) return -1.0f;
                return 0.0f;

            case InputAction::LookX:
                return pad.rightStick.x;

            case InputAction::LookY:
                return pad.rightStick.y;

            case InputAction::ZoomIn:
                if (pad.triggers.right > 0.01f) return pad.triggers.right;
                if (pad.IsHeld(VirtualButton::ZR) || pad.IsHeld(VirtualButton::R)) return 1.0f;
                return 0.0f;

            case InputAction::ZoomOut:
                if (pad.triggers.left > 0.01f) return pad.triggers.left;
                if (pad.IsHeld(VirtualButton::ZL) || pad.IsHeld(VirtualButton::L)) return 1.0f;
                return 0.0f;

            default:
                return IsActionHeld(pad, action) ? 1.0f : 0.0f;
        }
    }

    [[nodiscard]] static bool IsActionHeld(const VirtualGamepad& pad, InputAction action) noexcept {
        switch (action) {
            case InputAction::Jump:
                return pad.IsHeld(VirtualButton::A);
            case InputAction::Interact:
                return pad.IsHeld(VirtualButton::Y);
            case InputAction::Action1:
                return pad.IsHeld(VirtualButton::DPadUp) || pad.IsHeld(VirtualButton::X);
            case InputAction::Action2:
                return pad.IsHeld(VirtualButton::DPadDown) || pad.IsHeld(VirtualButton::Y);
            case InputAction::Action3:
                return pad.IsHeld(VirtualButton::DPadLeft) || pad.IsHeld(VirtualButton::L);
            case InputAction::Action4:
                return pad.IsHeld(VirtualButton::DPadRight) || pad.IsHeld(VirtualButton::R);
            case InputAction::Pause:
                return pad.IsHeld(VirtualButton::Plus);
            case InputAction::Back:
                return pad.IsHeld(VirtualButton::B);
            case InputAction::ToggleTelemetry:
                return pad.IsHeld(VirtualButton::Minus);
            case InputAction::ResetCamera:
                return pad.IsHeld(VirtualButton::StickR);
            default:
                return false;
        }
    }

    [[nodiscard]] static bool WasActionTriggered(const VirtualGamepad& pad, InputAction action) noexcept {
        switch (action) {
            case InputAction::Jump:
                return pad.WasJustPressed(VirtualButton::A);
            case InputAction::Interact:
                return pad.WasJustPressed(VirtualButton::Y);
            case InputAction::Action1:
                return pad.WasJustPressed(VirtualButton::DPadUp) || pad.WasJustPressed(VirtualButton::X);
            case InputAction::Action2:
                return pad.WasJustPressed(VirtualButton::DPadDown) || pad.WasJustPressed(VirtualButton::Y);
            case InputAction::Action3:
                return pad.WasJustPressed(VirtualButton::DPadLeft) || pad.WasJustPressed(VirtualButton::L);
            case InputAction::Action4:
                return pad.WasJustPressed(VirtualButton::DPadRight) || pad.WasJustPressed(VirtualButton::R);
            case InputAction::Pause:
                return pad.WasJustPressed(VirtualButton::Plus);
            case InputAction::Back:
                return pad.WasJustPressed(VirtualButton::B);
            case InputAction::ToggleTelemetry:
                return pad.WasJustPressed(VirtualButton::Minus);
            case InputAction::ResetCamera:
                return pad.WasJustPressed(VirtualButton::StickR);
            default:
                return false;
        }
    }
};

} // namespace Neleo
