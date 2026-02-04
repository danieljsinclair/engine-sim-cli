#ifndef ENGINE_SIM_TUI_IENGINEDATAPROVIDER_H
#define ENGINE_SIM_TUI_IENGINEDATAPROVIDER_H

#include <string>
#include <vector>
#include <memory>

namespace engine_sim_tui {
namespace data {

/**
 * @brief Abstract interface for engine data source
 *
 * This interface abstracts the data source for the TUI, enabling:
 * - Testing with mock data
 * - Dependency injection
 * - Substitution of different data sources
 */
class IEngineDataProvider {
public:
    virtual ~IEngineDataProvider() = default;

    // ====================================================================
    // State Queries (Thread-safe reads)
    // ====================================================================

    /**
     * @brief Get current engine RPM
     * @return RPM value (0-10000+)
     */
    virtual double GetRPM() const = 0;

    /**
     * @brief Get current throttle position
     * @return Throttle position (0.0-1.0)
     */
    virtual double GetThrottle() const = 0;

    /**
     * @brief Get manifold pressure
     * @return Pressure in inHg (negative = vacuum)
     */
    virtual double GetManifoldPressure() const = 0;

    /**
     * @brief Get intake air-fuel ratio
     * @return AFR value (typically ~14.7 for stoichiometric)
     */
    virtual double GetIntakeAFR() const = 0;

    /**
     * @brief Get exhaust air-fuel ratio
     * @return AFR value
     */
    virtual double GetExhaustAFR() const = 0;

    /**
     * @brief Get intake CFM (cubic feet per minute)
     * @return Air flow rate
     */
    virtual double GetIntakeCFM() const = 0;

    /**
     * @brief Get volumetric efficiency
     * @return VE as percentage (0-100+)
     */
    virtual double GetVolumetricEfficiency() const = 0;

    /**
     * @brief Get vehicle speed
     * @return Speed in MPH
     */
    virtual double GetSpeed() const = 0;

    /**
     * @brief Get fuel consumed
     * @return Fuel volume in gallons
     */
    virtual double GetFuelConsumed() const = 0;

    /**
     * @brief Get cylinder temperatures
     * @return Vector of temperatures for each cylinder
     */
    virtual std::vector<double> GetCylinderTemperatures() const = 0;

    /**
     * @brief Get cylinder pressures
     * @return Vector of pressures for each cylinder
     */
    virtual std::vector<double> GetCylinderPressures() const = 0;

    /**
     * @brief Get cylinder firing state
     * @return Vector of booleans indicating if each cylinder is firing
     */
    virtual std::vector<bool> GetCylinderFiring() const = 0;

    // ====================================================================
    // Metadata
    // ====================================================================

    /**
     * @brief Get engine redline RPM
     * @return Redline value
     */
    virtual double GetRedline() const = 0;

    /**
     * @brief Get number of cylinders
     * @return Cylinder count
     */
    virtual int GetCylinderCount() const = 0;

    /**
     * @brief Get engine name/description
     * @return Engine name string
     */
    virtual std::string GetEngineName() const = 0;

    // ====================================================================
    // Commands (Thread-safe writes)
    // ====================================================================

    /**
     * @brief Set throttle position
     * @param value Throttle position (0.0-1.0)
     */
    virtual void SetThrottle(double value) = 0;

    /**
     * @brief Set ignition state
     * @param enabled True to enable ignition
     */
    virtual void SetIgnition(bool enabled) = 0;

    /**
     * @brief Set starter motor state
     * @param enabled True to engage starter
     */
    virtual void SetStarterMotor(bool enabled) = 0;

    /**
     * @brief Set clutch pressure
     * @param pressure Clutch pressure (0.0-1.0)
     */
    virtual void SetClutchPressure(double pressure) = 0;

    // ====================================================================
    // Simulation Control
    // ====================================================================

    /**
     * @brief Update simulation state
     * @param dt Delta time in seconds
     */
    virtual void Update(double dt) = 0;

    /**
     * @brief Check if simulation is initialized
     * @return True if ready to provide data
     */
    virtual bool IsInitialized() const = 0;
};

} // namespace data
} // namespace engine_sim_tui

#endif // ENGINE_SIM_TUI_IENGINEDATAPROVIDER_H
