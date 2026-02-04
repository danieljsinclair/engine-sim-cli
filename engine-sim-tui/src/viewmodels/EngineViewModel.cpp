#include "engine_sim_tui/viewmodels/EngineViewModel.h"
#include <cmath>

namespace engine_sim_tui {
namespace viewmodels {

EngineViewModel::EngineViewModel()
    : m_cylinderTemperatures(4)
    , m_cylinderPressures(4)
    , m_cylinderFiring(4)
{
}

void EngineViewModel::SetDataProvider(std::shared_ptr<data::IEngineDataProvider> provider) {
    m_provider = std::move(provider);

    if (m_provider) {
        // Cache metadata
        m_redline = m_provider->GetRedline();
        m_cylinderCount = m_provider->GetCylinderCount();
        m_engineName = m_provider->GetEngineName();

        // Resize cylinder data vectors
        m_cylinderTemperatures.resize(m_cylinderCount);
        m_cylinderPressures.resize(m_cylinderCount);
        m_cylinderFiring.resize(m_cylinderCount);
    }
}

void EngineViewModel::SetUpdateCallback(UpdateCallback callback) {
    m_updateCallback = std::move(callback);
}

double EngineViewModel::GetRPM() const {
    return m_rpm;
}

void EngineViewModel::Update() {
    if (!m_provider || !m_provider->IsInitialized()) {
        return;
    }

    m_hasChanged = false;

    // Update cached values and track changes
    double newRpm = m_provider->GetRPM();
    if (std::abs(newRpm - m_rpm) > RPM_CHANGE_THRESHOLD) {
        m_rpm = newRpm;
        m_hasChanged = true;
    }

    double newThrottle = m_provider->GetThrottle();
    if (std::abs(newThrottle - m_throttle) > 0.01) {
        m_throttle = newThrottle;
        m_hasChanged = true;
    }

    double newManifold = m_provider->GetManifoldPressure();
    if (std::abs(newManifold - m_manifoldPressure) > PRESSURE_CHANGE_THRESHOLD) {
        m_manifoldPressure = newManifold;
        m_hasChanged = true;
    }

    double newSpeed = m_provider->GetSpeed();
    if (std::abs(newSpeed - m_speed) > SPEED_CHANGE_THRESHOLD) {
        m_speed = newSpeed;
        m_hasChanged = true;
    }

    // Update other values (always update these for simplicity)
    m_intakeAFR = m_provider->GetIntakeAFR();
    m_exhaustAFR = m_provider->GetExhaustAFR();
    m_intakeCFM = m_provider->GetIntakeCFM();
    m_volumetricEfficiency = m_provider->GetVolumetricEfficiency();
    m_fuelConsumed = m_provider->GetFuelConsumed();

    // Update cylinder data
    m_cylinderTemperatures = m_provider->GetCylinderTemperatures();
    m_cylinderPressures = m_provider->GetCylinderPressures();
    m_cylinderFiring = m_provider->GetCylinderFiring();

    // Notify callback if something changed
    if (m_hasChanged && m_updateCallback) {
        m_updateCallback();
    }
}

} // namespace viewmodels
} // namespace engine_sim_tui
