#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace WiiU {

enum class PlayerIndex : uint8_t {
    Player1 = 0,
    Player2 = 1,
    Player3 = 2,
    Player4 = 3,
    GamePad = 4 // Canal directo para el GamePad DRC (o alias de Player1 cuando usa GamePad)
};

/**
 * @brief [Jason Gregory Cap. 8.2: "Haptic Feedback & Rumble"]
 * Presets hápticos predefinidos con duraciones e intensidades calibradas.
 */
enum class RumblePreset : uint8_t {
    SubtleTick = 0,     // 0.08s, 25% (Confirmaciones táctiles en UI/Menú)
    JumpLand = 1,       // 0.15s, 50% (Impacto de aterrizaje de KCC)
    DamageImpact = 2,   // 0.35s, 85% (Colisiones violentas o daño de avatar)
    HeavyExplosion = 3  // 0.60s, 100% (Explosiones o eventos cinemáticos)
};

/**
 * @brief [Jason Gregory Cap. 8.2 & 8.4]
 * Gestor Central de Vibración y Hápticos para Consola (Wii U) y PC (SDL2).
 * 
 * Controla de forma no bloqueante los actuadores de rotación excéntrica (ERM)
 * del GamePad DRC (VPAD), Wii Remote y Wii U Pro Controller (WPAD), así como
 * mandos conectados por SDL en PC. Implementa apagado automático de seguridad
 * y respeto a la configuración global de EngineConfig.
 */
class RumbleManager {
public:
    static RumbleManager& Instance() noexcept {
        static RumbleManager instance;
        return instance;
    }

    RumbleManager(const RumbleManager&) = delete;
    RumbleManager& operator=(const RumbleManager&) = delete;
    RumbleManager(RumbleManager&&) = delete;
    RumbleManager& operator=(RumbleManager&&) = delete;

    void Init();
    void Update(float dt);
    void StopAll() noexcept;
    void Shutdown() noexcept;

    /**
     * @brief Dispara vibración háptica en el jugador o dispositivo indicado.
     * @param player Slot de jugador (Player1 a Player4 o GamePad)
     * @param intensity Intensidad normalizada [0.0f, 1.0f]
     * @param durationSeconds Duración en segundos (ej. 0.2f)
     */
    void TriggerRumble(PlayerIndex player, float intensity, float durationSeconds);

    /**
     * @brief Dispara un preset háptico canónico.
     */
    void PlayPreset(PlayerIndex player, RumblePreset preset);

    /**
     * @brief Detiene la vibración en un jugador específico de inmediato.
     */
    void StopRumble(PlayerIndex player) noexcept;

    [[nodiscard]] bool IsRumbling(PlayerIndex player) const noexcept;

private:
    RumbleManager() = default;
    ~RumbleManager() = default;

    void SetHardwareRumble(PlayerIndex player, float intensity, bool active) noexcept;

    struct RumbleChannel {
        float remainingTime{0.0f};
        float pulseInterval{0.0f};
        float intensity{0.0f};
        bool isActuatorOn{false};
    };

    static constexpr size_t NUM_CHANNELS = 5; // 4 jugadores + 1 GamePad dedicado
    RumbleChannel m_channels[NUM_CHANNELS]{};
    bool m_initialized{false};
};

} // namespace WiiU
