#include "Graphics/UIComponents.hpp"
#include "Graphics/Render2D.hpp"
#include "Core/Localization/LocalizationManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Audio/AudioManager.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace Neleo {

UIButton::UIButton() = default;

UIButton::UIButton(int x, int y, int w, int h, std::string_view label, int textScale)
    : m_rect{x, y, w, h}, m_textScale(textScale) {
    SetLabel(label);
}

void UIButton::SetLabel(std::string_view label) noexcept {
    size_t copyLen = std::min(label.size(), sizeof(m_label) - 1);
    if (copyLen > 0) {
        std::memcpy(m_label, label.data(), copyLen);
    }
    m_label[copyLen] = '\0';
}

void UIButton::SetRect(int x, int y, int w, int h) {
    m_rect = UIIntRect{x, y, w, h};
}

void UIButton::SetColors(ColorRGBA normalFill, ColorRGBA hoverFill,
                         ColorRGBA normalBorder, ColorRGBA hoverBorder,
                         ColorRGBA textColor) {
    m_normalFill = normalFill;
    m_hoverFill = hoverFill;
    m_normalBorder = normalBorder;
    m_hoverBorder = hoverBorder;
    m_textColor = textColor;
}

bool UIButton::Update(const TouchState& touch, const char* actionLabel) {
    m_hovered = false;

    if (!touch.active) {
        return false;
    }

    const bool insideX = (touch.x >= m_rect.x) && (touch.x <= (m_rect.x + m_rect.w));
    const bool insideY = (touch.y >= m_rect.y) && (touch.y <= (m_rect.y + m_rect.h));

    if (insideX && insideY) {
        m_hovered = true;
        if (touch.justPressed) {
            if (actionLabel) {
                InputManager::Instance().TriggerAction(actionLabel);
            }
            return true;
        }
    }

    return false;
}

void UIButton::Render() const {
    const ColorRGBA& fill = m_hovered ? m_hoverFill : m_normalFill;
    const ColorRGBA& border = m_hovered ? m_hoverBorder : m_normalBorder;

    Render2D::DrawBox(m_rect.x, m_rect.y, m_rect.w, m_rect.h, fill, border);

    if (m_label[0] != '\0') {
        const size_t len = std::strlen(m_label);
        const int textWidth = static_cast<int>(len) * 6 * m_textScale;
        const int textHeight = 7 * m_textScale;
        const int textX = m_rect.x + std::max(10, (m_rect.w - textWidth) / 2);
        const int textY = m_rect.y + std::max(5, (m_rect.h - textHeight) / 2);

        Render2D::DrawPixelString(m_label, textX, textY, m_textScale, m_textColor);
    }
}

// UIBackButton Implementation
UIBackButton::UIBackButton()
    : m_button(40, 35, 200, 46, NELEO_LOC("ui.back"), 3) {
    m_button.SetColors(
        ColorRGBA{20, 30, 50, 220},   // Normal Fill
        ColorRGBA{200, 40, 40, 230},  // Hover Fill
        ColorRGBA::Cyan(),            // Normal Border
        ColorRGBA::Yellow(),          // Hover Border
        ColorRGBA::White()            // Text Color
    );
}

void UIBackButton::RefreshLabel() noexcept {
    m_button.SetLabel(NELEO_LOC("ui.back"));
}

UIBackButton::UIBackButton(std::function<void()> onBack)
    : UIBackButton() {
    m_onBack = std::move(onBack);
}

bool UIBackButton::UpdateAndHandle(const TouchState& touch) {
    const bool touchClicked = m_button.Update(touch, "VOLVER");
    const bool backRequested = InputManager::Instance().IsBackRequested();

    if (touchClicked || backRequested) {
        AudioManager::Instance().PlayClick();
        if (m_onBack) {
            m_onBack();
        } else {
            SceneManager::Instance().NavigateToHomeMenu();
        }
        return true;
    }
    return false;
}

void UIBackButton::Render() const {
    m_button.Render();
}

// UISlider Implementation
UISlider::UISlider() = default;

UISlider::UISlider(int x, int y, int w, int h, float minValue, float maxValue, float initialValue, std::string_view label)
    : m_rect{x, y, w, h},
      m_minValue(minValue),
      m_maxValue(maxValue),
      m_value(std::clamp(initialValue, minValue, maxValue)) {
    SetLabel(label);
}

