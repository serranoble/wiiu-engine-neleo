#pragma once

#include "Graphics/Math3D.hpp"
#include <cstdint>
#include <vector>
#include <string>

namespace WiiU {

/**
 * @brief Handle generacional seguro de 32 bits para un canal de voz de audio activo.
 * Previene acceso a voces recicladas o robadas por el Voice Pool.
 */
struct SoundHandle {
    uint16_t voiceIndex{0xFFFF};
    uint16_t generation{0};

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return voiceIndex != 0xFFFF;
    }

    constexpr bool operator==(const SoundHandle& o) const noexcept {
        return voiceIndex == o.voiceIndex && generation == o.generation;
    }
    constexpr bool operator!=(const SoundHandle& o) const noexcept {
        return !(*this == o);
    }
};

/**
 * @brief Búfer de audio PCM en memoria (mono o estéreo, 16-bit signed @ 48kHz).
 */
struct SoundClip {
    std::string name;
    std::vector<int16_t> samples;
    int channels{1};      // 1 = mono (espacializable 3D), 2 = estéreo plano
    int sampleRate{48000};

    [[nodiscard]] size_t GetFrameCount() const noexcept {
        if (channels <= 0) return 0;
        return samples.size() / static_cast<size_t>(channels);
    }
};

/**
 * @brief Representación del oyente (cámara u oídos del jugador) en el espacio 3D.
 */
struct AudioListener3D {
    Point3D position{0.0f, 0.0f, 0.0f};
    Point3D forward{0.0f, 0.0f, -1.0f}; // Vector unitario hacia donde mira
    Point3D up{0.0f, 1.0f, 0.0f};       // Vector vertical de orientación
    Point3D velocity{0.0f, 0.0f, 0.0f}; // Para Doppler si procede
    bool isSubmerged{false};             // Oyente sumergido bajo el agua (aplica LPF subacuático)
};

/**
 * @brief [Hito 133] Submix Buses jerarquicos para mezcla y Sidechain Ducking (Cap. 15).
 */
enum class AudioBus : uint8_t {
    Master   = 0,
    BGM      = 1,
    SFX      = 2,
    Ambience = 3,
    Count    = 4
};

/**
 * @brief Parámetros físicos y acústicos de una fuente emisora de sonido 3D.
 */
struct AudioEmitter3D {
    Point3D position{0.0f, 0.0f, 0.0f};
    Point3D velocity{0.0f, 0.0f, 0.0f};
    float minDistance{1.5f};     // Distancia dentro de la cual el volumen es 100%
    float maxDistance{35.0f};    // Distancia a partir de la cual el sonido se extingue
    float rolloffFactor{1.0f};   // Factor de atenuación de distancia
    float volume{1.0f};          // Ganancia intrínseca [0.0, 1.0]
    float pitch{1.0f};           // Tono / velocidad de reproducción [0.5, 2.0]
    float occlusion{0.0f};       // Oclusión acústica por obstáculos [0.0 = libre, 1.0 = ocluido]
    bool isLooping{false};       // Si el sonido se repite indefinidamente
    uint8_t priority{50};        // Prioridad de robo de voz (0 = ambiente menor, 255 = crítico)
    AudioBus bus{AudioBus::SFX}; // Bus de submezcla destino
    float alertRadius{0.0f};     // Radio de audibilidad para mecánicas de gameplay / sigilo
};

/**
 * @brief Estado de procesamiento de una voz activa en el Voice Pool estático de 32 canales.
 */
struct AudioVoice {
    bool active{false};
    uint16_t generation{1};
    const SoundClip* clip{nullptr};
    size_t sampleOffset{0};      // Índice entero actual dentro de clip->samples
    float samplePhase{0.0f};     // Acumulador de fase fraccionario para Doppler / resampleo
    float dopplerPitch{1.0f};    // Factor de tono efectivo (pitch * dopplerShift)
    bool isLooping{false};
    uint8_t priority{50};
    AudioBus bus{AudioBus::SFX};
    bool is3D{false};
    AudioEmitter3D emitter;

    // Ganancias de volumen estéreo suavizadas (anti-zipper)
    float currentGainL{0.0f};
    float currentGainR{0.0f};
    float targetGainL{0.0f};
    float targetGainR{0.0f};

    // Filtro Paso Bajo (LPF) de 1 polo para simular oclusión acústica y medio subacuático
    // y[n] = y[n-1] + alpha * (x[n] - y[n-1])
    float lpfAlpha{1.0f};        // Coeficiente del filtro (1.0 = transparente / sin filtro)
    float lpfStateL{0.0f};       // Memoria del filtro canal izquierdo
    float lpfStateR{0.0f};       // Memoria del filtro canal derecho

    // Telemetría para OSD y debug
    float distanceToListener{0.0f};
    float perceivedLoudness{0.0f};

    // [OPT-224 / OPT-249] Virtualizacion de voces y sueno acustico LOD
    bool isVirtualized{false};
    bool isSleeping{false};
};

/**
 * @brief Resumen diagnóstico del estado del motor de audio espacial 3D.
 */
struct AudioEngineTelemetry {
    uint32_t activeVoices{0};
    uint32_t totalVoices{32};
    uint32_t virtualizedVoices{0};
    uint32_t sleepingVoices{0};
    uint32_t maxVirtualVoices{128};
    uint32_t stolenVoicesTotal{0};
    float listenerX{0.0f};
    float listenerY{0.0f};
    float listenerZ{0.0f};
    bool listenerSubmerged{false};
    float maxOcclusionInScene{0.0f};
};

} // namespace WiiU
