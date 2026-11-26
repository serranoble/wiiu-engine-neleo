#pragma once

#include <array>
#include <cstddef>
#include <memory>

namespace Neleo {

/**
 * @brief [OPT-233 / OPT-314] Interfaz para capas superpuestas compositables (modales, inventarios, dialogos, terminal CVar).
 */
class ISceneOverlay {
public:
    virtual ~ISceneOverlay() = default;

    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void Update(float dt) = 0;
    virtual void Render() = 0;

    [[nodiscard]] virtual bool BlocksSceneUpdate() const noexcept { return true; }
    [[nodiscard]] virtual bool BlocksSceneInput() const noexcept { return true; }
    [[nodiscard]] virtual const char* GetOverlayName() const noexcept { return "Overlay"; }
};

/**
 * @brief [OPT-233 / OPT-314] Pila de Modales y Overlays Compositados (ModalStack).
 *
 * Permite apilar hasta 8 capas UI sobre la escena activa sin destruirla ni recargar VRAM.
 * - Actualizacion top-down respetando BlocksSceneUpdate().
 * - Renderizado bottom-up unificado sobre Render2D / GX2.
 */
class ModalStack {
public:
    static constexpr size_t MAX_OVERLAYS = 8;

    ModalStack() = default;
    ~ModalStack() { Clear(); }
    ModalStack(const ModalStack&) = delete;
    ModalStack& operator=(const ModalStack&) = delete;
    ModalStack(ModalStack&&) = delete;
    ModalStack& operator=(ModalStack&&) = delete;

    bool Push(std::unique_ptr<ISceneOverlay> overlay);
    bool Pop();
    void Clear();

    void Update(float dt);
    void Render();

    [[nodiscard]] bool IsBlockingSceneUpdate() const noexcept;
    [[nodiscard]] bool IsBlockingSceneInput() const noexcept;
    [[nodiscard]] size_t GetDepth() const noexcept { return m_count; }
    [[nodiscard]] bool IsEmpty() const noexcept { return m_count == 0; }
    [[nodiscard]] ISceneOverlay* GetTop() const noexcept {
        return (m_count > 0) ? m_overlays[m_count - 1].get() : nullptr;
    }

private:
    std::array<std::unique_ptr<ISceneOverlay>, MAX_OVERLAYS> m_overlays{};
    size_t m_count{0};
};

} // namespace Neleo
