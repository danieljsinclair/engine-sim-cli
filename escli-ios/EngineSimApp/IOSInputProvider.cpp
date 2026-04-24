// IOSInputProvider.cpp - iOS-specific input provider implementation

#include "IOSInputProvider.h"
#include <cmath>

namespace input {

IOSInputProvider::IOSInputProvider() {
}

bool IOSInputProvider::Initialize() {
    initialized_ = true;
    lastError_.clear();
    return true;
}

void IOSInputProvider::Shutdown() {
    initialized_ = false;
}

bool IOSInputProvider::IsConnected() const {
    return initialized_;
}

input::EngineInput IOSInputProvider::OnUpdateSimulation(double dt) {
    (void)dt;  // Unused in iOS mode (time-based input not needed)

    EngineInput input;
    input.throttle = throttle_.load(std::memory_order_relaxed);
    input.ignition = ignition_.load(std::memory_order_relaxed);
    input.starterMotor = starterMotor_.load(std::memory_order_relaxed);
    input.shouldContinue = shouldContinue_.load(std::memory_order_relaxed);

    return input;
}

std::string IOSInputProvider::GetProviderName() const {
    return "IOSInputProvider";
}

std::string IOSInputProvider::GetLastError() const {
    return lastError_;
}

// ============================================================================
// iOS-specific setters (called from .mm wrapper / Swift UI)
// ============================================================================

void IOSInputProvider::setThrottle(double position) {
    // Clamp to valid range [0.0, 1.0]
    double clamped = std::max(0.0, std::min(1.0, position));
    throttle_.store(clamped, std::memory_order_relaxed);
}

void IOSInputProvider::setIgnition(bool enabled) {
    ignition_.store(enabled, std::memory_order_relaxed);
}

void IOSInputProvider::setStarterMotor(bool enabled) {
    starterMotor_.store(enabled, std::memory_order_relaxed);
}

void IOSInputProvider::requestStop() {
    shouldContinue_.store(false, std::memory_order_relaxed);
}

} // namespace input
