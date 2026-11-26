#include "Core/ResourceMonitor.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/StackString.hpp"
#include "Audio/AudioEngine3D.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/Render2D.hpp"
#include "Platform/PlatformClock.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace Neleo {

ResourceMonitor::ResourceMonitor() noexcept {
    const uint64_t freq = PlatformClock::GetPerformanceFrequency();
    m_perfFreq = (freq > 0ULL) ? freq : 1ULL;
}

void ResourceMonitor::SetVisible(bool visible) noexcept {
    const bool becomingVisible = (!m_visible && visible);
    m_visible = visible;
    if (becomingVisible) {
        m_sampleTimer = 0.0f;
        SampleMetrics();
    }
}

void ResourceMonitor::BeginFrame() {
    m_frameStartCounter = PlatformClock::GetPerformanceCounter();
}

void ResourceMonitor::MarkCPUEnd() {
    m_cpuEndCounter = PlatformClock::GetPerformanceCounter();
    if (m_frameStartCounter != 0 && m_cpuEndCounter >= m_frameStartCounter) {
        float instCpuMs = static_cast<float>(m_cpuEndCounter - m_frameStartCounter) * 1000.0f / static_cast<float>(m_perfFreq);
        if (instCpuMs > 100.0f) instCpuMs = 100.0f;
        m_smoothedCpuMs = m_smoothedCpuMs * 0.90f + instCpuMs * 0.10f;
    }
}

void ResourceMonitor::MarkGPUEnd() {
    m_gpuEndCounter = PlatformClock::GetPerformanceCounter();
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
    const float sampleInterval = m_visible ? 0.05f : 0.50f;
    if (m_sampleTimer >= sampleInterval) {
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

void ResourceMonitor::Render() {
    if (!m_visible) return;

    constexpr int panelX = 890;
    constexpr int panelY = 16;
    constexpr int panelW = 370;
    constexpr int panelH = 345;
    constexpr int barW   = 330;
    constexpr int barH   = 5;

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

    struct MetricRow {
        StackString<64> label{};
        float percent{0.0f};
        ColorRGBA color{};
    };
    std::array<MetricRow, 8> rows{};
    rows[0].label.Format("CPU:   %4.1f%% (%4.1f ms)", static_cast<double>(m_metrics.cpuPercent), static_cast<double>(m_metrics.cpuTimeMs));
    rows[0].percent = m_metrics.cpuPercent; rows[0].color = getUsageColor(m_metrics.cpuPercent);
    rows[1].label.Format("GPU:   %4.1f%% (%4.1f ms)", static_cast<double>(m_metrics.gpuPercent), static_cast<double>(m_metrics.gpuTimeMs));
    rows[1].percent = m_metrics.gpuPercent; rows[1].color = getUsageColor(m_metrics.gpuPercent);
    rows[2].label.Format("MEM1:  %4.1f%% (%4.1f MB)", static_cast<double>(m_metrics.mem1Percent), static_cast<double>(m_metrics.mem1Bytes) / (1024.0 * 1024.0));
    rows[2].percent = m_metrics.mem1Percent; rows[2].color = getUsageColor(m_metrics.mem1Percent);
    rows[3].label.Format("MEM2:  %4.1f%% (%4.1f MB)", static_cast<double>(m_metrics.mem2Percent), static_cast<double>(m_metrics.mem2Bytes) / (1024.0 * 1024.0));
    rows[3].percent = m_metrics.mem2Percent; rows[3].color = getUsageColor(m_metrics.mem2Percent);
    rows[4].label.Format("SCENE: %4.1f%% (%zu KB)", static_cast<double>(m_metrics.sceneArenaPercent), m_metrics.sceneArenaBytes / 1024);
    rows[4].percent = m_metrics.sceneArenaPercent; rows[4].color = getUsageColor(m_metrics.sceneArenaPercent);
    rows[5].label.Format("FRAME: %4.1f%% (%zu KB)", static_cast<double>(m_metrics.frameArenaPercent), m_metrics.frameArenaBytes / 1024);
    rows[5].percent = m_metrics.frameArenaPercent; rows[5].color = getUsageColor(m_metrics.frameArenaPercent);
    rows[6].label.Format("AUDIO: %4.1f%% (%u/32 V)", static_cast<double>(m_metrics.audioPercent), m_metrics.activeVoices);
    rows[6].percent = m_metrics.audioPercent; rows[6].color = getUsageColor(m_metrics.audioPercent);
    rows[7].label.Format("BYPASS:%4.1f%% (%u ev)", static_cast<double>(m_metrics.rhiBypassPercent), m_metrics.rhiBypasses);
    rows[7].percent = m_metrics.rhiBypassPercent; rows[7].color = getBypassColor(m_metrics.rhiBypassPercent);

    StackString<64> footerStr;
    footerStr.Format("FPS: %4.1f | [-] OCULTAR", static_cast<double>(m_metrics.fps));

    Render2D::Begin();
    Render2D::DrawBox(panelX, panelY, panelW, panelH, ColorRGBA{8, 14, 26, 238}, borderColor);
    Render2D::DrawBox(panelX, panelY, panelW, 32, ColorRGBA{15, 30, 52, 250}, borderColor);
    Render2D::DrawPixelString("TELEMETRIA RECURSOS [-]", panelX + 35, panelY + 9, 2, ColorRGBA{255, 220, 40, 255});

    int rowY = panelY + 38;
    for (const auto& r : rows) {
        Render2D::DrawPixelString(r.label.string_view(), panelX + 16, rowY, 2, ColorRGBA::White());
        Render2D::DrawRect(panelX + 16, rowY + 16, barW, barH, ColorRGBA{20, 35, 55, 200});
        const int fillW = static_cast<int>(static_cast<float>(barW) * std::clamp(r.percent / 100.0f, 0.0f, 1.0f));
        if (fillW > 0) {
            Render2D::DrawRect(panelX + 16, rowY + 16, fillW, barH, r.color);
        }
        rowY += 28;
    }

    Render2D::DrawPixelString(footerStr.string_view(), panelX + 50, panelY + panelH - 20, 2, ColorRGBA{0, 220, 255, 255});
    Render2D::End();
}

} // namespace Neleo
