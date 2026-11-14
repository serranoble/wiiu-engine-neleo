#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <functional>
#include <vector>

namespace WiiU {

/**
 * @brief [Hito 124 - Jason Gregory Cap. 5.3] Modos de filtrado de textura en hardware GX2.
 */
enum class TextureFilterMode : uint8_t {
    Point = 0,
    Bilinear = 1,
    Bicubic = 2,
    Aniso2x = 3,
    Aniso4x = 4,
    Aniso8x = 5,
    Aniso16x = 6
};

/**
 * @brief Modos globales de sombreado y renderizado geométrico.
 */
enum class ShadingRenderMode : uint8_t {
    Textured = 0,   // Materiales completos con difuso, normal map y reflexiones
    Clay = 1,       // Modo Arcilla uniforme de estudio
    Wireframe = 2   // Modo líneas alámbricas
};

/**
 * @brief Calidad del mapeo de sombras dinámicas en silicio GX2.
 */
enum class ShadowQuality : uint8_t {
    Off = 0,
    Low = 1,        // Sombra dura 512x512
    High = 2        // PCF 1024x1024 con filtro suavizado
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
    ShadingRenderMode renderMode{ShadingRenderMode::Textured};
    bool vsyncEnabled{true};
    ShadowQuality shadowQuality{ShadowQuality::High};
    bool showPedestal{true};

    [[nodiscard]] const char* GetFilterName() const noexcept;
    [[nodiscard]] const char* GetRenderModeName() const noexcept;
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
 * @brief Parámetros de control de cámara y jugabilidad.
 */
struct CameraConfig {
    float orbitSensitivity{1.0f}; // 0.5x, 1.0x, 1.5x, 2.0x
    bool invertY{false};
    float fovDegrees{55.0f};
};

/**
 * @brief Parámetros de periféricos, hápticos y vibración (Jason Gregory Cap. 8).
 */
struct ControlsConfig {
    bool rumbleEnabled{true};
    float rumbleIntensity{1.0f};  // 0.0f a 1.0f
    float deadzoneRadius{0.18f};  // Zona muerta radial canónica (Jason Gregory Cap. 8.2)
};

/**
 * @brief Banderas globales de depuración visual 3D (Jason Gregory Cap. 15.2).
 */
struct DebugConfig {
    bool showColliders{true};      // Volúmenes de colisión (Esferas y AABBs)
    bool showTriggers{true};       // Triggers espaciales y zonas meta
    bool showNavGraph{true};       // Grafo de navegación y rutas A*
    bool showVisionCones{true};    // Conos de visión sensorial de IA
    bool showOSDTelemetry{true};   // Barra inferior de telemetría y FPS
};

/**
 * @brief [Jason Gregory Cap. 5.3: "Engine Configuration"]
 * Gestor Central de Configuración del Motor y Persistencia No Volátil.
 * 
 * Centraliza las variables de configuración (CVars) en categorías tipadas,
 * proporciona persistencia en formato INI sobre almacenamiento no volátil,
 * e implementa notificación reactiva a subsistemas activos.
 */
class EngineConfig {
public:
    static EngineConfig& Get() noexcept {
        static EngineConfig instance;
        return instance;
    }

    static EngineConfig& Instance() noexcept {
        return Get();
    }

    EngineConfig(const EngineConfig&) = delete;
    EngineConfig& operator=(const EngineConfig&) = delete;
    EngineConfig(EngineConfig&&) = delete;
    EngineConfig& operator=(EngineConfig&&) = delete;

    GraphicsConfig graphics;
    AudioConfig audio;
    CameraConfig camera;
    ControlsConfig controls;
    DebugConfig debug;

    [[nodiscard]] const ControlsConfig& GetControls() const noexcept { return controls; }
    [[nodiscard]] ControlsConfig& GetControls() noexcept { return controls; }

    /**
     * @brief Carga las opciones desde un archivo INI en disco.
     * Si el archivo no existe, inicializa con valores por defecto.
     * @param filepath Ruta al archivo INI (si es nullptr, usa la ruta por defecto).
     * @return true si se leyó correctamente o se crearon valores seguros.
     */
    bool LoadFromFile(const char* filepath = nullptr);

    /**
     * @brief Serializa y guarda la configuración actual a un archivo INI.
     * @param filepath Ruta al archivo INI (si es nullptr, usa la ruta por defecto).
     * @return true si la escritura a disco fue exitosa.
     */
    bool SaveToFile(const char* filepath = nullptr);

    /**
     * @brief Restablece todas las opciones a los valores de fábrica recomendados.
     */
    void ResetToDefaults() noexcept;

    /**
     * @brief Aplica las configuraciones activas directamente a los subsistemas del motor
     * (AudioManager, SceneManager, samplers y renderers).
     */
    void ApplyToSubsystems();

    /**
     * @brief Registra un callback que será invocado cada vez que una opción sea modificada.
     */
    using ChangeCallback = std::function<void(const EngineConfig&)>;
    void RegisterChangeCallback(ChangeCallback cb);

    /**
     * @brief Notifica a todos los oyentes registrados que la configuración ha cambiado.
     */
    void NotifyChanged();

    /**
     * @brief Retorna la ruta canónica del archivo de configuración.
     */
    [[nodiscard]] static const char* GetDefaultConfigPath() noexcept;

private:
    EngineConfig() noexcept;
    ~EngineConfig() = default;

    std::vector<ChangeCallback> m_callbacks;
    bool m_isApplying{false};
};

} // namespace WiiU
