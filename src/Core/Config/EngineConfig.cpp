#include "Core/Config/EngineConfig.hpp"
#include "Core/Storage/StorageManager.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>

namespace WiiU {

const char* GraphicsConfig::GetFilterName() const noexcept {
    switch (textureFilter) {
        case TextureFilterMode::Point:    return "PUNTO (NEAREST)";
        case TextureFilterMode::Bilinear: return "BILINEAL";
        case TextureFilterMode::Aniso2x:  return "ANISOTROPICO 2X";
        case TextureFilterMode::Aniso4x:  return "ANISOTROPICO 4X";
        case TextureFilterMode::Aniso8x:  return "ANISOTROPICO 8X";
        case TextureFilterMode::Aniso16x: return "ANISOTROPICO 16X";
        default:                          return "ANISOTROPICO 4X";
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
    graphics.vsyncEnabled = true;
    graphics.showOSDTelemetry = true;

    audio.masterVolume = 0.80f;
    audio.bgmEnabled = true;
    audio.bgmVolume = 0.65f;
    audio.sfxEnabled = true;
    audio.sfxVolume = 1.00f;

    controls.rumbleEnabled = true;
    controls.rumbleIntensity = 1.0f;
    controls.deadzoneRadius = 0.18f;
}

static inline void TrimWhitespace(char* str) noexcept {
    if (!str) return;
    size_t len = std::strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' || str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
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
    ResetToDefaults();
    const char* targetName = filepath ? filepath : "engine_config.ini";

    std::vector<uint8_t> buffer;
    if (!StorageManager::LoadSaveFile(targetName, buffer) || buffer.empty()) {
        SaveToFile(filepath);
        ApplyToSubsystems();
        return false;
    }

    std::string content(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    char currentSection[64] = "";

    size_t lineStart = 0;
    while (lineStart < content.size()) {
        size_t lineEnd = content.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = content.size();

        std::string rawLine = content.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        char line[256];
        size_t copyLen = std::min(rawLine.size(), sizeof(line) - 1);
        std::memcpy(line, rawLine.data(), copyLen);
        line[copyLen] = '\0';

        TrimWhitespace(line);
        if (line[0] == '\0' || line[0] == ';' || line[0] == '#') {
            continue;
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
                int v = std::atoi(val);
                graphics.resolution = static_cast<ResolutionMode>(std::clamp(v, 0, 3));
            } else if (std::strcmp(key, "texture_filter") == 0) {
                int v = std::atoi(val);
                graphics.textureFilter = static_cast<TextureFilterMode>(std::clamp(v, 0, 5));
            } else if (std::strcmp(key, "vsync") == 0) {
                graphics.vsyncEnabled = (std::atoi(val) != 0);
            } else if (std::strcmp(key, "show_telemetry") == 0) {
                graphics.showOSDTelemetry = (std::atoi(val) != 0);
            }
        } else if (std::strcmp(currentSection, "Audio") == 0) {
            if (std::strcmp(key, "master_volume") == 0) {
                float v = static_cast<float>(std::atof(val));
                if (std::isfinite(v)) audio.masterVolume = std::clamp(v, 0.0f, 1.0f);
            } else if (std::strcmp(key, "bgm_enabled") == 0) {
                audio.bgmEnabled = (std::atoi(val) != 0);
            } else if (std::strcmp(key, "bgm_volume") == 0) {
                float v = static_cast<float>(std::atof(val));
                if (std::isfinite(v)) audio.bgmVolume = std::clamp(v, 0.0f, 1.0f);
            } else if (std::strcmp(key, "sfx_enabled") == 0) {
                audio.sfxEnabled = (std::atoi(val) != 0);
            } else if (std::strcmp(key, "sfx_volume") == 0) {
                float v = static_cast<float>(std::atof(val));
                if (std::isfinite(v)) audio.sfxVolume = std::clamp(v, 0.0f, 1.0f);
            }
        } else if (std::strcmp(currentSection, "Controls") == 0) {
            if (std::strcmp(key, "rumble_enabled") == 0) {
                controls.rumbleEnabled = (std::atoi(val) != 0);
            } else if (std::strcmp(key, "rumble_intensity") == 0) {
                float v = static_cast<float>(std::atof(val));
                if (std::isfinite(v)) controls.rumbleIntensity = std::clamp(v, 0.0f, 1.0f);
            } else if (std::strcmp(key, "deadzone_radius") == 0) {
                float v = static_cast<float>(std::atof(val));
                if (std::isfinite(v)) controls.deadzoneRadius = std::clamp(v, 0.05f, 0.50f);
            }
        }
    }

    ApplyToSubsystems();
    return true;
}

bool EngineConfig::SaveToFile(const char* filepath) {
    char iniBuffer[1024];
    int len = std::snprintf(iniBuffer, sizeof(iniBuffer),
        "; =============================================================================\n"
        "; Global Engine Configuration (StorageManager SAL)\n"
        "; =============================================================================\n\n"
        "[Graphics]\n"
        "resolution=%d\n"
        "texture_filter=%d\n"
        "vsync=%d\n"
        "show_telemetry=%d\n\n"
        "[Audio]\n"
        "master_volume=%.2f\n"
        "bgm_enabled=%d\n"
        "bgm_volume=%.2f\n"
        "sfx_enabled=%d\n"
        "sfx_volume=%.2f\n\n"
        "[Controls]\n"
        "rumble_enabled=%d\n"
        "rumble_intensity=%.2f\n"
        "deadzone_radius=%.2f\n",
        static_cast<int>(graphics.resolution),
        static_cast<int>(graphics.textureFilter),
        graphics.vsyncEnabled ? 1 : 0,
        graphics.showOSDTelemetry ? 1 : 0,
        audio.masterVolume,
        audio.bgmEnabled ? 1 : 0,
        audio.bgmVolume,
        audio.sfxEnabled ? 1 : 0,
        audio.sfxVolume,
        controls.rumbleEnabled ? 1 : 0,
        controls.rumbleIntensity,
        controls.deadzoneRadius
    );

    if (len <= 0 || static_cast<size_t>(len) >= sizeof(iniBuffer)) {
        return false;
    }

    const char* targetPath = filepath ? filepath : "engine_config.ini";
    return StorageManager::SaveStringAtomic(targetPath, std::string_view(iniBuffer, static_cast<size_t>(len)));
}

uint32_t EngineConfig::RegisterChangeCallback(ConfigChangeCallback cb, void* userData) noexcept {
    if (!cb) return 0;
    for (auto& slot : m_observers) {
        if (slot.id == 0) {
            slot.id = m_nextObserverId++;
            if (m_nextObserverId == 0) m_nextObserverId = 1;
            slot.callback = cb;
            slot.userData = userData;
            return slot.id;
        }
    }
    return 0; // Pool lleno
}

void EngineConfig::UnregisterChangeCallback(uint32_t id) noexcept {
    if (id == 0) return;
    for (auto& slot : m_observers) {
        if (slot.id == id) {
            slot.id = 0;
            slot.callback = nullptr;
            slot.userData = nullptr;
            return;
        }
    }
}

void EngineConfig::NotifyChanged() {
    for (const auto& slot : m_observers) {
        if (slot.id != 0 && slot.callback) {
            slot.callback(*this, slot.userData);
        }
    }
}

void EngineConfig::ApplyToSubsystems() {
    if (m_isApplying) return;
    m_isApplying = true;
    NotifyChanged();
    m_isApplying = false;
}

} // namespace WiiU
