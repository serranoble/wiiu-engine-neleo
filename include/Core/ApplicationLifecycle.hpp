#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Neleo {

/**
 * @brief [OPT-50 / OPT-136 / OPT-210] Maquina de Estados del Ciclo de Vida de la Aplicacion (ALSM).
 */
enum class AppLifecycleState : uint8_t {
    Boot = 0,
    Foreground,
    Background,
    Suspending,
    Exiting
};

[[nodiscard]] inline const char* GetAppLifecycleStateName(AppLifecycleState state) noexcept {
    switch (state) {
        case AppLifecycleState::Boot:       return "BOOT";
        case AppLifecycleState::Foreground: return "FOREGROUND";
        case AppLifecycleState::Background: return "BACKGROUND";
        case AppLifecycleState::Suspending: return "SUSPENDING";
        case AppLifecycleState::Exiting:    return "EXITING";
        default:                            return "UNKNOWN";
    }
}

using LifecycleObserverFn = void (*)(AppLifecycleState oldState, AppLifecycleState newState, void* userData);

/**
 * @brief [OPT-50 / OPT-136 / OPT-210] Observador Hardware-Agnostico de Ciclo de Vida y ProcUI.
 *
 * Desacopla la gestion de primer plano, suspension del Menu HOME y cierre limpio de Cafe OS
 * (y futuras plataformas Switch/PC), proporcionando una senal preventiva de aborto de frame
 * sin alocaciones dinamicas.
 */
class ApplicationLifecycle {
public:
    static constexpr size_t MAX_OBSERVERS = 16;

    static ApplicationLifecycle& Instance() noexcept;

    ApplicationLifecycle(const ApplicationLifecycle&) = delete;
    ApplicationLifecycle& operator=(const ApplicationLifecycle&) = delete;
    ApplicationLifecycle(ApplicationLifecycle&&) = delete;
    ApplicationLifecycle& operator=(ApplicationLifecycle&&) = delete;

    void Initialize() noexcept;
    void Shutdown() noexcept;

    /**
     * @brief Sondea el estado nativo del SO (ProcUI en Wii U / eventos SDL) y dispara transiciones.
     */
    void PollPlatformState(bool externalExitRequested) noexcept;

    void RequestExit() noexcept;

    uint32_t AddObserver(LifecycleObserverFn fn, void* userData = nullptr) noexcept;
    void RemoveObserver(uint32_t observerId) noexcept;

    [[nodiscard]] AppLifecycleState GetState() const noexcept {
        return static_cast<AppLifecycleState>(m_state.load(std::memory_order_acquire));
    }
    [[nodiscard]] bool IsForeground() const noexcept { return GetState() == AppLifecycleState::Foreground; }
    [[nodiscard]] bool IsExiting() const noexcept { return GetState() == AppLifecycleState::Exiting; }
    [[nodiscard]] bool ShouldContinueRunning() const noexcept { return !IsExiting(); }

    /**
     * @brief Senal preventiva para abortar pases de GPU/rendering cuando el proceso pierde primer plano.
     */
    [[nodiscard]] bool IsFrameAbortRequested() const noexcept {
        const AppLifecycleState st = GetState();
        return st != AppLifecycleState::Foreground;
    }

private:
    ApplicationLifecycle() noexcept = default;
    ~ApplicationLifecycle() noexcept = default;

    void TransitionTo(AppLifecycleState nextState) noexcept;

    struct ObserverSlot {
        uint32_t id{0};
        LifecycleObserverFn fn{nullptr};
        void* userData{nullptr};
    };

    std::atomic<uint8_t> m_state{static_cast<uint8_t>(AppLifecycleState::Boot)};
    std::array<ObserverSlot, MAX_OBSERVERS> m_observers{};
    uint32_t m_nextObserverId{1};
};

} // namespace Neleo
