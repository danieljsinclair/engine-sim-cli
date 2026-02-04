#ifndef ENGINE_SIM_TUI_CIRCULARGAUGEWIDGET_H
#define ENGINE_SIM_TUI_CIRCULARGAUGEWIDGET_H

#include "engine_sim_tui/viewmodels/GaugeViewModel.h"
#include "engine_sim_tui/canvas/ColorPalette.h"
#include <ftxui/dom/elements.hpp>
#include <string>
#include <memory>

namespace engine_sim_tui {
namespace widgets {

/**
 * @brief Circular gauge widget for FTXUI
 *
 * Renders a circular gauge with:
 * - Title and value display
 * - Circular arc with tick marks
 * - Animated needle with physics
 * - Colored bands (safe/warning/danger zones)
 *
 * For Iteration 1: Simplified ASCII art rendering
 * Future iterations: FTXUI Canvas for smooth circles
 */
class CircularGaugeWidget {
public:
    CircularGaugeWidget();
    ~CircularGaugeWidget() = default;

    // ====================================================================
    // Configuration
    // ====================================================================

    /**
     * @brief Set gauge title
     */
    CircularGaugeWidget& SetTitle(std::string title);

    /**
     * @brief Set gauge units
     */
    CircularGaugeWidget& SetUnit(std::string unit);

    /**
     * @brief Set value range
     */
    CircularGaugeWidget& SetRange(double min, double max);

    /**
     * @brief Set decimal precision for value display
     */
    CircularGaugeWidget& SetPrecision(int precision);

    /**
     * @brief Set gauge dimensions
     */
    CircularGaugeWidget& SetSize(int width, int height);

    /**
     * @brief Set primary color
     */
    CircularGaugeWidget& SetColor(ftxui::Color color);

    /**
     * @brief Set angle range for gauge arc (in radians)
     * @param thetaMin Start angle (0 = right, pi/2 = up, pi = left)
     * @param thetaMax End angle
     */
    CircularGaugeWidget& SetAngleRange(double thetaMin, double thetaMax);

    // ====================================================================
    // Value
    // ====================================================================

    /**
     * @brief Update gauge value
     * @param value New value
     * @return True if value changed significantly
     */
    bool SetValue(double value);

    // ====================================================================
    // Rendering
    // ====================================================================

    /**
     * @brief Render gauge as FTXUI element
     */
    ftxui::Element Render();

private:
    // View model backing this widget
    std::shared_ptr<viewmodels::GaugeViewModel> m_viewModel;

    // Visual configuration
    int m_width = 24;
    int m_height = 12;
    ftxui::Color m_color = ftxui::Color::Cyan;
    double m_thetaMin = 0.0;   // Start of gauge arc (radians)
    double m_thetaMax = 0.0;   // End of gauge arc (radians)

    // Needle physics (for future animation)
    double m_needlePosition = 0.0;  // Normalized 0-1
    double m_needleVelocity = 0.0;

    /**
     * @brief Render gauge using ASCII art (Iteration 1)
     */
    ftxui::Element RenderAscii();

    /**
     * @brief Calculate needle character based on angle
     */
    std::string GetNeedleCharacter(double normalizedValue);
};

} // namespace widgets
} // namespace engine_sim_tui

#endif // ENGINE_SIM_TUI_CIRCULARGAUGEWIDGET_H
