#include "Core/ApplicationLifecycle.hpp"

#if defined(__WIIU__) || defined(__wiiu__)
#include "Core/Window.hpp"
#include <proc_ui/procui.h>
#endif

namespace WiiU {

ApplicationLifecycle& ApplicationLifecycle::Instance() noexcept {
    static ApplicationLifecycle instance;
    return instance;
}

void ApplicationLifecycle::Initialize() noexcept {
    m_state.store(static_cast<uint8_t>(AppLifecycleState::Boot), std::memory_order_release);
    TransitionTo(AppLifecycleState::Foreground);
}

void ApplicationLifecycle::Shutdown() noexcept {
    TransitionTo(AppLifecycleState::Exiting);
    for (auto& slot : m_observers) {
        slot = ObserverSlot{};
    }
}

void ApplicationLifecycle::RequestExit() noexcept {
    TransitionTo(AppLifecycleState::Exiting);
}

void ApplicationLifecycle::PollPlatformState(bool externalExitRequested) noexcept {
    if (externalExitRequested) {
        TransitionTo(AppLifecycleState::Exiting);
        return;
    }

#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIInShutdown() || !ProcUIIsRunning() || Window::IsExitingProcUI()) {
        TransitionTo(AppLifecycleState::Exiting);
        return;
    }
    if (!Window::HasForeground() || !ProcUIInForeground()) {
        if (GetState() == AppLifecycleState::Foreground) {
            TransitionTo(AppLifecycleState::Suspending);
        }
        TransitionTo(AppLifecycleState::Background);
        return;
    }
    TransitionTo(AppLifecycleState::Foreground);
#else
    if (GetState() == AppLifecycleState::Boot) {
        TransitionTo(AppLifecycleState::Foreground);
    }
#endif
}

uint32_t ApplicationLifecycle::AddObserver(LifecycleObserverFn fn, void* userData) noexcept {
    if (!fn) return 0;
    for (auto& slot : m_observers) {
        if (slot.id == 0) {
            slot.id = m_nextObserverId++;
            if (m_nextObserverId == 0) m_nextObserverId = 1;
            slot.fn = fn;
            slot.userData = userData;
            return slot.id;
        }
    }
    return 0;
}

void ApplicationLifecycle::RemoveObserver(uint32_t observerId) noexcept {
    if (observerId == 0) return;
    for (auto& slot : m_observers) {
        if (slot.id == observerId) {
            slot = ObserverSlot{};
            return;
        }
    }
}

void ApplicationLifecycle::TransitionTo(AppLifecycleState nextState) noexcept {
    uint8_t prevRaw = m_state.load(std::memory_order_acquire);
    const uint8_t nextRaw = static_cast<uint8_t>(nextState);
    while (true) {
        const auto prevState = static_cast<AppLifecycleState>(prevRaw);
        if (prevState == nextState || prevState == AppLifecycleState::Exiting) {
            return;
        }
        if (m_state.compare_exchange_weak(prevRaw, nextRaw,
                                          std::memory_order_acq_rel, std::memory_order_acquire)) {
            for (const auto& slot : m_observers) {
                if (slot.id != 0 && slot.fn != nullptr) {
                    slot.fn(prevState, nextState, slot.userData);
                }
            }
            return;
        }
    }
}

} // namespace WiiU
