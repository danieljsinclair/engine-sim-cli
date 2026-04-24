// IOSInputProvider.h - iOS-specific input provider for engine simulation
// Implements IInputProvider for iOS SwiftUI input
// Thread-safe state storage for throttle/ignition/starter controls
// Lives in iOS app target, NOT in the bridge

#ifndef IOS_INPUT_PROVIDER_H
#define IOS_INPUT_PROVIDER_H

#include "io/IInputProvider.h"
#include <atomic>
#include <string>

namespace input {

// ============================================================================
// IOSInputProvider - Input provider for iOS SwiftUI interface
// ============================================================================
//
// Stores control state from UI (throttle, ignition, starter) in thread-safe
// atomics. OnUpdateSimulation() returns current state for the simulation loop.
// Setter methods are called from the .mm wrapper (Swift UI -> C++ bridge).
//
// Usage:
//   1. Create from IOSRunner
//   2. .mm wrapper calls setThrottle()/setIgnition()/setStarterMotor() from UI
//   3. Simulation loop calls OnUpdateSimulation() each tick to get current inputs
//   4. requestStop() signals shouldContinue=false for graceful shutdown
//
// Thread safety: All state is std::atomic for lock-free reads from simulation thread
// ============================================================================

class IOSInputProvider : public IInputProvider {
public:
    IOSInputProvider();
    ~IOSInputProvider() override = default;

    // Non-copyable
    IOSInputProvider(const IOSInputProvider&) = delete;
    IOSInputProvider& operator=(const IOSInputProvider&) = delete;

    // ========================================================================
    // IInputProvider implementation
    // ========================================================================

    bool Initialize() override;
    void Shutdown() override;
    bool IsConnected() const override;
    EngineInput OnUpdateSimulation(double dt) override;
    std::string GetProviderName() const override;
    std::string GetLastError() const override;

    // ========================================================================
    // iOS-specific setters (called from .mm wrapper / Swift UI)
    // ========================================================================
    // Thread-safe: store values in atomics for simulation thread to read

    void setThrottle(double position);
    void setIgnition(bool enabled);
    void setStarterMotor(bool enabled);
    void requestStop();

private:
    // Thread-safe control state (atomics for lock-free reads)
    std::atomic<double> throttle_{0.1};
    std::atomic<bool> ignition_{true};
    std::atomic<bool> starterMotor_{false};
    std::atomic<bool> shouldContinue_{true};

    // Diagnostics
    std::string lastError_;
    bool initialized_{false};
};

} // namespace input

#endif // IOS_INPUT_PROVIDER_H