void UISlider::SetRect(int x, int y, int w, int h) {
    m_rect = UIIntRect{x, y, w, h};
}

void UISlider::SetRange(float minVal, float maxVal) {
    if (minVal > maxVal) std::swap(minVal, maxVal);
    m_minValue = minVal;
    m_maxValue = maxVal;
    m_value = std::clamp(m_value, m_minValue, m_maxValue);
}

void UISlider::SetValue(float value) {
    m_value = std::clamp(value, m_minValue, m_maxValue);
}

void UISlider::SetLabel(std::string_view label) noexcept {
    size_t copyLen = std::min(label.size(), sizeof(m_label) - 1);
    if (copyLen > 0) {
        std::memcpy(m_label, label.data(), copyLen);
    }
    m_label[copyLen] = '\0';
}

void UISlider::SetColors(ColorRGBA trackFill, ColorRGBA progressFill, ColorRGBA handleFill, ColorRGBA border, ColorRGBA textColor) {
    m_trackFill = trackFill;
    m_progressFill = progressFill;
    m_handleFill = handleFill;
    m_border = border;
    m_textColor = textColor;
}

bool UISlider::Update(const TouchState& touch, const char* actionLabel) {
    m_hovered = false;

    if (!touch.active) {
        m_dragging = false;
        return false;
    }

    constexpr int hitMarginY = 16;
    const bool insideX = (touch.x >= m_rect.x) && (touch.x <= (m_rect.x + m_rect.w));
    const bool insideY = (touch.y >= (m_rect.y - hitMarginY)) && (touch.y <= (m_rect.y + m_rect.h + hitMarginY));

    if (insideX && insideY) {
        m_hovered = true;
        if (touch.justPressed) {
            m_dragging = true;
        }
    }

    if (m_dragging) {
        float normalized = (m_rect.w > 0) ? static_cast<float>(touch.x - m_rect.x) / static_cast<float>(m_rect.w) : 0.0f;
        normalized = std::clamp(normalized, 0.0f, 1.0f);
        float newVal = m_minValue + normalized * (m_maxValue - m_minValue);

        if (std::abs(newVal - m_value) > 0.005f) {
            m_value = newVal;
            if (actionLabel && touch.justPressed) {
                InputManager::Instance().TriggerAction(actionLabel);
            }
            return true;
        }
    }

    return false;
}

void UISlider::Render() const {
    // 1. Pista de fondo (Track)
    Render2D::DrawBox(m_rect.x, m_rect.y, m_rect.w, m_rect.h, m_trackFill, m_border);

    // 2. Progreso activo (Fill)
    float normalized = (m_maxValue > m_minValue) ? (m_value - m_minValue) / (m_maxValue - m_minValue) : 0.0f;
    normalized = std::clamp(normalized, 0.0f, 1.0f);
    int fillW = static_cast<int>((m_rect.w - 4) * normalized);
    if (fillW > 0) {
        Render2D::DrawBox(m_rect.x + 2, m_rect.y + 2, fillW, m_rect.h - 4, m_progressFill, m_progressFill);
    }

    // 3. Tirador deslizante (Handle)
    int handleX = m_rect.x + static_cast<int>(m_rect.w * normalized) - 7;
    int handleY = m_rect.y - 4;
    int handleW = 14;
    int handleH = m_rect.h + 8;
    ColorRGBA handleBorder = m_dragging ? ColorRGBA::White() : ColorRGBA::Yellow();
    Render2D::DrawBox(handleX, handleY, handleW, handleH, m_handleFill, handleBorder);

    // 4. Etiqueta superior (Cero alocaciones dinámicas a 60 FPS)
    if (m_label[0] != '\0') {
        int percent = static_cast<int>(std::round(normalized * 100.0f));
        char fullText[96];
        int written = std::snprintf(fullText, sizeof(fullText), "%s: %d %%", m_label, percent);
        if (written > 0) {
            const int boundedLen = std::min<int>(written, static_cast<int>(sizeof(fullText) - 1));
            const int textW = boundedLen * 6 * 2;
            const int textX = m_rect.x + std::max(0, (m_rect.w - textW) / 2);
            const int textY = m_rect.y - 24;
            Render2D::DrawPixelString(fullText, textX, textY, 2, m_textColor);
        }
    }
}

} // namespace Neleo
