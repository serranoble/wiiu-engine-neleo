#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

namespace WiiU {

/**
 * @brief [Hito 124 - Jason Gregory Cap. 5.3] Modos de filtrado de textura en hardware GX2.
 */
enum class TextureFilterMode : uint8_t {
    Point = 0,
    Bilinear = 1,
    Aniso2x = 2,
    Aniso4x = 3,
    Aniso8x = 4,
    Aniso16x = 5
};

/**
 * @brief Resolución nativa canónica del motor.
 */
enum class ResolutionMode : uint8_t {
    Res720p = 0,    // 1280x720 (Nativo estándar Wii U)
    Res1080p = 1,   // 1920x1080 (Full HD Scanbuffer)
    Res480p = 2,    // 854x480 (GamePad DRC)
    Res648p = 3     // 1152x648 (Modo intermedio)
};

/**
 * @brief Parámetros de renderizado y silicio gráfico del motor.
 */
struct GraphicsConfig {
    ResolutionMode resolution{ResolutionMode::Res720p};
    TextureFilterMode textureFilter{TextureFilterMode::Aniso4x};
    bool vsyncEnabled{true};
    bool showOSDTelemetry{true};

    [[nodiscard]] const char* GetFilterName() const noexcept;
    [[nodiscard]] const char* GetResolutionName() const noexcept;
};

/**
 * @brief Parámetros de audio y canales de sonido.
 */
struct AudioConfig {
    float masterVolume{0.80f};  // 0.0f a 1.0f
    bool bgmEnabled{true};
    float bgmVolume{0.65f};
    bool sfxEnabled{true};
    float sfxVolume{1.00f};
};

/**
 * @brief Parámetros de periféricos, hápticos y vibración (Jason Gregory Cap. 8).
 */
struct ControlsConfig {
    bool rumbleEnabled{true};
    float rumbleIntensity{1.0f};  // 0.3f, 0.7f, 1.0f
    float deadzoneRadius{0.18f};  // 0.10f, 0.18f, 0.25f (Jason Gregory Cap. 8.2)
};

/**
 * @brief Parámetros de red y servidor local/LAN (Jason Gregory Cap. 20).
 */
struct NetConfig {
    char serverIp[48]{"127.0.0.1"};
    uint16_t tcpPort{7777u};
    uint16_t udpPort{7778u};
};

/**
 * @brief Callback de notificación de cambios sin asignación dinámica en heap.
 */
using ConfigChangeCallback = void(*)(const class EngineConfig&, void* userData);

/**
 * @brief [Jason Gregory Cap. 5.3: "Engine Configuration"]
 * Gestor Central de Configuración del Motor y Persistencia No Volátil.
 * 
 * Centraliza las variables de configuración en categorías tipadas globales,
 * proporciona persistencia en formato INI sobre almacenamiento no volátil,
 * e implementa notificación desacoplada a subsistemas activos (DIP).
 */
class EngineConfig {
public:
    static EngineConfig& Instance() noexcept {
        static EngineConfig instance;
        return instance;
    }

    static EngineConfig& Get() noexcept {
        return Instance();
    }

    EngineConfig(const EngineConfig&) = delete;
    EngineConfig& operator=(const EngineConfig&) = delete;
    EngineConfig(EngineConfig&&) = delete;
    EngineConfig& operator=(EngineConfig&&) = delete;

    GraphicsConfig graphics;
    AudioConfig audio;
    ControlsConfig controls;
    NetConfig network;

    [[nodiscard]] const ControlsConfig& GetControls() const noexcept { return controls; }
    [[nodiscard]] ControlsConfig& GetControls() noexcept { return controls; }
    [[nodiscard]] const NetConfig& GetNetwork() const noexcept { return network; }
    [[nodiscard]] NetConfig& GetNetwork() noexcept { return network; }

    bool LoadFromFile(const char* filepath = nullptr);
    bool SaveToFile(const char* filepath = nullptr);
    bool SaveToFileAsync(const char* filepath = nullptr) const;
    void ResetToDefaults() noexcept;
    void ApplyToSubsystems();

    /**
     * @brief Registra un observador pasivo. Cero alocaciones dinámicas.
     * @return ID único del observador para posterior desregistro.
     */
    uint32_t RegisterChangeCallback(ConfigChangeCallback cb, void* userData = nullptr) noexcept;

    /**
     * @brief Desregistra un observador por su ID de forma segura.
     */
    void UnregisterChangeCallback(uint32_t id) noexcept;

    void NotifyChanged();

    [[nodiscard]] static const char* GetDefaultConfigPath() noexcept;

private:
    EngineConfig() noexcept;
    ~EngineConfig() = default;
    [[nodiscard]] size_t FormatIni(char* outBuf, size_t capacity) const noexcept;

    struct ObserverSlot {
        uint32_t id{0};
        ConfigChangeCallback callback{nullptr};
        void* userData{nullptr};
    };

    static constexpr size_t MAX_OBSERVERS = 16;
    std::array<ObserverSlot, MAX_OBSERVERS> m_observers{};
    uint32_t m_nextObserverId{1};
    bool m_isApplying{false};
};

} // namespace WiiU
