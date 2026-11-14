#include "Core/Config/EngineConfig.hpp"
#include "Core/Storage/StorageManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Scene/SceneManager.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace WiiU {

const char* GraphicsConfig::GetFilterName() const noexcept {
    switch (textureFilter) {
        case TextureFilterMode::Point:    return "POINT (NEAREST)";
        case TextureFilterMode::Bilinear: return "BILINEAL HW";
        case TextureFilterMode::Bicubic:  return "BICUBICO R700";
        case TextureFilterMode::Aniso2x:  return "ANISO 2X HW";
        case TextureFilterMode::Aniso4x:  return "ANISO 4X HW";
        case TextureFilterMode::Aniso8x:  return "ANISO 8X HW";
        case TextureFilterMode::Aniso16x: return "ANISO 16X HW";
        default:                          return "ANISO 4X HW";
    }
}

const char* GraphicsConfig::GetRenderModeName() const noexcept {
    switch (renderMode) {
        case ShadingRenderMode::Textured:  return "TEXTURIZADO";
        case ShadingRenderMode::Clay:      return "ARCILLA (CLAY)";
        case ShadingRenderMode::Wireframe: return "WIREFRAME";
        default:                           return "TEXTURIZADO";
    }
}

const char* GraphicsConfig::GetResolutionName() const noexcept {
    switch (resolution) {
        case ResolutionMode::Res720p:  return "720P (1280x720)";
        case ResolutionMode::Res1080p: return "1080P (1920x1080)";
        case ResolutionMode::Res480p:  return "480P (854x480)";
        case ResolutionMode::Res648p:  return "648P (1152x648)";
        default:                       return "720P (1280x720)";
    }
}

const char* EngineConfig::GetDefaultConfigPath() noexcept {
    static char s_configPath[256] = "";
    if (s_configPath[0] == '\0') {
        StorageManager::ResolveSavePath("engine_config.ini", s_configPath, sizeof(s_configPath));
    }
    return s_configPath;
}

EngineConfig::EngineConfig() noexcept {
    ResetToDefaults();
}

void EngineConfig::ResetToDefaults() noexcept {
    graphics.resolution = ResolutionMode::Res720p;
    graphics.textureFilter = TextureFilterMode::Aniso4x;
    graphics.renderMode = ShadingRenderMode::Textured;
    graphics.vsyncEnabled = true;
    graphics.shadowQuality = ShadowQuality::High;
    graphics.showPedestal = true;

    audio.masterVolume = 0.80f;
    audio.bgmEnabled = true;
    audio.bgmVolume = 0.65f;
    audio.sfxEnabled = true;
    audio.sfxVolume = 1.00f;

    camera.orbitSensitivity = 1.0f;
    camera.invertY = false;
    camera.fovDegrees = 55.0f;

    controls.rumbleEnabled = true;
    controls.rumbleIntensity = 1.0f;
    controls.deadzoneRadius = 0.18f;

    debug.showColliders = true;
    debug.showTriggers = true;
    debug.showNavGraph = true;
    debug.showVisionCones = true;
    debug.showOSDTelemetry = true;
}

static inline void TrimWhitespace(char* str) {
    if (!str) return;
    char* end = str + std::strlen(str) - 1;
    while (end >= str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        --end;
    }
    char* start = str;
    while (*start == ' ' || *start == '\t') {
        ++start;
    }
    if (start != str) {
        std::memmove(str, start, std::strlen(start) + 1);
    }
}

