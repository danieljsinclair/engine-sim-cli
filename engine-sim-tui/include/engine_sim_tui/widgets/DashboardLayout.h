#ifndef ENGINE_SIM_TUI_DASHBOARDLAYOUT_H
#define ENGINE_SIM_TUI_DASHBOARDLAYOUT_H

#include "engine_sim_tui/data/IEngineDataProvider.h"
#include "engine_sim_tui/viewmodels/EngineViewModel.h"
#include "engine_sim_tui/widgets/CircularGaugeWidget.h"
#include <memory>
#include <vector>

namespace engine_sim_tui {
namespace widgets {

/**
 * @brief Main dashboard layout
 *
 * Composes multiple gauges into a dashboard layout.
 * For Iteration 1: Simple side-by-side gauge layout
 * Future iterations: Full dashboard with all panels
 */
class DashboardLayout {
public:
    DashboardLayout();
    ~DashboardLayout() = default;

    // ====================================================================
    // Configuration
    // ====================================================================

    /**
     * @brief Set the data provider
     */
    void SetDataProvider(std::shared_ptr<data::IEngineDataProvider> provider);

    // ====================================================================
    // Update
    // ====================================================================

    /**
     * @brief Update all gauges from data provider
     * @return True if any value changed significantly
     */
    bool Update();

    // ====================================================================
    // Rendering
    // ====================================================================

    /**
     * @brief Render the full dashboard
     */
    ftxui::Element Render();

private:
    // Data
    std::shared_ptr<data::IEngineDataProvider> m_dataProvider;
    std::shared_ptr<viewmodels::EngineViewModel> m_engineViewModel;

    // Gauges
    std::shared_ptr<CircularGaugeWidget> m_tachometer;
    std::shared_ptr<CircularGaugeWidget> m_speedometer;
    std::shared_ptr<CircularGaugeWidget> m_manifoldGauge;

    // Initialization
    void InitializeGauges();
};

} // namespace widgets
} // namespace engine_sim_tui

#endif // ENGINE_SIM_TUI_DASHBOARDLAYOUT_H
