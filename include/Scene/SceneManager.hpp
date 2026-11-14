#pragma once

#include "Scene/IScene.hpp"
#include <SDL2/SDL.h>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <cstdint>
#include <functional>

namespace WiiU {

/**
 * @brief Fases del ciclo de vida en la máquina de estados de transición de escenas.
 */
enum class TransitionPhase : uint8_t {
    Idle = 0,          // Escena activa en ejecución normal
    DrainingGPU,       // Espera a que la GPU Latte concluya comandos en vuelo (GX2DrawDone)
    UnloadingScene,    // Invocación de OnExit(), destrucción de escena y auditoría de memoria
    LoadingScene,      // Validación de assets, instanciación e invocación de OnEnter()
    Finalizing         // Snap a 100%, presentación fluida y retorno a Idle
};

/**
 * @brief Gestor central de ciclo de vida de escenas, transiciones progresivas y VSync.
 * 
 * Implementa:
 * 1. Proceso determinista de carga y descarga de escenas con máquina de estados finita.
 * 2. Pantalla de carga y liberación de recursos progresiva animada en tiempo real.
 * 3. Barrera de sincronización con la GPU (GX2DrawDone) para prevenir colisiones en VRAM.
 * 4. Auditoría estricta de memoria (MemoryManager) al cerrar o alternar escenas.
 */
class SceneManager {
public:
    static SceneManager& Instance() {
        static SceneManager instance;
        return instance;
    }

    using SceneFactoryFn = std::function<std::unique_ptr<IScene>()>;
    using OptionsSceneFactoryFn = std::function<std::unique_ptr<IScene>(std::function<void()> onBack)>;

    void SetHomeMenuFactory(SceneFactoryFn factory) { m_homeMenuFactory = std::move(factory); }
    void SetModelsMenuFactory(SceneFactoryFn factory) { m_modelsMenuFactory = std::move(factory); }
    void SetOptionsSceneFactory(OptionsSceneFactoryFn factory) { m_optionsSceneFactory = std::move(factory); }

    bool NavigateToHomeMenu();
    bool NavigateToParentMenu(bool fromPureGX2);
    bool NavigateToOptions(bool fromPureGX2);

    void ChangeScene(std::unique_ptr<IScene> newScene);
    void Update(float dt);
    void Render(SDL_Renderer* renderer);

    [[nodiscard]] IScene* GetCurrentScene() const noexcept { return m_currentScene.get(); }
    [[nodiscard]] bool IsTransitioning() const noexcept { return m_transitionPhase != TransitionPhase::Idle; }
    [[nodiscard]] TransitionPhase GetTransitionPhase() const noexcept { return m_transitionPhase; }
    [[nodiscard]] float GetTransitionProgress() const noexcept { return m_transitionProgress; }

    bool IsFPSLimitEnabled() const noexcept { return m_fpsLimitEnabled; }
    void SetFPSLimitEnabled(bool enabled);
    void ToggleFPSLimit();

    // Sistema de Pausa Centralizado y Modal Global
    [[nodiscard]] bool IsPaused() const noexcept { return m_isPaused; }
    void SetPaused(bool paused);
    void TogglePause();

    void Clear();

private:
    SceneManager() = default;
    ~SceneManager() = default;

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    void UpdateTransition(float dt);
    void RenderTransitionScreen(SDL_Renderer* renderer);

    void UpdatePauseModal(float dt);
    void RenderPauseModal(SDL_Renderer* renderer);

    std::unique_ptr<IScene> m_currentScene{nullptr};
    std::unique_ptr<IScene> m_pendingScene{nullptr};

    TransitionPhase m_transitionPhase{TransitionPhase::Idle};
    std::string m_outgoingSceneName;
    std::string m_incomingSceneName;
    bool m_outgoingWasMenu{false};

    float m_transitionProgress{0.0f};
    float m_targetProgress{0.0f};
    float m_phaseTimer{0.0f};
    float m_pulseAnim{0.0f};
    size_t m_reclaimedBytes{0};

    // Estado de Carga Asíncrona en Core 2 (Pilar 2 — Jason Gregory Cap. 7)
    std::atomic<uint32_t> m_asyncLoadCounter{0};
    bool m_asyncLoadDispatched{false};
    bool m_asyncAssetsValid{true};
    std::vector<std::string> m_asyncMissingAssets;

    bool m_fpsLimitEnabled{true};

    // Estado del Sistema de Pausa y Modal
    bool m_isPaused{false};
    float m_pauseAnimTimer{0.0f};
    bool m_resumeHovered{false};
    bool m_optionsHovered{false};
    bool m_menuHovered{false};

    SceneFactoryFn m_homeMenuFactory{};
    SceneFactoryFn m_modelsMenuFactory{};
    OptionsSceneFactoryFn m_optionsSceneFactory{};
};

} // namespace WiiU
