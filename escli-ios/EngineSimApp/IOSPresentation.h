// IOSPresentation.h - iOS-specific presentation layer for engine simulation
// Implements IPresentation for iOS SwiftUI display
// Lives in iOS app target, NOT in the bridge

#ifndef IOS_PRESENTATION_H
#define IOS_PRESENTATION_H

#include "io/IPresentation.h"
#include "telemetry/ITelemetryProvider.h"
#include <atomic>
#include <memory>

namespace presentation {

// ============================================================================
// IOSPresentation - Presentation layer for iOS SwiftUI interface
// ============================================================================
//
// Minimal implementation of IPresentation for iOS.
// Telemetry is the source of truth - this class doesn't shadow state.
// The .mm wrapper reads from ITelemetryReader, not from this.
//
// Usage:
//   1. Create from IOSRunner with ITelemetryReader
//   2. Simulation loop calls ShowEngineState() each tick (logs or no-op)
//   3. .mm wrapper reads from ITelemetryReader directly for Swift UI
//
// ============================================================================

class IOSPresentation : public IPresentation {
public:
    explicit IOSPresentation(telemetry::ITelemetryReader* telemetryReader);
    ~IOSPresentation() override = default;

    // Non-copyable
    IOSPresentation(const IOSPresentation&) = delete;
    IOSPresentation& operator=(const IOSPresentation&) = delete;

    // ========================================================================
    // IPresentation implementation
    // ========================================================================

    bool Initialize(const PresentationConfig& config) override;
    void Shutdown() override;
    void ShowEngineState(const EngineState& state) override;
    void ShowMessage(const std::string& message) override;
    void ShowError(const std::string& error) override;
    void ShowProgress(double currentTime, double duration) override;
    void Update(double dt) override;

private:
    // Telemetry reader for accurate state (not shadowed here)
    telemetry::ITelemetryReader* telemetryReader_;

    // Diagnostics
    bool initialized_{false};
    mutable std::string lastError_;
};

} // namespace presentation

#endif // IOS_PRESENTATION_H
