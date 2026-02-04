#include "engine_sim_tui/widgets/CircularGaugeWidget.h"
#include <sstream>
#include <iomanip>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace engine_sim_tui {
namespace widgets {

CircularGaugeWidget::CircularGaugeWidget()
    : m_viewModel(std::make_shared<viewmodels::GaugeViewModel>())
{
    // Default: tachometer style - 270 degree sweep
    SetAngleRange(M_PI * 0.75, -M_PI * 0.75);
}

CircularGaugeWidget& CircularGaugeWidget::SetTitle(std::string title) {
    m_viewModel->SetTitle(std::move(title));
    return *this;
}

CircularGaugeWidget& CircularGaugeWidget::SetUnit(std::string unit) {
    m_viewModel->SetUnit(std::move(unit));
    return *this;
}

CircularGaugeWidget& CircularGaugeWidget::SetRange(double min, double max) {
    m_viewModel->SetRange(min, max);
    return *this;
}

CircularGaugeWidget& CircularGaugeWidget::SetPrecision(int precision) {
    m_viewModel->SetPrecision(precision);
    return *this;
}

CircularGaugeWidget& CircularGaugeWidget::SetSize(int width, int height) {
    m_width = width;
    m_height = height;
    return *this;
}

CircularGaugeWidget& CircularGaugeWidget::SetColor(ftxui::Color color) {
    m_color = color;
    return *this;
}

CircularGaugeWidget& CircularGaugeWidget::SetAngleRange(double thetaMin, double thetaMax) {
    m_thetaMin = thetaMin;
    m_thetaMax = thetaMax;
    return *this;
}

bool CircularGaugeWidget::SetValue(double value) {
    return m_viewModel->SetValue(value);
}

ftxui::Element CircularGaugeWidget::Render() {
    return RenderAscii();
}

ftxui::Element CircularGaugeWidget::RenderAscii() {
    using namespace ftxui;

    // Format value string
    std::string valueStr = m_viewModel->FormatValue();

    // Build gauge visual
    // Create an ASCII representation of the gauge

    std::vector<Element> lines;

    // Title
    if (!m_viewModel->GetTitle().empty()) {
        lines.push_back(text(m_viewModel->GetTitle()) | bold | center);
    }

    // Gap
    lines.push_back(text(""));

    // Value
    lines.push_back(text(valueStr) | bold | color(m_color) | center);

    // Unit
    if (!m_viewModel->GetUnit().empty()) {
        lines.push_back(text(m_viewModel->GetUnit()) | dim | center);
    }

    // Gap
    lines.push_back(text(""));

    // Gauge arc and needle
    double normalized = m_viewModel->GetNormalizedValue();

    // Create needle visual
    int gaugeWidth = m_width - 4; // Account for border and padding
    std::string needleLine(gaugeWidth, ' ');

    // Draw arc marks
    for (int i = 1; i < gaugeWidth - 1; ++i) {
        double t = static_cast<double>(i) / (gaugeWidth - 1);
        needleLine[i] = '_';
    }

    // Calculate needle position
    int needlePos = static_cast<int>(normalized * (gaugeWidth - 2)) + 1;
    needlePos = std::clamp(needlePos, 1, gaugeWidth - 2);

    // Draw needle
    char needleChar = '|';
    if (normalized < 0.3) needleChar = '\\';
    else if (normalized > 0.7) needleChar = '/';

    needleLine[needlePos] = needleChar;

    // Center pivot
    int centerPos = gaugeWidth / 2;
    if (centerPos >= 1 && centerPos < static_cast<int>(needleLine.size())) {
        needleLine[centerPos] = 'o';
    }

    // Add the gauge line
    lines.push_back(text(needleLine) | center);

    // Optional: numeric range below
    std::ostringstream rangeStr;
    rangeStr << std::fixed << std::setprecision(0)
             << m_viewModel->GetMin() << "        " << m_viewModel->GetMax();
    lines.push_back(text(rangeStr.str()) | dim | center);

    // Compose final element
    auto content = vbox(lines);

    // Add border and set size
    return content | border | size(WIDTH, EQUAL, m_width);
}

std::string CircularGaugeWidget::GetNeedleCharacter(double normalizedValue) {
    if (normalizedValue < 0.25) return "\\";
    if (normalizedValue < 0.75) return "|";
    return "/";
}

} // namespace widgets
} // namespace engine_sim_tui