bool EngineConfig::LoadFromFile(const char* filepath) {
    const char* path = filepath ? filepath : GetDefaultConfigPath();
    FILE* f = std::fopen(path, "r");
    if (!f) {
        // [CORRECCIÓN AUDITORÍA]: Pasar filepath original (o nullptr) para preservar la ruta canónica en SAL
        SaveToFile(filepath);
        return false;
    }

    char line[256];
    char currentSection[64] = "";

    while (std::fgets(line, sizeof(line), f)) {
        TrimWhitespace(line);
        if (line[0] == '\0' || line[0] == ';' || line[0] == '#') {
            continue; // Comentarios o líneas vacías
        }

        if (line[0] == '[' && line[std::strlen(line) - 1] == ']') {
            line[std::strlen(line) - 1] = '\0';
            std::strncpy(currentSection, line + 1, sizeof(currentSection) - 1);
            currentSection[sizeof(currentSection) - 1] = '\0';
            TrimWhitespace(currentSection);
            continue;
        }

        char* eq = std::strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        TrimWhitespace(key);
        TrimWhitespace(val);

        if (std::strcmp(currentSection, "Graphics") == 0) {
            if (std::strcmp(key, "resolution") == 0) {
                graphics.resolution = static_cast<ResolutionMode>(std::clamp(std::atoi(val), 0, 3));
            } else if (std::strcmp(key, "texture_filter") == 0) {
                graphics.textureFilter = static_cast<TextureFilterMode>(std::clamp(std::atoi(val), 0, 6));
            } else if (std::strcmp(key, "render_mode") == 0) {
                graphics.renderMode = static_cast<ShadingRenderMode>(std::clamp(std::atoi(val), 0, 2));
            } else if (std::strcmp(key, "vsync") == 0) {
                graphics.vsyncEnabled = (std::atoi(val) != 0);
            } else if (std::strcmp(key, "shadow_quality") == 0) {
                graphics.shadowQuality = static_cast<ShadowQuality>(std::clamp(std::atoi(val), 0, 2));
            } else if (std::strcmp(key, "show_pedestal") == 0) {
                graphics.showPedestal = (std::atoi(val) != 0);
            }
        } else if (std::strcmp(currentSection, "Audio") == 0) {
            if (std::strcmp(key, "master_volume") == 0) {
                audio.masterVolume = std::clamp(static_cast<float>(std::atof(val)), 0.0f, 1.0f);
            } else if (std::strcmp(key, "bgm_enabled") == 0) {
                audio.bgmEnabled = (std::atoi(val) != 0);
            } else if (std::strcmp(key, "bgm_volume") == 0) {
                audio.bgmVolume = std::clamp(static_cast<float>(std::atof(val)), 0.0f, 1.0f);
            } else if (std::strcmp(key, "sfx_enabled") == 0) {
                audio.sfxEnabled = (std::atoi(val) != 0);
            } else if (std::strcmp(key, "sfx_volume") == 0) {
                audio.sfxVolume = std::clamp(static_cast<float>(std::atof(val)), 0.0f, 1.0f);
            }
        } else if (std::strcmp(currentSection, "Camera") == 0) {
            if (std::strcmp(key, "sensitivity") == 0) {
                camera.orbitSensitivity = std::clamp(static_cast<float>(std::atof(val)), 0.25f, 4.0f);
            } else if (std::strcmp(key, "invert_y") == 0) {
                camera.invertY = (std::atoi(val) != 0);
            } else if (std::strcmp(key, "fov") == 0) {
                camera.fovDegrees = std::clamp(static_cast<float>(std::atof(val)), 30.0f, 110.0f);
            }
        } else if (std::strcmp(currentSection, "Controls") == 0) {
            if (std::strcmp(key, "rumble_enabled") == 0) {
                controls.rumbleEnabled = (std::atoi(val) != 0);
            } else if (std::strcmp(key, "rumble_intensity") == 0) {
                controls.rumbleIntensity = std::clamp(static_cast<float>(std::atof(val)), 0.0f, 1.0f);
            } else if (std::strcmp(key, "deadzone_radius") == 0) {
                controls.deadzoneRadius = std::clamp(static_cast<float>(std::atof(val)), 0.05f, 0.50f);
            }
        } else if (std::strcmp(currentSection, "Debug") == 0) {
            if (std::strcmp(key, "show_colliders") == 0) {
                debug.showColliders = (std::atoi(val) != 0);
            } else if (std::strcmp(key, "show_triggers") == 0) {
                debug.showTriggers = (std::atoi(val) != 0);
            } else if (std::strcmp(key, "show_nav_graph") == 0) {
                debug.showNavGraph = (std::atoi(val) != 0);
            } else if (std::strcmp(key, "show_vision_cones") == 0) {
                debug.showVisionCones = (std::atoi(val) != 0);
            } else if (std::strcmp(key, "show_telemetry") == 0) {
                debug.showOSDTelemetry = (std::atoi(val) != 0);
            }
        }
    }

    std::fclose(f);
    ApplyToSubsystems();
    return true;
}

