#ifndef ENGINE_SIM_TUI_COLORPALETTE_H
#define ENGINE_SIM_TUI_COLORPALETTE_H

#include <ftxui/screen/color.hpp>

namespace engine_sim_tui {
namespace canvas {

/**
 * @brief Color palette matching the GUI color scheme
 *
 * Provides FTXUI Color objects that match the visual style
 * of the original engine simulator GUI.
 */
class ColorPalette {
public:
    // Primary colors from GUI
    static const ftxui::Color Foreground()  { return ftxui::Color::RGB(255, 255, 255); }
    static const ftxui::Color Background()  { return ftxui::Color::RGB(18, 18, 18); }
    static const ftxui::Color Shadow()       { return ftxui::Color::RGB(50, 50, 50); }

    // Highlight colors
    static const ftxui::Color Highlight1()  { return ftxui::Color::RGB(100, 150, 255); }
    static const ftxui::Color Highlight2()  { return ftxui::Color::RGB(150, 200, 255); }

    // Status colors
    static const ftxui::Color Pink()        { return ftxui::Color::RGB(255, 105, 180); }
    static const ftxui::Color Orange()      { return ftxui::Color::RGB(255, 140, 0); }
    static const ftxui::Color Yellow()      { return ftxui::Color::RGB(255, 220, 0); }
    static const ftxui::Color Red()         { return ftxui::Color::RGB(255, 50, 50); }
    static const ftxui::Color Green()       { return ftxui::Color::RGB(50, 255, 50); }
    static const ftxui::Color Blue()        { return ftxui::Color::RGB(50, 150, 255); }

    // Gauge-specific colors
    static const ftxui::Color GaugeText()         { return ftxui::Color::RGB(220, 220, 220); }
    static const ftxui::Color GaugeTickMajor()    { return ftxui::Color::RGB(180, 180, 180); }
    static const ftxui::Color GaugeTickMinor()    { return ftxui::Color::RGB(100, 100, 100); }
    static const ftxui::Color GaugeNeedle()       { return ftxui::Color::RGB(255, 50, 50); }
    static const ftxui::Color GaugeBandSafe()     { return ftxui::Color::RGB(100, 200, 100); }
    static const ftxui::Color GaugeBandWarning()  { return ftxui::Color::RGB(255, 180, 0); }
    static const ftxui::Color GaugeBandDanger()   { return ftxui::Color::RGB(255, 50, 50); }

    // Oscilloscope colors
    static const ftxui::Color ScopeTrace()    { return ftxui::Color::RGB(0, 255, 127); }
    static const ftxui::Color ScopeGrid()     { return ftxui::Color::RGB(50, 50, 50); }
    static const ftxui::Color ScopeTrigger()  { return ftxui::Color::RGB(255, 255, 0); }

    // Firing indicator colors
    static const ftxui::Color CylinderFiring()   { return ftxui::Color::RGB(255, 255, 100); }
    static const ftxui::Color CylinderExhaust()  { return ftxui::Color::RGB(255, 100, 50); }
    static const ftxui::Color CylinderIntake()   { return ftxui::Color::RGB(100, 200, 255); }
};

} // namespace canvas
} // namespace engine_sim_tui

#endif // ENGINE_SIM_TUI_COLORPALETTE_H
