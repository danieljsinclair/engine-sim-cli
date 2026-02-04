#ifndef ENGINE_SIM_TUI_MOCKENGINEDATAPROVIDER_H
#define ENGINE_SIM_TUI_MOCKENGINEDATAPROVIDER_H

#include "IEngineDataProvider.h"
#include <functional>
#include <memory>
#include <atomic>

namespace engine_sim_tui {
namespace data {

/**
 * @brief Mock data provider for testing and demonstration
 *
 * Generates realistic engine data using configurable patterns.
 * Used for TUI development without requiring the full simulator.
 */
class MockEngineDataProvider : public IEngineDataProvider {
public:
    /**
     * @brief Pattern function for RPM generation
     * @param time Time in seconds
     * @return RPM value at given time
     */
    using RpmPattern = std::function<double(double time)>;

    // ====================================================================
    // Construction
    // ====================================================================

    MockEngineDataProvider();
    ~MockEngineDataProvider() override = default;

    // ====================================================================
    // Pattern Configuration
    // ====================================================================

    /**
     * @brief Set the RPM generation pattern
     * @param pattern Function that returns RPM for a given time
     */
    void SetRpmPattern(RpmPattern pattern);

    /**
     * @brief Get built-in idle pattern
     * @return Pattern generating ~800 RPM with slight variation
     */
    static RpmPattern IdlePattern();

    /**
     * @brief Get built-in revving pattern
     * @param maxRpm Maximum RPM during rev
     * @return Pattern that revs from idle to maxRpm and back
     */
    static RpmPattern RevvingPattern(double maxRpm);

    /**
     * @brief Get sine wave pattern
     * @param minRpm Minimum RPM
     * @param maxRpm Maximum RPM
     * @param frequency Oscillations per second
     * @return Pattern generating sine wave between min and max
     */
    static RpmPattern SineWavePattern(double minRpm, double maxRpm, double frequency);

    // ====================================================================
    // IEngineDataProvider Implementation
    // ====================================================================

    // State Queries
    double GetRPM() const override;
    double GetThrottle() const override;
    double GetManifoldPressure() const override;
    double GetIntakeAFR() const override;
    double GetExhaustAFR() const override;
    double GetIntakeCFM() const override;
    double GetVolumetricEfficiency() const override;
    double GetSpeed() const override;
    double GetFuelConsumed() const override;
    std::vector<double> GetCylinderTemperatures() const override;
    std::vector<double> GetCylinderPressures() const override;
    std::vector<bool> GetCylinderFiring() const override;

    // Metadata
    double GetRedline() const override;
    int GetCylinderCount() const override;
    std::string GetEngineName() const override;

    // Commands
    void SetThrottle(double value) override;
    void SetIgnition(bool enabled) override;
    void SetStarterMotor(bool enabled) override;
    void SetClutchPressure(double pressure) override;

    // Simulation Control
    void Update(double dt) override;
    bool IsInitialized() const override;

private:
    // Simulation state
    double m_time = 0.0;
    std::atomic<double> m_rpm{0.0};
    std::atomic<double> m_speed{0.0};
    std::atomic<double> m_manifoldPressure{0.0};
    std::atomic<double> m_throttle{0.0};
    std::atomic<double> m_fuelConsumed{0.0};

    std::atomic<bool> m_ignitionEnabled{false};
    std::atomic<bool> m_starterEngaged{false};
    std::atomic<bool> m_initialized{true};

    // Pattern for RPM generation
    RpmPattern m_rpmPattern;

    // Cached cylinder data (updated on each Update)
    mutable std::vector<double> m_cylinderTemperatures;
    mutable std::vector<double> m_cylinderPressures;
    mutable std::vector<bool> m_cylinderFiring;

    // Configuration
    static constexpr double DEFAULT_REDLINE = 7000.0;
    static constexpr int DEFAULT_CYLINDERS = 4;
    static constexpr double IDLE_RPM = 800.0;
};

} // namespace data
} // namespace engine_sim_tui

#endif // ENGINE_SIM_TUI_MOCKENGINEDATAPROVIDER_H
