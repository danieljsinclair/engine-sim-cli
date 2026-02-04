#include "engine_sim_tui/widgets/DashboardLayout.h"
#include "engine_sim_tui/canvas/ColorPalette.h"

namespace engine_sim_tui {
namespace widgets {

DashboardLayout::DashboardLayout()
    : m_engineViewModel(std::make_shared<viewmodels::EngineViewModel>())
{
    InitializeGauges();
}

void DashboardLayout::SetDataProvider(std::shared_ptr<data::IEngineDataProvider> provider) {
    m_dataProvider = provider;
    m_engineViewModel->SetDataProvider(provider);
}

void DashboardLayout::InitializeGauges() {
    // Tachometer
    m_tachometer = std::make_shared<CircularGaugeWidget>();
    m_tachometer->SetTitle("TACHOMETER")
                  .SetUnit("RPM")
                  .SetRange(0, 7000)
                  .SetPrecision(0)
                  .SetSize(26, 14)
                  .SetColor(canvas::ColorPalette::Blue());

    // Speedometer
    m_speedometer = std::make_shared<CircularGaugeWidget>();
    m_speedometer->SetTitle("SPEEDOMETER")
                    .SetUnit("MPH")
                    .SetRange(0, 200)
                    .SetPrecision(0)
                    .SetSize(26, 14)
                    .SetColor(canvas::ColorPalette::Green());

    // Manifold Pressure
    m_manifoldGauge = std::make_shared<CircularGaugeWidget>();
    m_manifoldGauge->SetTitle("MANIFOLD")
                    .SetUnit("inHg")
                    .SetRange(-30, 10)
                    .SetPrecision(1)
                    .SetSize(26, 14)
                    .SetColor(canvas::ColorPalette::Orange());
}

bool DashboardLayout::Update() {
    if (!m_dataProvider) {
        return false;
    }

    // Update simulation
    m_dataProvider->Update(0.016); // Assume ~60 FPS

    // Update view model
    m_engineViewModel->Update();

    // Update gauges
    bool changed = false;

    changed |= m_tachometer->SetValue(m_engineViewModel->GetRPM());
    changed |= m_speedometer->SetValue(m_engineViewModel->GetSpeed());
    changed |= m_manifoldGauge->SetValue(m_engineViewModel->GetManifoldPressure());

    return changed;
}

ftxui::Element DashboardLayout::Render() {
    using namespace ftxui;

    // Get gauge elements
    auto tachElement = m_tachometer->Render();
    auto speedElement = m_speedometer->Render();
    auto manifoldElement = m_manifoldGauge->Render();

    // Build header
    auto header = hbox({
        text(" Engine Sim CLI - TUI Dashboard ") | bold,
    });

    // Build engine info line
    std::string engineName = m_engineViewModel->GetEngineName();
    std::string cylinderInfo = " | Cylinders: " + std::to_string(m_engineViewModel->GetCylinderCount());
    auto infoLine = text(" " + engineName + cylinderInfo) | dim;

    // Build top row: tach and speed
    auto topRow = hbox({
        tachElement,
        filler(),
        speedElement,
    });

    // Build bottom row: manifold pressure
    auto bottomRow = hbox({
        manifoldElement,
        filler(),
    });

    // Compose full layout
    auto content = vbox({
        header | bgcolor(canvas::ColorPalette::Highlight1()) | center,
        separator(),
        infoLine,
        text(""),
        topRow,
        text(""),
        bottomRow,
    });

    return content;
}

} // namespace widgets
} // namespace engine_sim_tui
