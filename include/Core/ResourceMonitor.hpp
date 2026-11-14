#pragma once

#include "Graphics/Color.hpp"
#include <cstdint>
#include <cstddef>
#include <SDL2/SDL.h>

namespace WiiU {

/**
 * @brief Métricas de telemetría de hardware y consumo de recursos de la máquina.
 */
struct ResourceMetrics {
    float cpuPercent{0.0f};        // Uso de CPU en relación al presupuesto VSync (16.667 ms = 100%)
    float cpuTimeMs{0.0f};         // Tiempo de CPU por frame en milisegundos
    float gpuPercent{0.0f};        // Uso de GPU en relación al presupuesto VSync (16.667 ms = 100%)
    float gpuTimeMs{0.0f};         // Tiempo de renderizado/presentación GPU en milisegundos
    float mem1Percent{0.0f};       // Porcentaje de VRAM / eDRAM activo (sobre presupuesto de 32 MB)
    size_t mem1Bytes{0};           // Bytes ocupados por buffers y texturas GPU
    float mem2Percent{0.0f};       // Porcentaje de Heap Cafe OS (sobre presupuesto app de 1024 MB)
    size_t mem2Bytes{0};           // Bytes asignados en heap de sistema
    float frameArenaPercent{0.0f}; // Porcentaje consumido de LinearFrameArena (sobre 128 KB)
    size_t frameArenaBytes{0};     // Bytes consumidos en Frame Arena
    float sceneArenaPercent{0.0f}; // Porcentaje consumido de SceneArena (sobre 2 MB)
    size_t sceneArenaBytes{0};     // Bytes consumidos en Scene Arena
    float audioPercent{0.0f};      // Porcentaje de voces de audio activas (sobre 32 canales)
    uint32_t activeVoices{0};      // Conteo de voces activas en Voice Pool
    float rhiBypassPercent{0.0f};  // Eficiencia del RHIPipelineCache (bypasses vs solicitudes de estado)
    uint32_t rhiBypasses{0};       // Total de cambios redundantes evitados en silicio Latte
    uint32_t rhiStateChanges{0};   // Total de cambios reales de registro de silicio
    float fps{60.0f};              // Tasa de refresco / fotogramas por segundo
};

/**
 * @brief Monitor Global de Recursos y Telemetría de Silicio para Nintendo Wii U y Framework.
 * 
 * Se activa y desactiva en cualquier momento con el botón "-" (Minus / Select):
 * - GamePad: VPAD_BUTTON_MINUS
 * - Pro Controller: WPAD_PRO_BUTTON_MINUS / WPAD_BUTTON_MINUS
 * - Teclado PC / Host: SDLK_MINUS, SDLK_KP_MINUS o SDLK_TAB
 * - Mandos USB / SDL: SDL_CONTROLLER_BUTTON_BACK
 * 
 * Características:
 * - Capa superpuesta global (Overlay OSD) renderizada en silicio Latte (GX2) o SDL2.
 * - Mide con precisión sub-microsegundo el tiempo de CPU y GPU.
 * - Reporta porcentajes de MEM1 (eDRAM VRAM), MEM2 (Heap Cafe OS), Arenas y Audio.
 * - Cero alocaciones dinámicas en caliente (0 bytes malloc/new a 60 FPS).
 */
class ResourceMonitor {
public:
    static ResourceMonitor& Instance() noexcept {
        static ResourceMonitor instance;
        return instance;
    }

    // Alterna la visibilidad del monitor OSD
    void Toggle() noexcept { m_visible = !m_visible; }
    void SetVisible(bool visible) noexcept { m_visible = visible; }
    [[nodiscard]] bool IsVisible() const noexcept { return m_visible; }

    // Puntos de muestreo de tiempo por frame
    void BeginFrame();
    void MarkCPUEnd();
    void MarkGPUEnd();
    void Update(float dt);

    // Renderizado OSD en pantalla sobre cualquier escena
    void Render(SDL_Renderer* renderer, bool isPureGX2);

    [[nodiscard]] const ResourceMetrics& GetMetrics() const noexcept { return m_metrics; }

private:
    ResourceMonitor() noexcept;
    ~ResourceMonitor() = default;

    ResourceMonitor(const ResourceMonitor&) = delete;
    ResourceMonitor& operator=(const ResourceMonitor&) = delete;
    ResourceMonitor(ResourceMonitor&&) = delete;
    ResourceMonitor& operator=(ResourceMonitor&&) = delete;

    void SampleMetrics();

    bool m_visible{false};
    ResourceMetrics m_metrics{};

    Uint64 m_frameStartCounter{0};
    Uint64 m_cpuEndCounter{0};
    Uint64 m_gpuEndCounter{0};
    Uint64 m_perfFreq{1};

    float m_smoothedCpuMs{0.0f};
    float m_smoothedGpuMs{0.0f};
    float m_smoothedFps{60.0f};
    float m_sampleTimer{0.0f};
    float m_pulseAnim{0.0f};
};

} // namespace WiiU
