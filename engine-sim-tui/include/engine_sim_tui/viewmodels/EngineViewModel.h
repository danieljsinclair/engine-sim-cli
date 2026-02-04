#ifndef ENGINE_SIM_TUI_ENGINEVIEWMODEL_H
#define ENGINE_SIM_TUI_ENGINEVIEWMODEL_H

#include "engine_sim_tui/data/IEngineDataProvider.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace engine_sim_tui {
namespace viewmodels {

/**
 * @brief ViewModel for engine telemetry data
 *
 * Bridges the data provider and UI widgets. Provides:
 * - Cached engine state for UI rendering
 * - Notification callbacks for data changes
 * - Unit conversions and formatting
 * - Thread-safe data access
 */
class EngineViewModel {
public:
    /**
     * @brief Callback type for data change notifications
     */
    using UpdateCallback = std::function<void()>;

    EngineViewModel();
    ~EngineViewModel() = default;

    // ====================================================================
    // Configuration
    // ====================================================================

    /**
     * @brief Set the data provider
     * @param provider Shared pointer to data provider (can be mock or real)
     */
    void SetDataProvider(std::shared_ptr<data::IEngineDataProvider> provider);

    /**
     * @brief Register callback for data updates
     * @param callback Function to call when data changes
     */
    void SetUpdateCallback(UpdateCallback callback);

    // ====================================================================
    // Data Access
    // ====================================================================

    // Primary metrics
    double GetRPM() const;
    double GetThrottle() const { return m_throttle; }
    double GetManifoldPressure() const { return m_manifoldPressure; }
    double GetSpeed() const { return m_speed; }

    // Air/Fuel metrics
    double GetIntakeAFR() const { return m_intakeAFR; }
    double GetExhaustAFR() const { return m_exhaustAFR; }
    double GetIntakeCFM() const { return m_intakeCFM; }
    double GetVolumetricEfficiency() const { return m_volumetricEfficiency; }

    // Fuel
    double GetFuelConsumed() const { return m_fuelConsumed; }

    // Cylinder data
    const std::vector<double>& GetCylinderTemperatures() const { return m_cylinderTemperatures; }
    const std::vector<double>& GetCylinderPressures() const { return m_cylinderPressures; }
    const std::vector<bool>& GetCylinderFiring() const { return m_cylinderFiring; }

    // Metadata
    double GetRedline() const { return m_redline; }
    int GetCylinderCount() const { return m_cylinderCount; }
    std::string GetEngineName() const { return m_engineName; }

    // ====================================================================
    // Update
    // ====================================================================

    /**
     * @brief Update cached data from provider
     *
     * Called from UI thread to refresh state. If data has changed
     * significantly, triggers the update callback.
     */
    void Update();

    /**
     * @brief Check if data has changed since last update
     * @return True if any significant value changed
     */
    bool HasChanged() const { return m_hasChanged; }

private:
    // Data provider
    std::shared_ptr<data::IEngineDataProvider> m_provider;

    // Cached state
    double m_rpm = 0.0;
    double m_throttle = 0.0;
    double m_manifoldPressure = 0.0;
    double m_speed = 0.0;
    double m_intakeAFR = 14.7;
    double m_exhaustAFR = 14.7;
    double m_intakeCFM = 0.0;
    double m_volumetricEfficiency = 0.0;
    double m_fuelConsumed = 0.0;

    std::vector<double> m_cylinderTemperatures;
    std::vector<double> m_cylinderPressures;
    std::vector<bool> m_cylinderFiring;

    // Metadata
    double m_redline = 7000.0;
    int m_cylinderCount = 4;
    std::string m_engineName = "Unknown Engine";

    // Change tracking
    bool m_hasChanged = false;

    // Update notification
    UpdateCallback m_updateCallback;

    // Threshold for considering a value "changed" (for UI updates)
    static constexpr double RPM_CHANGE_THRESHOLD = 10.0;
    static constexpr double PRESSURE_CHANGE_THRESHOLD = 0.5;
    static constexpr double SPEED_CHANGE_THRESHOLD = 0.5;
};

} // namespace viewmodels
} // namespace engine_sim_tui

#endif // ENGINE_SIM_TUI_ENGINEVIEWMODEL_H
