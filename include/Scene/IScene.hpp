#pragma once

#include <vector>
#include <string>

namespace Neleo {

class IScene {
public:
    virtual ~IScene() = default;
    // Pilar 2 (Jason Gregory Cap. 7): Fase asíncrona pesada de E/S y staging ejecutada en Core 2
    // mientras Core 1 mantiene el bucle de renderizado de la pantalla de transición a 60 FPS.
    virtual void LoadAsync() {}
    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void OnPause() {}
    virtual void OnResume() {}
    virtual void Update(float dt) = 0;
    virtual void Render() = 0;

    // Retorna la lista de assets requeridos para la escena (por defecto lista vacía).
    [[nodiscard]] virtual std::vector<std::string> GetRequiredAssets() const {
        return {};
    }

    // Nombre legible de la escena para mensajes informativos, logs y pantallas de error.
    [[nodiscard]] virtual std::string GetSceneName() const {
        return "Escena";
    }

    // Todas las escenas renderizan de forma nativa sobre GX2 en Wii U
    [[nodiscard]] virtual bool IsPureGX2() const noexcept {
        return true;
    }

    // Indica si la escena limpia por sí misma el ColorBuffer principal (evita doble Clear en eDRAM)
    [[nodiscard]] virtual bool ClearsFramebuffer() const noexcept {
        return false;
    }

    // Retorna si la escena es un menú o interfaz del sistema (donde suena la música de fondo).
    [[nodiscard]] virtual bool IsMenu() const {
        return false;
    }
};

} // namespace Neleo
