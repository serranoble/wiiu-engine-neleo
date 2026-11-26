#pragma once

#include "Graphics/Color.hpp"
#include "Graphics/Render2DGX2.hpp"
#include <string_view>
#include <initializer_list>
#include <cmath>
#include <cstdint>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/context.h>
#include <gx2/surface.h>
#endif

namespace Neleo::UI {

/**
 * @brief Dimensiones del Lienzo Virtual Canónico (Virtual Canvas)
 * Siguiendo los principios de Jason Gregory (Game Engine Architecture 3rd Ed., Cap. 14.5:
 * "The Gameplay Foundation System - User Interface & HUD Systems").
 */
inline constexpr float CANVAS_WIDTH  = 1280.0f;
inline constexpr float CANVAS_HEIGHT = 720.0f;

/**
 * @brief Escalas enteras de tipografía bitmap 5x7.
 * En fuentes bitmap de matriz discreta, las escalas fraccionarias (ej. 1.55x, 1.65x)
 * causan un muestreo fraccionario que rompe los bordes de los glifos produciendo
 * desenfoque e irregularidades en el rasterizador de AMD Latte.
 * La cuantización entera garantiza que cada punto del glifo sea exactamente un bloque
 * de NxN píxeles físicos en el scanbuffer.
 */
enum class TextScale : uint8_t {
    Small  = 1, // 1x: 5x7 píxeles por glifo, paso horizontal 6px (subtítulos, descripciones compactas, telemetría densa)
    Normal = 2, // 2x: 10x14 píxeles por glifo, paso horizontal 12px (botones principales, texto estándar de HUD)
    Large  = 3, // 3x: 15x21 píxeles por glifo, paso horizontal 18px (títulos de diálogo, modales de pausa)
    Header = 4  // 4x: 20x28 píxeles por glifo, paso horizontal 24px (títulos de menú y encabezados mayores)
};

/**
 * @brief Utilidad de alineación de cuadrícula: Redondea coordenadas al entero más cercano.
 * (Texel-to-Pixel Half-Texel Grid Alignment).
 */
[[nodiscard]] inline constexpr float SnapToPixel(float v) noexcept {
    return std::floor(v + 0.5f);
}

/**
 * @brief Rectángulo de interfaz de usuario con alineación física de píxeles.
 */
struct UIRect {
    float x{0.0f};
    float y{0.0f};
    float w{0.0f};
    float h{0.0f};

    constexpr UIRect() noexcept = default;
    constexpr UIRect(float inX, float inY, float inW, float inH) noexcept
        : x(std::floor(inX + 0.5f)),
          y(std::floor(inY + 0.5f)),
          w(std::floor(inW + 0.5f)),
          h(std::floor(inH + 0.5f)) {}

    [[nodiscard]] constexpr bool Contains(float px, float py) const noexcept {
        return px >= x && px <= (x + w) && py >= y && py <= (y + h);
    }

    [[nodiscard]] constexpr UIRect Inset(float dx, float dy) const noexcept {
        return UIRect(x + dx, y + dy, (w > 2.0f * dx) ? (w - 2.0f * dx) : 0.0f,
                                      (h > 2.0f * dy) ? (h - 2.0f * dy) : 0.0f);
    }

    [[nodiscard]] constexpr UIRect Offset(float dx, float dy) const noexcept {
        return UIRect(x + dx, y + dy, w, h);
    }
};

/**
 * @brief Paleta de colores e identidad visual estandarizada para la UI del motor.
 */
struct UITheme {
    ColorRGBA backgroundBar{12, 16, 26, 220};
    ColorRGBA panelBg{14, 20, 32, 225};
    ColorRGBA panelBorder{0, 210, 255, 140};

    ColorRGBA buttonNormal{20, 32, 52, 235};
    ColorRGBA buttonHover{45, 75, 120, 245};
    ColorRGBA buttonBorderNormal{0, 200, 240, 220};
    ColorRGBA buttonBorderHover{255, 220, 0, 255};

    ColorRGBA buttonDangerNormal{160, 30, 40, 235};
    ColorRGBA buttonDangerHover{210, 50, 60, 250};
    ColorRGBA buttonDangerBorder{255, 200, 40, 255};

