#include "engine_sim_tui/data/MockEngineDataProvider.h"
#include <algorithm>
#include <cmath>
#include <random>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace engine_sim_tui {
namespace data {

// ========================================================================
// Construction
// ========================================================================

MockEngineDataProvider::MockEngineDataProvider()
    : m_cylinderTemperatures(DEFAULT_CYLINDERS)
    , m_cylinderPressures(DEFAULT_CYLINDERS)
    , m_cylinderFiring(DEFAULT_CYLINDERS)
{
    // Default: gentle sine wave for demo
    m_rpmPattern = SineWavePattern(800, 3000, 0.5);

    // Initialize cylinder temps to reasonable values
    for (auto& temp : m_cylinderTemperatures) {
        temp = 180.0;
    }
}

// ========================================================================
// Pattern Configuration
// ========================================================================

void MockEngineDataProvider::SetRpmPattern(RpmPattern pattern) {
    m_rpmPattern = std::move(pattern);
}

MockEngineDataProvider::RpmPattern MockEngineDataProvider::IdlePattern() {
    return [](double t) {
        return IDLE_RPM + std::sin(t * 2.0) * 50 + std::sin(t * 7.3) * 20;
    };
}

MockEngineDataProvider::RpmPattern MockEngineDataProvider::RevvingPattern(double maxRpm) {
    return [maxRpm](double t) {
        double phase = std::fmod(t, 4.0);
        if (phase < 1.0) return IDLE_RPM;                          // idle
        else if (phase < 2.5) return IDLE_RPM + (phase - 1.0) / 1.5 * (maxRpm - IDLE_RPM);  // rev up
        else if (phase < 3.0) return maxRpm;                        // hold
        else return maxRpm - (phase - 3.0) / 1.0 * (maxRpm - IDLE_RPM);  // rev down
    };
}

MockEngineDataProvider::RpmPattern MockEngineDataProvider::SineWavePattern(
    double minRpm, double maxRpm, double frequency)
{
    return [minRpm, maxRpm, frequency](double t) {
        double mid = (minRpm + maxRpm) / 2.0;
        double amplitude = (maxRpm - minRpm) / 2.0;
        return mid + std::sin(t * frequency * 2.0 * M_PI) * amplitude;
    };
}

// ========================================================================
// IEngineDataProvider Implementation
// ========================================================================

double MockEngineDataProvider::GetRPM() const {
    return m_rpm.load();
}

double MockEngineDataProvider::GetThrottle() const {
    return m_throttle.load();
}

double MockEngineDataProvider::GetManifoldPressure() const {
    return m_manifoldPressure.load();
}

double MockEngineDataProvider::GetIntakeAFR() const {
    return 14.7; // Stoichiometric
}

double MockEngineDataProvider::GetExhaustAFR() const {
    return 14.7;
}

double MockEngineDataProvider::GetIntakeCFM() const {
    // Simplified calculation based on RPM and throttle
    double rpm = m_rpm.load();
    double throttle = m_throttle.load();
    return (rpm / 1000.0) * throttle * 50.0;
}

double MockEngineDataProvider::GetVolumetricEfficiency() const {
    // VE varies with RPM, peaks around torque peak
    double rpm = m_rpm.load();
    double rpmRatio = rpm / 4000.0;
    return 80.0 + 20.0 * std::exp(-std::pow(rpmRatio - 0.8, 2) * 5.0);
}

double MockEngineDataProvider::GetSpeed() const {
    return m_speed.load();
}

double MockEngineDataProvider::GetFuelConsumed() const {
    return m_fuelConsumed.load();
}

std::vector<double> MockEngineDataProvider::GetCylinderTemperatures() const {
    return m_cylinderTemperatures;
}

std::vector<double> MockEngineDataProvider::GetCylinderPressures() const {
    return m_cylinderPressures;
}

std::vector<bool> MockEngineDataProvider::GetCylinderFiring() const {
    return m_cylinderFiring;
}

double MockEngineDataProvider::GetRedline() const {
    return DEFAULT_REDLINE;
}

int MockEngineDataProvider::GetCylinderCount() const {
    return DEFAULT_CYLINDERS;
}

std::string MockEngineDataProvider::GetEngineName() const {
    return "Subaru EJ25 2.5L H4";
}

// ========================================================================
// Commands
// ========================================================================

void MockEngineDataProvider::SetThrottle(double value) {
    m_throttle.store(std::clamp(value, 0.0, 1.0));
}

void MockEngineDataProvider::SetIgnition(bool enabled) {
    m_ignitionEnabled.store(enabled);
}

void MockEngineDataProvider::SetStarterMotor(bool enabled) {
    m_starterEngaged.store(enabled);
}

void MockEngineDataProvider::SetClutchPressure(double pressure) {
    // Store for potential future use
    (void)pressure;
}

// ========================================================================
// Simulation Control
// ========================================================================

void MockEngineDataProvider::Update(double dt) {
    m_time += dt;

    // Update RPM based on pattern
    double targetRpm = m_rpmPattern(m_time);

    // Starter motor effect
    if (m_starterEngaged.load() && !m_ignitionEnabled.load()) {
        targetRpm = 200.0;
    }

    // Clamp to reasonable range
    targetRpm = std::clamp(targetRpm, 0.0, DEFAULT_REDLINE * 1.1);

    // Smooth RPM changes (simulates engine inertia)
    double currentRpm = m_rpm.load();
    double rpmRate = 2000.0; // RPM per second acceleration rate
    double newRpm = currentRpm + std::clamp(targetRpm - currentRpm, -rpmRate * dt, rpmRate * dt);
    m_rpm.store(std::max(0.0, newRpm));

    // Update vehicle speed (lags behind RPM)
    double currentSpeed = m_speed.load();
    double targetSpeed = (m_rpm.load() / 3000.0) * 60.0; // Rough approximation
    double speedRate = 10.0; // MPH per second
    double newSpeed = currentSpeed + std::clamp(targetSpeed - currentSpeed, -speedRate * dt, speedRate * dt);
    m_speed.store(std::max(0.0, newSpeed));

    // Update manifold pressure (correlates with throttle and RPM)
    double throttle = m_throttle.load();
    double rpm = m_rpm.load();
    double targetManifold = -30.0 + throttle * 35.0;

    // At high RPM with low throttle, manifold goes into vacuum
    if (rpm > 2000 && throttle < 0.3) {
        targetManifold = -20.0;
    }

    double currentManifold = m_manifoldPressure.load();
    double manifoldRate = 50.0; // inHg per second
    double newManifold = currentManifold + std::clamp(
        targetManifold - currentManifold,
        -manifoldRate * dt,
        manifoldRate * dt
    );
    m_manifoldPressure.store(newManifold);

    // Update cylinder temperatures (vary with RPM and slight oscillation)
    double baseTemp = 180.0 + (rpm / DEFAULT_REDLINE) * 100.0;
    static double tempPhase = 0.0;
    tempPhase += dt * 10.0;
    for (size_t i = 0; i < m_cylinderTemperatures.size(); ++i) {
        m_cylinderTemperatures[i] = baseTemp + std::sin(tempPhase + i * 0.785) * 5.0;
    }

    // Update cylinder pressures (follow manifold pressure)
    double basePressure = m_manifoldPressure.load() + 100.0;
    for (size_t i = 0; i < m_cylinderPressures.size(); ++i) {
        m_cylinderPressures[i] = basePressure;
    }

    // Update firing state (4-cylinder firing order: 1-3-4-2)
    if (rpm > 100) {
        double crankAngle = std::fmod(m_time * (rpm / 60.0) * 360.0, 720.0);
        m_cylinderFiring[0] = (crankAngle >= 0 && crankAngle < 180);
        m_cylinderFiring[2] = (crankAngle >= 180 && crankAngle < 360);
        m_cylinderFiring[3] = (crankAngle >= 360 && crankAngle < 540);
        m_cylinderFiring[1] = (crankAngle >= 540 && crankAngle < 720);
    } else {
        std::fill(m_cylinderFiring.begin(), m_cylinderFiring.end(), false);
    }

    // Update fuel consumption (very rough approximation)
    double fuelRate = throttle * (rpm / 1000.0) * 0.0001; // gallons per second at this rate
    double currentFuel = m_fuelConsumed.load();
    m_fuelConsumed.store(currentFuel + fuelRate * dt);
}

bool MockEngineDataProvider::IsInitialized() const {
    return m_initialized.load();
}

} // namespace data
} // namespace engine_sim_tui
