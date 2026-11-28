#pragma once

#include "Graphics/Color.hpp"
#include "Input/InputManager.hpp"
#include <string_view>
#include <functional>

namespace Neleo {

struct UIIntRect {
    int x{0};
    int y{0};
    int w{0};
    int h{0};
};

/**
 * @brief Reusable, lightweight UI Button component for Wii U touch/screen interfaces.
 */
class UIButton {
public:
    UIButton();
    UIButton(int x, int y, int w, int h, std::string_view label, int textScale = 2);

    void SetRect(int x, int y, int w, int h);
    void SetLabel(std::string_view label) noexcept;
    void SetTextScale(int scale) noexcept { m_textScale = scale; }
    void SetColors(ColorRGBA normalFill, ColorRGBA hoverFill,
                   ColorRGBA normalBorder, ColorRGBA hoverBorder,
                   ColorRGBA textColor);

    /**
     * @brief Updates hover state and checks if button was tapped.
     * @return true if button was just pressed/clicked.
     */
    bool Update(const TouchState& touch, const char* actionLabel = nullptr);

    /**
     * @brief Renders button background, border, and centered text.
     */
    void Render() const;

    [[nodiscard]] bool IsHovered() const noexcept { return m_hovered; }
    void SetHovered(bool hovered) noexcept { m_hovered = hovered; }
    [[nodiscard]] const UIIntRect& GetRect() const noexcept { return m_rect; }
    [[nodiscard]] std::string_view GetLabel() const noexcept { return std::string_view(m_label); }

private:
    UIIntRect m_rect{0, 0, 0, 0};
    char m_label[96]{0};
    int m_textScale{2};
    bool m_hovered{false};

    ColorRGBA m_normalFill{20, 30, 50, 220};
    ColorRGBA m_hoverFill{200, 40, 40, 230};
    ColorRGBA m_normalBorder{0, 200, 240, 255}; // Cyan
    ColorRGBA m_hoverBorder{255, 220, 0, 255};  // Yellow
    ColorRGBA m_textColor{255, 255, 255, 255};  // White
};

/**
 * @brief Preconfigured standard back button (< VOLVER) placed at top-left.
 */
class UIBackButton {
public:
    UIBackButton();
    explicit UIBackButton(std::function<void()> onBack);

    void SetOnBack(std::function<void()> onBack) { m_onBack = std::move(onBack); }
    void RefreshLabel() noexcept;

    /**
     * @brief Updates button and triggers menu transition when tapped.
     * @return true if back action was triggered and scene change occurred.
     */
    bool UpdateAndHandle(const TouchState& touch);

    void Render() const;

private:
    UIButton m_button;
    std::function<void()> m_onBack{nullptr};
};

/**
 * @brief Reusable interactive UI Slider component for touch/mouse input.
 */
class UISlider {
public:
    UISlider();
    UISlider(int x, int y, int w, int h, float minValue = 0.0f, float maxValue = 1.0f, float initialValue = 1.0f, std::string_view label = "");

    void SetRect(int x, int y, int w, int h);
    void SetRange(float minVal, float maxVal);
    void SetValue(float value);
    [[nodiscard]] float GetValue() const { return m_value; }

    void SetLabel(std::string_view label) noexcept;
    void SetColors(ColorRGBA trackFill, ColorRGBA progressFill, ColorRGBA handleFill, ColorRGBA border, ColorRGBA textColor);

    /**
     * @brief Updates slider value when touch or mouse drags across the track.
     * @return true if value was modified this frame.
     */
    bool Update(const TouchState& touch, const char* actionLabel = nullptr);

    /**
     * @brief Renders the track, active progress bar, sliding handle knob, and label.
     */
    void Render() const;

private:
    UIIntRect m_rect{0, 0, 0, 0};
    float m_minValue{0.0f};
    float m_maxValue{1.0f};
    float m_value{1.0f};
    char m_label[64]{0};
    bool m_dragging{false};
    bool m_hovered{false};

    ColorRGBA m_trackFill{15, 25, 40, 230};
    ColorRGBA m_progressFill{0, 190, 240, 240};
    ColorRGBA m_handleFill{255, 220, 0, 255};
    ColorRGBA m_border{0, 220, 255, 255};
    ColorRGBA m_textColor{255, 255, 255, 255};
};

} // namespace Neleo
