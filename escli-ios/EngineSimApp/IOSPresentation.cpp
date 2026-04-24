// IOSPresentation.cpp - iOS-specific presentation layer implementation

#include "IOSPresentation.h"

namespace presentation {

IOSPresentation::IOSPresentation(telemetry::ITelemetryReader* telemetryReader)
    : telemetryReader_(telemetryReader) {
}

bool IOSPresentation::Initialize(const PresentationConfig& config) {
    (void)config;  // Unused in iOS mode
    initialized_ = true;
    return true;
}

void IOSPresentation::Shutdown() {
    initialized_ = false;
}

void IOSPresentation::ShowEngineState(const EngineState& state) {
    // iOS app reads from telemetry directly, not from presentation state
    // This is a no-op - telemetry is the single source of truth
    (void)state;
}

void IOSPresentation::ShowMessage(const std::string& message) {
    // iOS app doesn't have a console - could store for UI display if needed
    (void)message;
}

void IOSPresentation::ShowError(const std::string& error) {
    // iOS app could display this in UI - for now, just store
    lastError_ = error;
}

void IOSPresentation::ShowProgress(double currentTime, double duration) {
    // iOS app doesn't have a progress bar - could store for UI display if needed
    (void)currentTime;
    (void)duration;
}

void IOSPresentation::Update(double dt) {
    (void)dt;  // No periodic updates needed for iOS
}

} // namespace presentation
