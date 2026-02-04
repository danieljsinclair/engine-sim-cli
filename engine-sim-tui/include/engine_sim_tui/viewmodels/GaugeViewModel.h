#ifndef ENGINE_SIM_TUI_GAUGEVIEWMODEL_H
#define ENGINE_SIM_TUI_GAUGEVIEWMODEL_H

#include <string>

namespace engine_sim_tui {
namespace viewmodels {

/**
 * @brief ViewModel for a single gauge
 *
 * Contains all data needed to render a gauge widget:
 * - Current value
 * - Range (min/max)
 * - Display units
 * - Label/title
 * - Precision for formatting
 */
class GaugeViewModel {
public:
    GaugeViewModel();
    ~GaugeViewModel() = default;

    // ====================================================================
    // Configuration
    // ====================================================================

    /**
     * @brief Set gauge title/label
     */
    void SetTitle(std::string title) { m_title = std::move(title); }

    /**
     * @brief Set display units
     */
    void SetUnit(std::string unit) { m_unit = std::move(unit); }

    /**
     * @brief Set value range
     */
    void SetRange(double min, double max);

    /**
     * @brief Set decimal precision for value display
     */
    void SetPrecision(int precision) { m_precision = precision; }

    // ====================================================================
    // Value Access
    // ====================================================================

    /**
     * @brief Set current gauge value
     * @param value New value (will be clamped to range)
     * @return True if value changed significantly
     */
    bool SetValue(double value);

    double GetValue() const { return m_value; }
    double GetMin() const { return m_min; }
    double GetMax() const { return m_max; }
    const std::string& GetTitle() const { return m_title; }
    const std::string& GetUnit() const { return m_unit; }
    int GetPrecision() const { return m_precision; }

    /**
     * @brief Get normalized value (0.0 to 1.0) within range
     */
    double GetNormalizedValue() const;

    // ====================================================================
    // Formatting
    // ====================================================================

    /**
     * @brief Format value for display
     * @return Formatted string with unit
     */
    std::string FormatValue() const;

private:
    double m_value = 0.0;
    double m_min = 0.0;
    double m_max = 100.0;
    std::string m_title;
    std::string m_unit;
    int m_precision = 0;

    // Threshold for considering value "changed"
    double m_changeThreshold;
};

} // namespace viewmodels
} // namespace engine_sim_tui

#endif // ENGINE_SIM_TUI_GAUGEVIEWMODEL_H
