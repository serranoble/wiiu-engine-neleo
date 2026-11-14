#include "Graphics/UI/UIToolkit.hpp"
#include "Graphics/Render2D.hpp"
#include <cstdio>
#include <cstring>

namespace WiiU::UI {

const UITheme& UITheme::Default() noexcept {
    static const UITheme s_defaultTheme{};
    return s_defaultTheme;
}

UILayoutRow::UILayoutRow(float startX, float startY, float totalWidth, float height, int numItems, float gap) noexcept
    : m_startX(SnapToPixel(startX)),
      m_startY(SnapToPixel(startY)),
      m_height(SnapToPixel(height)),
      m_gap(SnapToPixel(gap)),
      m_numItems(numItems > 0 ? numItems : 1) {
    
    const float availableWidth = totalWidth - static_cast<float>(m_numItems - 1) * m_gap;
    m_itemWidth = SnapToPixel(availableWidth / static_cast<float>(m_numItems));
    if (m_itemWidth < 1.0f) {
        m_itemWidth = 1.0f;
    }
}

UIRect UILayoutRow::GetCell(int index) const noexcept {
    if (index < 0 || index >= m_numItems) {
        return UIRect(m_startX, m_startY, m_itemWidth, m_height);
    }
    const float cellX = SnapToPixel(m_startX + static_cast<float>(index) * (m_itemWidth + m_gap));
    return UIRect(cellX, m_startY, m_itemWidth, m_height);
}

#if defined(__WIIU__) || defined(__wiiu__)
ScopedUIPass::ScopedUIPass(GX2ContextState* contextState, GX2ColorBuffer* colorBuffer) {
    UIToolkit::BeginPass(contextState, colorBuffer);
}
#else
ScopedUIPass::ScopedUIPass() {
    UIToolkit::BeginPass();
}
#endif

ScopedUIPass::~ScopedUIPass() {
    UIToolkit::EndPass();
}

#if defined(__WIIU__) || defined(__wiiu__)
void UIToolkit::BeginPass(GX2ContextState* contextState, GX2ColorBuffer* colorBuffer) {
    if (contextState) {
        GX2SetContextState(contextState);
    }
    if (colorBuffer) {
        GX2SetColorBuffer(colorBuffer, GX2_RENDER_TARGET_0);
    }
    Render2DGX2::Instance().Begin();
}
#else
void UIToolkit::BeginPass() {
    Render2DGX2::Instance().Begin();
}
#endif

void UIToolkit::EndPass() {
    Render2DGX2::Instance().End();
}

void UIToolkit::SetTheme(const UITheme& theme) noexcept {
    s_activeTheme = theme;
}

const UITheme& UIToolkit::GetTheme() noexcept {
    return s_activeTheme;
}

void UIToolkit::DrawPanel(const UIRect& rect,
                          const ColorRGBA* customBg,
                          const ColorRGBA* customBorder,
                          float borderWidth) {
    const ColorRGBA& bg = customBg ? *customBg : s_activeTheme.panelBg;
    const ColorRGBA& border = customBorder ? *customBorder : s_activeTheme.panelBorder;
    Render2DGX2::Instance().DrawBox(rect.x, rect.y, rect.w, rect.h, bg, border, borderWidth);
}

void UIToolkit::DrawButton(const UIRect& rect,
                           std::string_view label,
                           bool isHovered,
                           TextScale scale,
                           const ColorRGBA* customBg,
                           const ColorRGBA* customBorder,
                           const ColorRGBA* customText) {
    // Si la etiqueta indica regreso ("< VOLVER"), aplicar tema de peligro/retorno si no hay override
    const bool isBackButton = (!label.empty() && label[0] == '<');

    ColorRGBA bg = customBg ? *customBg :
        (isBackButton ? (isHovered ? s_activeTheme.buttonDangerHover : s_activeTheme.buttonDangerNormal)
                      : (isHovered ? s_activeTheme.buttonHover : s_activeTheme.buttonNormal));

    ColorRGBA border = customBorder ? *customBorder :
        (isBackButton ? s_activeTheme.buttonDangerBorder
                      : (isHovered ? s_activeTheme.buttonBorderHover : s_activeTheme.buttonBorderNormal));

    ColorRGBA text = customText ? *customText :
        (isHovered ? s_activeTheme.textAccentYellow : s_activeTheme.textPrimary);

    Render2DGX2::Instance().DrawButton(label, rect.x, rect.y, rect.w, rect.h, isHovered,
                                      bg, bg, border, border, text,
                                      static_cast<float>(scale));
}

void UIToolkit::DrawToggleButton(const UIRect& rect,
                                 std::string_view label,
                                 bool isActive,
                                 bool isHovered,
                                 TextScale scale) {
    const ColorRGBA& border = isHovered ? s_activeTheme.buttonBorderHover
                                        : (isActive ? s_activeTheme.buttonActiveBorder : s_activeTheme.buttonBorderNormal);
    const ColorRGBA& bg = isHovered ? s_activeTheme.buttonHover : s_activeTheme.buttonNormal;
    const ColorRGBA& textColor = isActive ? s_activeTheme.textAccentGreen : s_activeTheme.textDanger;

    Render2DGX2::Instance().DrawButton(label, rect.x, rect.y, rect.w, rect.h, isHovered,
                                      bg, bg, border, border, textColor,
                                      static_cast<float>(scale));
}

void UIToolkit::DrawValueButton(const UIRect& rect,
                                std::string_view label,
                                std::string_view value,
                                bool isHovered,
                                TextScale scale,
                                const ColorRGBA* customValueColor) {
    const ColorRGBA& bg = isHovered ? s_activeTheme.buttonHover : s_activeTheme.buttonNormal;
    const ColorRGBA& border = isHovered ? s_activeTheme.buttonBorderHover : s_activeTheme.buttonBorderNormal;
    const ColorRGBA& valColor = customValueColor ? *customValueColor : s_activeTheme.textAccentCyan;

    // Dibujar caja contenedora
    Render2DGX2::Instance().DrawBox(rect.x, rect.y, rect.w, rect.h, bg, border, 2.0f);

    const float s = static_cast<float>(scale);
    const float labelWidth = static_cast<float>(label.length() * 6) * s;
    const float valueWidth = static_cast<float>(value.length() * 6) * s;
    const float totalWidth = labelWidth + valueWidth + (4.0f * s);
    const float textHeight = 7.0f * s;

    // Centrado horizontal conjunto con ajuste defensivo
    float startX = SnapToPixel(rect.x + (rect.w - totalWidth) * 0.5f);
    if (startX < rect.x + 4.0f) startX = rect.x + 4.0f;
    const float startY = SnapToPixel(rect.y + (rect.h - textHeight) * 0.5f);

    Render2DGX2::Instance().DrawText(label, startX, startY, s, s_activeTheme.textSecondary);
    Render2DGX2::Instance().DrawText(value, startX + labelWidth + (4.0f * s), startY, s, valColor);
}

void UIToolkit::DrawLabel(float x, float y,
                          std::string_view text,
                          TextScale scale,
                          const ColorRGBA& color) {
    Render2DGX2::Instance().DrawText(text, SnapToPixel(x), SnapToPixel(y), static_cast<float>(scale), color);
}

void UIToolkit::DrawProgressBar(const UIRect& rect,
                                float fraction,
                                std::string_view label,
                                const ColorRGBA& barColor) {
    // Fondo y borde
    Render2DGX2::Instance().DrawBox(rect.x, rect.y, rect.w, rect.h,
                                    s_activeTheme.panelBg, s_activeTheme.panelBorder, 2.0f);

    const float clampedFrac = (fraction < 0.0f) ? 0.0f : ((fraction > 1.0f) ? 1.0f : fraction);
    const float fillW = SnapToPixel((rect.w - 4.0f) * clampedFrac);
    if (fillW > 0.0f) {
        Render2DGX2::Instance().DrawRect(rect.x + 2.0f, rect.y + 2.0f, fillW, rect.h - 4.0f, barColor);
    }

    if (!label.empty()) {
        const float textWidth = static_cast<float>(label.length() * 6) * 2.0f;
        const float tx = SnapToPixel(rect.x + (rect.w - textWidth) * 0.5f);
        const float ty = SnapToPixel(rect.y + (rect.h - 14.0f) * 0.5f);
        Render2DGX2::Instance().DrawText(label, tx, ty, 2.0f, s_activeTheme.textPrimary);
    }
}

void UIToolkit::DrawSeparator(float x, float y, float w, const ColorRGBA& color) {
    Render2DGX2::Instance().DrawRect(SnapToPixel(x), SnapToPixel(y), SnapToPixel(w), 2.0f, color);
}

void UIToolkit::DrawGuideBar(std::string_view text, float y, TextScale scale) {
    const UIRect rect(20.0f, y, 1240.0f, 30.0f);
    DrawPanel(rect, &s_activeTheme.backgroundBar, &s_activeTheme.panelBorder, 2.0f);

    const float textWidth = MeasureText(text, scale);
    const float textHeight = 7.0f * static_cast<float>(scale);
    float tx = SnapToPixel(rect.x + (rect.w - textWidth) * 0.5f);
    if (tx < rect.x + 10.0f) tx = rect.x + 10.0f;
    const float ty = SnapToPixel(rect.y + (rect.h - textHeight) * 0.5f);
    DrawTextFit(tx, ty, rect.w - 20.0f, text, scale, s_activeTheme.textSecondary);
}

void UIToolkit::DrawTelemetryBar(std::string_view text, float y, TextScale scale) {
    const UIRect rect(20.0f, y, 1240.0f, 36.0f);
    const ColorRGBA borderGold{255, 180, 40, 190};
    DrawPanel(rect, &s_activeTheme.backgroundBar, &borderGold, 2.0f);

    const float textHeight = 7.0f * static_cast<float>(scale);
    const float ty = SnapToPixel(rect.y + (rect.h - textHeight) * 0.5f);
    DrawTextFit(rect.x + 12.0f, ty, rect.w - 24.0f, text, scale, s_activeTheme.textAccentCyan);
}

float UIToolkit::MeasureText(std::string_view text, TextScale scale) noexcept {
    return static_cast<float>(text.length() * 6u * static_cast<uint32_t>(scale));
}

void UIToolkit::DrawTextFit(float x, float y, float maxWidth, std::string_view text,
                            TextScale scale, const ColorRGBA& color) {
    if (text.empty() || maxWidth <= 0.0f) return;
    const uint32_t pitch = 6u * static_cast<uint32_t>(scale);
    const size_t maxChars = static_cast<size_t>(maxWidth) / pitch;
    if (maxChars == 0) return;

    auto& r2d = Render2DGX2::Instance();
    const float s = static_cast<float>(scale);
    if (text.length() <= maxChars) {
        r2d.DrawText(text, SnapToPixel(x), SnapToPixel(y), s, color);
        return;
    }
    // Truncado limpio a escala entera: nunca se encoge el glifo
    const size_t keep = (maxChars > 2) ? (maxChars - 2) : maxChars;
    r2d.DrawText(text.substr(0, keep), SnapToPixel(x), SnapToPixel(y), s, color);
    if (maxChars > 2) {
        r2d.DrawText("..", SnapToPixel(x + static_cast<float>(keep * pitch)), SnapToPixel(y), s, color);
    }
}

namespace {
/**
 * @brief Recorre las líneas resultantes de partir `text` en los separadores " | " sin exceder
 *        maxChars por línea. Opera sobre string_view (cero alocaciones).
 */
template <typename Fn>
void ForEachWrappedLine(std::string_view text, size_t maxChars, Fn&& fn) {
    constexpr std::string_view SEP = " | ";
    while (!text.empty()) {
        if (text.length() <= maxChars) { fn(text); return; }
        // Buscar el último separador que permita una línea <= maxChars
        size_t cut = std::string_view::npos;
        size_t pos = text.find(SEP);
        while (pos != std::string_view::npos && pos <= maxChars) {
            cut = pos;
            pos = text.find(SEP, pos + 1);
        }
        if (cut == std::string_view::npos || cut == 0) {
            fn(text.substr(0, maxChars)); // segmento indivisible: corte duro
            text.remove_prefix(maxChars);
        } else {
            fn(text.substr(0, cut));
            text.remove_prefix(cut + SEP.length());
        }
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    }
}
} // namespace

float UIToolkit::DrawHudFooter(std::initializer_list<HudLine> lines) {
    constexpr float PANEL_X = 20.0f;
    constexpr float PANEL_W = 1240.0f;
    constexpr float BOTTOM  = 708.0f;   // Zona segura inferior (12 px de margen)
    constexpr float PAD_X   = 14.0f;
    constexpr float PAD_Y   = 8.0f;
    constexpr float LINE_H  = 20.0f;    // Glifo 2x = 14 px + 6 px de interlineado
    constexpr uint32_t PITCH = 12u;     // 6 px * escala 2
    const size_t maxChars = static_cast<size_t>(PANEL_W - 2.0f * PAD_X) / PITCH;

    size_t lineCount = 0;
    for (const HudLine& l : lines) {
        ForEachWrappedLine(l.text, maxChars, [&](std::string_view) { ++lineCount; });
    }
    if (lineCount == 0) return BOTTOM;

    const float panelH = SnapToPixel(2.0f * PAD_Y + static_cast<float>(lineCount) * LINE_H - 6.0f);
    const float panelY = BOTTOM - panelH;
    DrawPanel(UIRect(PANEL_X, panelY, PANEL_W, panelH), &s_activeTheme.backgroundBar, &s_activeTheme.panelBorder, 2.0f);

    auto& r2d = Render2DGX2::Instance();
    float y = panelY + PAD_Y;
    for (const HudLine& l : lines) {
        ForEachWrappedLine(l.text, maxChars, [&](std::string_view seg) {
            r2d.DrawText(seg, PANEL_X + PAD_X, SnapToPixel(y), 2.0f, l.color);
            y += LINE_H;
        });
    }
    return panelY;
}

bool UIToolkit::VerifyUIToolkitIntegrity() {
    // 1. Validar dimensiones de lienzo canónico 720p
    if (CANVAS_WIDTH != 1280.0f || CANVAS_HEIGHT != 720.0f) {
        return false;
    }

    // 2. Validar redondeo estricto de píxel entero (SnapToPixel)
    if (SnapToPixel(10.2f) != 10.0f || SnapToPixel(10.7f) != 11.0f || SnapToPixel(10.5f) != 11.0f) {
        return false;
    }

    // 3. Validar UIRect y cálculo de contención
    UIRect rect(10.3f, 20.7f, 100.2f, 50.8f);
    if (rect.x != 10.0f || rect.y != 21.0f || rect.w != 100.0f || rect.h != 51.0f) {
        return false;
    }
    if (!rect.Contains(50.0f, 40.0f) || rect.Contains(5.0f, 40.0f) || rect.Contains(120.0f, 40.0f)) {
        return false;
    }

    // 4. Validar UILayoutRow (distribución regular sin traslape y con píxeles enteros)
    UILayoutRow row(12.0f, 8.0f, 1256.0f, 48.0f, 9, 8.0f);
    if (row.GetItemCount() != 9) {
        return false;
    }
    for (int i = 0; i < 8; ++i) {
        UIRect c0 = row.GetCell(i);
        UIRect c1 = row.GetCell(i + 1);
        if (c0.x + c0.w > c1.x) {
            return false; // Traslape detectado
        }
        if (c0.w <= 0.0f || c0.h <= 0.0f) {
            return false;
        }
    }

    // 5. Validar cuantización de escalas
    if (static_cast<uint8_t>(TextScale::Small) != 1 ||
        static_cast<uint8_t>(TextScale::Normal) != 2 ||
        static_cast<uint8_t>(TextScale::Large) != 3 ||
        static_cast<uint8_t>(TextScale::Header) != 4) {
        return false;
    }

    return true;
}

} // namespace WiiU::UI
