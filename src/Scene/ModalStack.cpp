#include "Scene/ModalStack.hpp"
#include "Input/InputManager.hpp"

namespace Neleo {

bool ModalStack::Push(std::unique_ptr<ISceneOverlay> overlay) {
    if (!overlay || m_count >= MAX_OVERLAYS) {
        return false;
    }
    const bool wasBlockingInput = IsBlockingSceneInput();
    ISceneOverlay* raw = overlay.get();
    m_overlays[m_count] = std::move(overlay);
    ++m_count;
    raw->OnAttach();

    if (!wasBlockingInput && IsBlockingSceneInput()) {
        InputManager::Instance().GetContextStack().PushContext(InputContext::UIModal, 100u, true);
    }
    return true;
}

bool ModalStack::Pop() {
    if (m_count == 0) return false;
    --m_count;
    std::unique_ptr<ISceneOverlay> detached = std::move(m_overlays[m_count]);
    if (detached) {
        detached->OnDetach();
        detached.reset();
    }
    if (!IsBlockingSceneInput()) {
        InputManager::Instance().GetContextStack().RemoveContext(InputContext::UIModal);
    }
    return true;
}

void ModalStack::Clear() {
    while (m_count > 0) {
        --m_count;
        std::unique_ptr<ISceneOverlay> detached = std::move(m_overlays[m_count]);
        if (detached) {
            detached->OnDetach();
            detached.reset();
        }
    }
    InputManager::Instance().GetContextStack().RemoveContext(InputContext::UIModal);
}

void ModalStack::Update(float dt) {
    for (size_t i = m_count; i > 0; --i) {
        if (i > m_count) {
            i = m_count;
            if (i == 0) break;
        }
        ISceneOverlay* overlay = m_overlays[i - 1].get();
        if (!overlay) continue;
        // Capturar politica ANTES de Update() porque el overlay puede auto-cerrarse (Pop/Clear).
        const bool blocksUpdate = overlay->BlocksSceneUpdate();
        overlay->Update(dt);
        if (blocksUpdate) {
            break;
        }
    }
}

void ModalStack::Render() {
    for (size_t i = 0; i < m_count; ++i) {
        if (m_overlays[i]) {
            m_overlays[i]->Render();
        }
    }
}

bool ModalStack::IsBlockingSceneUpdate() const noexcept {
    for (size_t i = 0; i < m_count; ++i) {
        if (m_overlays[i] && m_overlays[i]->BlocksSceneUpdate()) {
            return true;
        }
    }
    return false;
}

bool ModalStack::IsBlockingSceneInput() const noexcept {
    for (size_t i = 0; i < m_count; ++i) {
        if (m_overlays[i] && m_overlays[i]->BlocksSceneInput()) {
            return true;
        }
    }
    return false;
}

} // namespace Neleo
