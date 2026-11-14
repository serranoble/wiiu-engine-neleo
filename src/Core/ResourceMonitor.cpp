#include "Core/ResourceMonitor.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/StackString.hpp"
#include "Audio/AudioEngine3D.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include <algorithm>
#include <cmath>

namespace WiiU {

ResourceMonitor::ResourceMonitor() noexcept {
    m_perfFreq = SDL_GetPerformanceFrequency();
    if (m_perfFreq == 0) m_perfFreq = 1;
}

void ResourceMonitor::BeginFrame() {
    m_frameStartCounter = SDL_GetPerformanceCounter();
}

void ResourceMonitor::MarkCPUEnd() {
    m_cpuEndCounter = SDL_GetPerformanceCounter();
    if (m_frameStartCounter != 0 && m_cpuEndCounter >= m_frameStartCounter) {
        float instCpuMs = static_cast<float>(m_cpuEndCounter - m_frameStartCounter) * 1000.0f / static_cast<float>(m_perfFreq);
        if (instCpuMs > 100.0f) instCpuMs = 100.0f;
        m_smoothedCpuMs = m_smoothedCpuMs * 0.90f + instCpuMs * 0.10f;
    }
}

void ResourceMonitor::MarkGPUEnd() {
    m_gpuEndCounter = SDL_GetPerformanceCounter();
    if (m_cpuEndCounter != 0 && m_gpuEndCounter >= m_cpuEndCounter) {
        float instGpuMs = static_cast<float>(m_gpuEndCounter - m_cpuEndCounter) * 1000.0f / static_cast<float>(m_perfFreq);
        if (instGpuMs > 100.0f) instGpuMs = 100.0f;
        m_smoothedGpuMs = m_smoothedGpuMs * 0.90f + instGpuMs * 0.10f;
    }
}

void ResourceMonitor::Update(float dt) {
    if (dt > 0.0001f) {
        float instFps = 1.0f / dt;
        m_smoothedFps = m_smoothedFps * 0.90f + instFps * 0.10f;
    }
    m_pulseAnim += dt * 4.0f;

    m_sampleTimer += dt;
    if (m_sampleTimer >= 0.05f) { // Muestrear cada 50 ms
        m_sampleTimer = 0.0f;
        SampleMetrics();
    }
}

void ResourceMonitor::SampleMetrics() {
    constexpr float VSYNC_BUDGET_MS = 16.6667f; // 60 Hz budget

    // 1. Tiempos y Porcentajes de CPU y GPU
    m_metrics.cpuTimeMs = m_smoothedCpuMs;
    m_metrics.cpuPercent = std::clamp((m_metrics.cpuTimeMs / VSYNC_BUDGET_MS) * 100.0f, 0.0f, 100.0f);

    m_metrics.gpuTimeMs = m_smoothedGpuMs;
    m_metrics.gpuPercent = std::clamp((m_metrics.gpuTimeMs / VSYNC_BUDGET_MS) * 100.0f, 0.0f, 100.0f);

    m_metrics.fps = m_smoothedFps;

    // 2. Memoria en Silicio Wii U:
    // MEM1: 32 MB eDRAM embebida en GPU Latte (Color Buffers TV/DRC ~5.3 MB, Depth Buffer ~3.7 MB y Context States).
    auto& mem = MemoryManager::Instance();
    size_t depthBytes = mem.GetAllocatedBytesByTag(MemoryTag::SurfaceDepth);
    size_t ctxBytes   = mem.GetAllocatedBytesByTag(MemoryTag::ContextState);
    constexpr size_t COLOR_BUFFERS_EDRAM_ESTIMATE = static_cast<size_t>(5.32f * 1024.0f * 1024.0f);

    m_metrics.mem1Bytes = COLOR_BUFFERS_EDRAM_ESTIMATE + depthBytes + ctxBytes;
    constexpr float EDRAM_BUDGET = 32.0f * 1024.0f * 1024.0f; // 32 MB eDRAM
    m_metrics.mem1Percent = std::clamp((static_cast<float>(m_metrics.mem1Bytes) / EDRAM_BUDGET) * 100.0f, 0.0f, 100.0f);

    // MEM2: 1024 MB DDR3 Cafe OS Foreground (Texturas, Mallas VBO/IBO, Shaders, Arenas, Audio y Heap general).
    m_metrics.mem2Bytes = mem.GetTotalAllocatedBytes();
    constexpr float HEAP_BUDGET = 1024.0f * 1024.0f * 1024.0f; // 1024 MB Cafe OS Heap
    m_metrics.mem2Percent = std::clamp((static_cast<float>(m_metrics.mem2Bytes) / HEAP_BUDGET) * 100.0f, 0.0f, 100.0f);

    // 3. Arenas Lineales de Memoria
    auto& frameArena = mem.GetFrameArena();
    m_metrics.frameArenaBytes = frameArena.GetUsedBytes();
    float frameCap = static_cast<float>(frameArena.GetCapacity());
    m_metrics.frameArenaPercent = (frameCap > 0.0f) ? std::clamp((static_cast<float>(m_metrics.frameArenaBytes) / frameCap) * 100.0f, 0.0f, 100.0f) : 0.0f;

    auto& sceneArena = mem.GetSceneArena();
    m_metrics.sceneArenaBytes = sceneArena.GetUsedBytes();
    float sceneCap = static_cast<float>(sceneArena.GetCapacity());
    m_metrics.sceneArenaPercent = (sceneCap > 0.0f) ? std::clamp((static_cast<float>(m_metrics.sceneArenaBytes) / sceneCap) * 100.0f, 0.0f, 100.0f) : 0.0f;

    // 4. Voces de Audio Espacial 3D
    auto audioTelem = AudioEngine3D::Instance().GetTelemetry();
    m_metrics.activeVoices = audioTelem.activeVoices;
    m_metrics.audioPercent = std::clamp((static_cast<float>(m_metrics.activeVoices) / 32.0f) * 100.0f, 0.0f, 100.0f);

    // 5. Eficiencia de Caché RHI
    auto& rhi = RHIPipelineCache::Instance();
    m_metrics.rhiBypasses = rhi.GetRedundantBypasses();
    m_metrics.rhiStateChanges = rhi.GetStateChanges();
    uint32_t totalRHIRequests = m_metrics.rhiBypasses + m_metrics.rhiStateChanges;
    m_metrics.rhiBypassPercent = (totalRHIRequests > 0)
        ? std::clamp((static_cast<float>(m_metrics.rhiBypasses) / static_cast<float>(totalRHIRequests)) * 100.0f, 0.0f, 100.0f)
        : 100.0f;
}

void ResourceMonitor::Render(SDL_Renderer* renderer, bool isPureGX2) {
    if (!m_visible) return;

    constexpr float panelX = 890.0f;
    constexpr float panelY = 16.0f;
    constexpr float panelW = 370.0f;
    constexpr float panelH = 345.0f;
    constexpr float barW   = 330.0f;
    constexpr float barH   = 5.0f;

    float pulse = 0.5f + 0.5f * std::sin(m_pulseAnim);
    uint8_t borderAlpha = static_cast<uint8_t>(200.0f + pulse * 55.0f);
    ColorRGBA borderColor{0, 210, 255, borderAlpha};

    auto getUsageColor = [](float percent) noexcept -> ColorRGBA {
        if (percent < 70.0f) return ColorRGBA{50, 255, 120, 255}; // Verde Esmeralda
        if (percent < 85.0f) return ColorRGBA{255, 215, 0, 255};   // Amarillo Dorado
        return ColorRGBA{255, 65, 65, 255};                        // Rojo Carmesí
    };

    auto getBypassColor = [](float percent) noexcept -> ColorRGBA {
        if (percent >= 75.0f) return ColorRGBA{50, 255, 120, 255}; // Excelente eficiencia
        if (percent >= 50.0f) return ColorRGBA{255, 215, 0, 255};
        return ColorRGBA{255, 140, 40, 255};
    };

    if (isPureGX2) {
        auto& r2d = Render2DGX2::Instance();
        r2d.Begin();

        // 1. Fondo del panel cyberpunk
        r2d.DrawBox(panelX, panelY, panelW, panelH, ColorRGBA{8, 14, 26, 238}, borderColor, 2.0f);

        // 2. Cabecera
        r2d.DrawBox(panelX, panelY, panelW, 32.0f, ColorRGBA{15, 30, 52, 250}, borderColor, 1.5f);
        r2d.DrawText("TELEMETRIA RECURSOS [-]", panelX + 35.0f, panelY + 9.0f, 2.0f, ColorRGBA{255, 220, 40, 255});

        float rowY = panelY + 38.0f;

        auto drawRowGX2 = [&](std::string_view label, float percent, ColorRGBA col) {
            r2d.DrawText(label, panelX + 16.0f, rowY, 2.0f, ColorRGBA::White());
            r2d.DrawRect(panelX + 16.0f, rowY + 16.0f, barW, barH, ColorRGBA{20, 35, 55, 200});
            float fillW = barW * std::clamp(percent / 100.0f, 0.0f, 1.0f);
            if (fillW > 0.0f) {
                r2d.DrawRect(panelX + 16.0f, rowY + 16.0f, fillW, barH, col);
            }
            rowY += 28.0f;
        };

        StackString<64> s1; s1.Format("CPU:   %4.1f%% (%4.1f ms)", m_metrics.cpuPercent, m_metrics.cpuTimeMs);
        drawRowGX2(s1.string_view(), m_metrics.cpuPercent, getUsageColor(m_metrics.cpuPercent));

        StackString<64> s2; s2.Format("GPU:   %4.1f%% (%4.1f ms)", m_metrics.gpuPercent, m_metrics.gpuTimeMs);
        drawRowGX2(s2.string_view(), m_metrics.gpuPercent, getUsageColor(m_metrics.gpuPercent));

        StackString<64> s3; s3.Format("MEM1:  %4.1f%% (%4.1f MB)", m_metrics.mem1Percent, static_cast<float>(m_metrics.mem1Bytes) / (1024.0f * 1024.0f));
        drawRowGX2(s3.string_view(), m_metrics.mem1Percent, getUsageColor(m_metrics.mem1Percent));

        StackString<64> s4; s4.Format("MEM2:  %4.1f%% (%4.1f MB)", m_metrics.mem2Percent, static_cast<float>(m_metrics.mem2Bytes) / (1024.0f * 1024.0f));
        drawRowGX2(s4.string_view(), m_metrics.mem2Percent, getUsageColor(m_metrics.mem2Percent));

        StackString<64> s5; s5.Format("SCENE: %4.1f%% (%zu KB)", m_metrics.sceneArenaPercent, m_metrics.sceneArenaBytes / 1024);
        drawRowGX2(s5.string_view(), m_metrics.sceneArenaPercent, getUsageColor(m_metrics.sceneArenaPercent));

        StackString<64> s6; s6.Format("FRAME: %4.1f%% (%zu KB)", m_metrics.frameArenaPercent, m_metrics.frameArenaBytes / 1024);
        drawRowGX2(s6.string_view(), m_metrics.frameArenaPercent, getUsageColor(m_metrics.frameArenaPercent));

        StackString<64> s7; s7.Format("AUDIO: %4.1f%% (%u/32 V)", m_metrics.audioPercent, m_metrics.activeVoices);
        drawRowGX2(s7.string_view(), m_metrics.audioPercent, getUsageColor(m_metrics.audioPercent));

        StackString<64> s8; s8.Format("BYPASS:%4.1f%% (%u ev)", m_metrics.rhiBypassPercent, m_metrics.rhiBypasses);
        drawRowGX2(s8.string_view(), m_metrics.rhiBypassPercent, getBypassColor(m_metrics.rhiBypassPercent));

        // Pie de información y FPS
        StackString<64> footerStr;
        footerStr.Format("FPS: %4.1f | [-] OCULTAR", m_metrics.fps);
        r2d.DrawText(footerStr.string_view(), panelX + 50.0f, panelY + panelH - 20.0f, 2.0f, ColorRGBA{0, 220, 255, 255});

        r2d.End();
    } else if (renderer) {
        // Renderizado SDL2 para escenas de software y menús
        int px = static_cast<int>(panelX);
        int py = static_cast<int>(panelY);
        int pw = static_cast<int>(panelW);
        int ph = static_cast<int>(panelH);
        int bw = static_cast<int>(barW);
        int bh = static_cast<int>(barH);

        // Fondo y borde
        Render2D::drawBox(renderer, px, py, pw, ph, ColorRGBA{8, 14, 26, 238}, borderColor);
        Render2D::drawBox(renderer, px, py, pw, 32, ColorRGBA{15, 30, 52, 250}, borderColor);
        Render2D::drawPixelString(renderer, "TELEMETRIA RECURSOS [-]", px + 35, py + 9, 2, ColorRGBA{255, 220, 40, 255});

        int rowY = py + 38;

        auto drawRowSDL = [&](std::string_view text, float percent, ColorRGBA col) {
            Render2D::drawPixelString(renderer, text, px + 16, rowY, 2, ColorRGBA::White());
            SDL_Rect bgRect{px + 16, rowY + 16, bw, bh};
            SDL_SetRenderDrawColor(renderer, 20, 35, 55, 200);
            SDL_RenderFillRect(renderer, &bgRect);

            int fillW = static_cast<int>(static_cast<float>(bw) * std::clamp(percent / 100.0f, 0.0f, 1.0f));
            if (fillW > 0) {
                SDL_Rect fillRect{px + 16, rowY + 16, fillW, bh};
                SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
                SDL_RenderFillRect(renderer, &fillRect);
            }
            rowY += 28;
        };

        StackString<64> s1; s1.Format("CPU:   %4.1f%% (%4.1f ms)", m_metrics.cpuPercent, m_metrics.cpuTimeMs);
        drawRowSDL(s1.string_view(), m_metrics.cpuPercent, getUsageColor(m_metrics.cpuPercent));

        StackString<64> s2; s2.Format("GPU:   %4.1f%% (%4.1f ms)", m_metrics.gpuPercent, m_metrics.gpuTimeMs);
        drawRowSDL(s2.string_view(), m_metrics.gpuPercent, getUsageColor(m_metrics.gpuPercent));

        StackString<64> s3; s3.Format("MEM1:  %4.1f%% (%4.1f MB)", m_metrics.mem1Percent, static_cast<float>(m_metrics.mem1Bytes) / (1024.0f * 1024.0f));
        drawRowSDL(s3.string_view(), m_metrics.mem1Percent, getUsageColor(m_metrics.mem1Percent));

        StackString<64> s4; s4.Format("MEM2:  %4.1f%% (%4.1f MB)", m_metrics.mem2Percent, static_cast<float>(m_metrics.mem2Bytes) / (1024.0f * 1024.0f));
        drawRowSDL(s4.string_view(), m_metrics.mem2Percent, getUsageColor(m_metrics.mem2Percent));

        StackString<64> s5; s5.Format("SCENE: %4.1f%% (%zu KB)", m_metrics.sceneArenaPercent, m_metrics.sceneArenaBytes / 1024);
        drawRowSDL(s5.string_view(), m_metrics.sceneArenaPercent, getUsageColor(m_metrics.sceneArenaPercent));

        StackString<64> s6; s6.Format("FRAME: %4.1f%% (%zu KB)", m_metrics.frameArenaPercent, m_metrics.frameArenaBytes / 1024);
        drawRowSDL(s6.string_view(), m_metrics.frameArenaPercent, getUsageColor(m_metrics.frameArenaPercent));

        StackString<64> s7; s7.Format("AUDIO: %4.1f%% (%u/32 V)", m_metrics.audioPercent, m_metrics.activeVoices);
        drawRowSDL(s7.string_view(), m_metrics.audioPercent, getUsageColor(m_metrics.audioPercent));

        StackString<64> s8; s8.Format("BYPASS:%4.1f%% (%u ev)", m_metrics.rhiBypassPercent, m_metrics.rhiBypasses);
        drawRowSDL(s8.string_view(), m_metrics.rhiBypassPercent, getBypassColor(m_metrics.rhiBypassPercent));

        StackString<64> footerStr;
        footerStr.Format("FPS: %4.1f | [-] OCULTAR", m_metrics.fps);
        Render2D::drawPixelString(renderer, footerStr.string_view(), px + 50, py + ph - 20, 2, ColorRGBA{0, 220, 255, 255});
    }
}

} // namespace WiiU