    ColorRGBA buttonActiveNormal{25, 80, 50, 235};
    ColorRGBA buttonActiveHover{40, 120, 75, 245};
    ColorRGBA buttonActiveBorder{120, 255, 160, 255};

    ColorRGBA textPrimary{255, 255, 255, 255};
    ColorRGBA textSecondary{180, 215, 245, 240};
    ColorRGBA textAccentYellow{255, 215, 0, 255};
    ColorRGBA textAccentCyan{0, 235, 255, 255};
    ColorRGBA textAccentGreen{120, 255, 160, 255};
    ColorRGBA textDanger{255, 120, 120, 255};
    ColorRGBA textMuted{140, 155, 175, 220};

    float borderWidth{2.0f};

    static const UITheme& Default() noexcept;
};

/**
 * @brief Administrador de disposición en fila (Flex Layout Row).
 * Facilita el cálculo automático de posiciones, anchos y márgenes para barras de herramientas
 * garantizando que los botones mantengan dimensiones enteras alineadas a píxeles.
 */
class UILayoutRow {
public:
    UILayoutRow(float startX, float startY, float totalWidth, float height, int numItems, float gap = 8.0f) noexcept;

    [[nodiscard]] UIRect GetCell(int index) const noexcept;
    [[nodiscard]] int GetItemCount() const noexcept { return m_numItems; }

private:
    float m_startX{0.0f};
    float m_startY{0.0f};
    float m_itemWidth{0.0f};
    float m_height{0.0f};
    float m_gap{8.0f};
    int m_numItems{0};
};

/**
 * @brief Guarda RAII para aislamiento de estado GPU en el pase de UI 2D.
 * (Jason Gregory, Cap. 14.5.3: "Render State Hygiene and Pipeline Isolation").
 * 
 * Configura deterministamente:
 * - Viewport y Scissor a 1280x720 exactos.
 * - Enlace a ContextState y ColorBuffer (si se suministran).
 * - Desactivación de Depth Test y Depth Write.
 * - Desactivación de Backface Culling.
 * - Modo de mezcla Alpha Blend (SrcAlpha, InvSrcAlpha).
 * - Cierre y emisión de lote de dibujo al salir de ámbito.
 */
class ScopedUIPass {
public:
#if defined(__WIIU__) || defined(__wiiu__)
    explicit ScopedUIPass(GX2ContextState* contextState = nullptr, GX2ColorBuffer* colorBuffer = nullptr);
#else
    ScopedUIPass();
#endif
    ~ScopedUIPass();

    ScopedUIPass(const ScopedUIPass&) = delete;
    ScopedUIPass& operator=(const ScopedUIPass&) = delete;
    ScopedUIPass(ScopedUIPass&&) = delete;
    ScopedUIPass& operator=(ScopedUIPass&&) = delete;
};

/**
 * @brief Toolkit de UI estandarizado y de alto rendimiento para Nintendo Wii U (GX2).
 * 
 * Centraliza la creación de componentes de interfaz garantizando:
 * 1. Nitidez absoluta mediante alineación de píxeles enteros (Zero Sub-Pixel Fuzziness).
 * 2. Cero alocaciones dinámicas en caliente (0 bytes malloc/new a 60 FPS).
 * 3. Aislamiento completo de estados de silicio respecto a los pases 3D precedentes.
 * 4. Tipografía bitmap homogénea con cuantización entera de escala.
 */
class UIToolkit {
public:
#if defined(__WIIU__) || defined(__wiiu__)
    static void BeginPass(GX2ContextState* contextState = nullptr, GX2ColorBuffer* colorBuffer = nullptr);
#else
    static void BeginPass();
#endif
    static void EndPass();

    static void SetTheme(const UITheme& theme) noexcept;
    static const UITheme& GetTheme() noexcept;

    // =========================================================================
    // WIDGETS ESTANDARIZADOS DE INTERFAZ
    // =========================================================================

    /**
     * @brief Dibuja un panel contenedor o barra de fondo estilizada con borde.
     */
    static void DrawPanel(const UIRect& rect,
                          const ColorRGBA* customBg = nullptr,
                          const ColorRGBA* customBorder = nullptr,
                          float borderWidth = 2.0f);