bool EngineConfig::SaveToFile(const char* filepath) {
    const char* path = filepath ? filepath : GetDefaultConfigPath();

    char iniBuffer[2048];
    int len = std::snprintf(iniBuffer, sizeof(iniBuffer),
        "; =============================================================================\n"
        "; [Hito 125 - OPT-396] Wii U Engine Global Configuration (StorageManager SAL)\n"
        "; =============================================================================\n\n"
        "[Graphics]\n"
        "resolution=%d\n"
        "texture_filter=%d\n"
        "render_mode=%d\n"
        "vsync=%d\n"
        "shadow_quality=%d\n"
        "show_pedestal=%d\n\n"
        "[Audio]\n"
        "master_volume=%.2f\n"
        "bgm_enabled=%d\n"
        "bgm_volume=%.2f\n"
        "sfx_enabled=%d\n"
        "sfx_volume=%.2f\n\n"
        "[Camera]\n"
        "sensitivity=%.2f\n"
        "invert_y=%d\n"
        "fov=%.1f\n\n"
        "[Controls]\n"
        "rumble_enabled=%d\n"
        "rumble_intensity=%.2f\n"
        "deadzone_radius=%.2f\n\n"
        "[Debug]\n"
        "show_colliders=%d\n"
        "show_triggers=%d\n"
        "show_nav_graph=%d\n"
        "show_vision_cones=%d\n"
        "show_telemetry=%d\n",
        static_cast<int>(graphics.resolution),
        static_cast<int>(graphics.textureFilter),
        static_cast<int>(graphics.renderMode),
        graphics.vsyncEnabled ? 1 : 0,
        static_cast<int>(graphics.shadowQuality),
        graphics.showPedestal ? 1 : 0,
        audio.masterVolume,
        audio.bgmEnabled ? 1 : 0,
        audio.bgmVolume,
        audio.sfxEnabled ? 1 : 0,
        audio.sfxVolume,
        camera.orbitSensitivity,
        camera.invertY ? 1 : 0,
        camera.fovDegrees,
        controls.rumbleEnabled ? 1 : 0,
        controls.rumbleIntensity,
        controls.deadzoneRadius,
        debug.showColliders ? 1 : 0,
        debug.showTriggers ? 1 : 0,
        debug.showNavGraph ? 1 : 0,
        debug.showVisionCones ? 1 : 0,
        debug.showOSDTelemetry ? 1 : 0
    );

    if (len <= 0) return false;

    // [CORRECCIÓN AUDITORÍA]: Delegación 100% limpia a la SAL sin duplicación de código de E/S
    const char* targetPath = filepath ? filepath : "engine_config.ini";
    return StorageManager::SaveStringAtomic(targetPath, std::string_view(iniBuffer, static_cast<size_t>(len)));
}

void EngineConfig::ApplyToSubsystems() {
    if (m_isApplying) return;
    m_isApplying = true;

    // 1. Sincronizar Audio
    auto& audioMgr = AudioManager::Instance();
    audioMgr.SetMasterVolume(audio.masterVolume);
    audioMgr.SetMusicEnabled(audio.bgmEnabled);
    audioMgr.SetBGMVolume(audio.bgmVolume);

    // 2. Sincronizar SceneManager (VSync / FPS Limit)
    auto& sceneMgr = SceneManager::Instance();
    sceneMgr.SetFPSLimitEnabled(graphics.vsyncEnabled);

    // 3. Notificar a los observadores
    NotifyChanged();

    m_isApplying = false;
}

void EngineConfig::RegisterChangeCallback(ChangeCallback cb) {
    if (cb) {
        m_callbacks.push_back(std::move(cb));
    }
}

void EngineConfig::NotifyChanged() {
    for (const auto& cb : m_callbacks) {
        if (cb) {
            cb(*this);
        }
    }
}

} // namespace WiiU
