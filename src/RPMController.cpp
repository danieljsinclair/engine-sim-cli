// RPMController.cpp - RPM control implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#include "RPMController.h"

#include <algorithm>
#include <cmath>

// ============================================================================
// RPM Controller Implementation
// ============================================================================

RPMController::RPMController() : targetRPM(0),
                 kp(KP),
                 ki(KI),
                 kd(KD),
                 integral(0),
                 lastError(0),
                 firstUpdate(true) {}

void RPMController::setTargetRPM(double rpm) {
    targetRPM = rpm;
    integral = 0;  // Reset integral when target changes
    firstUpdate = true;
}

double RPMController::update(double currentRPM, double dt) {
    if (targetRPM <= 0) return 0.0;

    // Only enable RPM control above minimum RPM to prevent hunting at idle
    if (currentRPM < MIN_RPM_FOR_CONTROL) {
        // Use minimum throttle to keep engine running but don't control RPM
        return MIN_THROTTLE;
    }

    double error = targetRPM - currentRPM;

    // Simple P-term calculation - responsive but stable
    double pTerm = error * kp;

    // No additional smoothing or rate limiting - let the main loop smoothing handle this
    double throttle = pTerm;

    // Conditional minimum throttle: only apply minimum when accelerating
    // When error > 0 (need to accelerate): use MIN_THROTTLE to prevent stalling
    // When error <= 0 (need to decelerate): allow 0.0 throttle to let engine slow down
    double minThrottle = (error > 0) ? MIN_THROTTLE : 0.0;

    // Clamp throttle with conditional minimum
    return std::max(minThrottle, std::min(MAX_THROTTLE, throttle));
}

void RPMController::getDebugInfo(double& pTerm, double& iTerm, double& dTerm) const {
    // Note: pTerm would need to be calculated from current error
    pTerm = 0.0;
    iTerm = integral * ki;
    dTerm = 0.0;
}