    /**
     * @brief Dibuja un botón interactivo estilizado con texto perfectamente centrado.
     */
    static void DrawButton(const UIRect& rect,
                           std::string_view label,
                           bool isHovered = false,
                           TextScale scale = TextScale::Normal,
                           const ColorRGBA* customBg = nullptr,
                           const ColorRGBA* customBorder = nullptr,
                           const ColorRGBA* customText = nullptr);

    /**
     * @brief Dibuja un botón de alternancia (Toggle) con indicador de estado [SI] / [NO].
     */
    static void DrawToggleButton(const UIRect& rect,
                                 std::string_view label,
                                 bool isActive,
                                 bool isHovered = false,
                                 TextScale scale = TextScale::Normal);

    /**
     * @brief Dibuja un botón con etiqueta y valor diferenciados (ej. "FILTRO:" y "ANISO 16X").
     */
    static void DrawValueButton(const UIRect& rect,
                                std::string_view label,
                                std::string_view value,
                                bool isHovered = false,
                                TextScale scale = TextScale::Normal,
                                const ColorRGBA* customValueColor = nullptr);

    /**
     * @brief Dibuja una etiqueta de texto pixel-perfect con escala entera y color.
     */
    static void DrawLabel(float x, float y,
                          std::string_view text,
                          TextScale scale = TextScale::Normal,
                          const ColorRGBA& color = ColorRGBA::White());

    /**
     * @brief Dibuja una barra de progreso o telemetría con porcentaje y etiqueta opcional.
     */
    static void DrawProgressBar(const UIRect& rect,
                                float fraction,
                                std::string_view label = "",
                                const ColorRGBA& barColor = ColorRGBA::Cyan());

    /**
     * @brief Dibuja una línea separadora horizontal de 1 o 2 píxeles de grosor.
     */
    static void DrawSeparator(float x, float y, float w,
                              const ColorRGBA& color = ColorRGBA{60, 90, 130, 200});

    /**
     * @brief Dibuja la barra estándar inferior de guía de controles del GamePad.
     */
    static void DrawGuideBar(std::string_view text,
                             float y = 636.0f,
                             TextScale scale = TextScale::Normal);

    /**
     * @brief Dibuja la barra estándar inferior de telemetría del motor (FPS, resolución, tiempos).
     */
    static void DrawTelemetryBar(std::string_view text,
                                 float y = 672.0f,
                                 TextScale scale = TextScale::Normal);

    // =========================================================================
    // TIPOGRAFÍA SEGURA: MEDICIÓN, AJUSTE Y PIE DE HUD UNIFICADO
    // =========================================================================

    /**
     * @brief Ancho en píxeles de un texto bitmap 5x7 a escala entera (paso 6 px * escala).
     */
    [[nodiscard]] static float MeasureText(std::string_view text, TextScale scale = TextScale::Normal) noexcept;

    /**
     * @brief Dibuja texto a escala ENTERA sin desbordar maxWidth: si no cabe, se trunca con ".."
     *        (nunca se reduce la escala a un valor fraccionario).
     */
    static void DrawTextFit(float x, float y, float maxWidth,
                            std::string_view text,
                            TextScale scale = TextScale::Normal,
                            const ColorRGBA& color = ColorRGBA::White());

    /**
     * @brief Línea de pie de HUD (telemetría o guía de controles).
     */
    struct HudLine {
        std::string_view text;
        ColorRGBA color;
    };

    /**
     * @brief Pie de HUD estándar del motor anclado al borde inferior (zona segura Y <= 708).
     *        Cada línea se parte automáticamente en los separadores " | " para no salir nunca de la
     *        pantalla, siempre a escala 2x nítida. El panel crece hacia arriba según las líneas.
     * @return Coordenada Y superior del panel (para colocar overlays por encima).
     */
    static float DrawHudFooter(std::initializer_list<HudLine> lines);

    /**
     * @brief Suite de autodiagnósticos en tiempo de arranque para verificar alineación de píxeles y temas.
     */
    [[nodiscard]] static bool VerifyUIToolkitIntegrity();

private:
    static inline UITheme s_activeTheme{};
};

} // namespace Neleo::UI
