// RPMController.h - RPM control class
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#ifndef RPM_CONTROLLER_H
#define RPM_CONTROLLER_H

// ============================================================================
// RPM Controller (Simple, Responsive)
// ============================================================================

class RPMController {
private:
    // Simple P-ONLY controller for responsive throttle without over-constraint
    static constexpr double KP = 0.3;    // Moderate P-gain for responsive but stable control
    static constexpr double KI = 0.0;    // No integral term to prevent overshoot
    static constexpr double KD = 0.0;    // No derivative term to prevent overshoot
    static constexpr double MIN_THROTTLE = 0.05;  // Minimum 5% throttle to prevent stalling
    static constexpr double MAX_THROTTLE = 1.0;  // Standard maximum throttle
    static constexpr double MIN_RPM_FOR_CONTROL = 300.0;  // Minimum RPM to enable control

    double targetRPM;
    double kp;
    double ki;
    double kd;
    double integral;
    double lastError;
    bool firstUpdate;

public:
    RPMController();

    void setTargetRPM(double rpm);

    // Update throttle based on current RPM, returns calculated throttle
    double update(double currentRPM, double dt);

    // Get debug information for diagnostics
    void getDebugInfo(double& pTerm, double& iTerm, double& dTerm) const;
};

#endif // RPM_CONTROLLER_H
