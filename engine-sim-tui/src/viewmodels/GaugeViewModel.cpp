#include "engine_sim_tui/viewmodels/GaugeViewModel.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace engine_sim_tui {
namespace viewmodels {

GaugeViewModel::GaugeViewModel()
    : m_changeThreshold((m_max - m_min) * 0.01) // 1% of range
{
}

void GaugeViewModel::SetRange(double min, double max) {
    m_min = min;
    m_max = max;
    m_changeThreshold = (max - min) * 0.01; // 1% of range

    // Clamp current value to new range
    SetValue(m_value);
}

bool GaugeViewModel::SetValue(double value) {
    double clampedValue = std::clamp(value, m_min, m_max);

    // Check if change is significant
    double delta = std::abs(clampedValue - m_value);
    if (delta < m_changeThreshold) {
        return false;
    }

    m_value = clampedValue;
    return true;
}

double GaugeViewModel::GetNormalizedValue() const {
    double range = m_max - m_min;
    if (range <= 0.0) {
        return 0.0;
    }
    return (m_value - m_min) / range;
}

std::string GaugeViewModel::FormatValue() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(m_precision) << m_value;
    if (!m_unit.empty()) {
        oss << " " << m_unit;
    }
    return oss.str();
}

} // namespace viewmodels
} // namespace engine_sim_tui
