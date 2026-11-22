#pragma once

#include "Graphics/Math3D.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace WiiU {

/**
 * @brief [Hito 133] Tipos de eventos de gameplay desacoplados (Jason Gregory, Cap. 18).
 */
enum class GameplayEventType : uint8_t {
    None = 0,
    CollectiblePicked,
    BouncePadActivated,
    LevelGoalReached,
    RadialExplosion,
    TimeScaleChanged,
    AcousticZoneChanged,
    CameraModeChanged
};

/**
 * @brief Mensaje tipado de tamaño fijo para cola circular sin asignaciones en heap.
 */
struct GameplayEvent {
    GameplayEventType type{GameplayEventType::None};
    uint32_t sourceEntityId{0};
    uint32_t targetEntityId{0};
    float value{0.0f};
    Point3D worldPosition{0.0f, 0.0f, 0.0f};
};

/**
 * @brief Notificacion visual efimera (Toast) en el HUD generada a partir de eventos.
 */
struct GameplayToast {
    std::array<char, 48> message{};
    uint32_t colorRGBA{0xFFFFFFFF};
    float timeRemaining{0.0f};
};

/**
 * @brief [Hito 133] Bus de Eventos de Gameplay y Cola de Toasts (0 alloc/frame, Cap. 18).
 *
 * Implementa un ring buffer de capacidad fija (`MAX_QUEUED_EVENTS = 64`) junto con
 * un historial efimero de hasta `MAX_TOASTS = 3` notificaciones visibles en HUD.
 */
class GameplayEventBus {
public:
    static constexpr size_t MAX_QUEUED_EVENTS = 64;
    static constexpr size_t MAX_TOASTS = 3;

    GameplayEventBus() noexcept = default;

    void Clear() noexcept {
        m_head = 0;
        m_tail = 0;
        m_count = 0;
        m_totalPublished = 0;
        for (auto& toast : m_toasts) {
            toast.message[0] = '\0';
            toast.timeRemaining = 0.0f;
            toast.colorRGBA = 0xFFFFFFFF;
        }
    }

    /**
     * @brief Publica un evento en la cola circular y opcionalmente registra un Toast en HUD.
     */
    bool Publish(const GameplayEvent& ev,
                 const char* toastText = nullptr,
                 uint32_t toastColor = 0x80FFCCFF,
                 float toastDuration = 2.8f) noexcept {
        if (toastText != nullptr && toastText[0] != '\0') {
            PushToast(toastText, toastColor, toastDuration);
        }

        if (m_count >= MAX_QUEUED_EVENTS) {
            // Sobrescribe el evento mas antiguo para garantizar avance sin bloquear
            m_head = (m_head + 1) % MAX_QUEUED_EVENTS;
            --m_count;
        }

        m_queue[m_tail] = ev;
        m_tail = (m_tail + 1) % MAX_QUEUED_EVENTS;
        ++m_count;
        ++m_totalPublished;
        return true;
    }

    /**
     * @brief Extrae el siguiente evento pendiente en orden FIFO.
     */
    bool Poll(GameplayEvent& outEvent) noexcept {
        if (m_count == 0) {
            return false;
        }
        outEvent = m_queue[m_head];
        m_head = (m_head + 1) % MAX_QUEUED_EVENTS;
        --m_count;
        return true;
    }

    void UpdateToasts(float realDt) noexcept {
        if (realDt <= 0.0f) {
            return;
        }
        for (auto& toast : m_toasts) {
            if (toast.timeRemaining > 0.0f) {
                toast.timeRemaining = std::max(0.0f, toast.timeRemaining - realDt);
                if (toast.timeRemaining == 0.0f) {
                    toast.message[0] = '\0';
                }
            }
        }
    }

    [[nodiscard]] const std::array<GameplayToast, MAX_TOASTS>& GetToasts() const noexcept {
        return m_toasts;
    }

    [[nodiscard]] uint32_t GetTotalPublished() const noexcept {
        return m_totalPublished;
    }

    [[nodiscard]] uint32_t GetPendingCount() const noexcept {
        return static_cast<uint32_t>(m_count);
    }

private:
    void PushToast(const char* text, uint32_t colorRGBA, float durationSec) noexcept {
        // Desplaza los toasts existentes hacia abajo para que el indice 0 sea el mas reciente
        for (size_t i = MAX_TOASTS - 1; i > 0; --i) {
            m_toasts[i] = m_toasts[i - 1];
        }
        GameplayToast& top = m_toasts[0];
        std::snprintf(top.message.data(), top.message.size(), "%s", text ? text : "");
        top.colorRGBA = colorRGBA;
        top.timeRemaining = std::clamp(durationSec, 0.1f, 10.0f);
    }

    std::array<GameplayEvent, MAX_QUEUED_EVENTS> m_queue{};
    std::array<GameplayToast, MAX_TOASTS> m_toasts{};
    size_t m_head{0};
    size_t m_tail{0};
    size_t m_count{0};
    uint32_t m_totalPublished{0};
};

} // namespace WiiU
