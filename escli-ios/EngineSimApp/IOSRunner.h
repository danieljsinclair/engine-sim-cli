// IOSRunner.h - iOS main entry point for engine simulation
// C++ entry point that follows CLIMain.cpp pattern
// Runs simulation loop on background thread for iOS app
// Lives in iOS app target, NOT in the bridge

#ifndef IOS_RUNNER_H
#define IOS_RUNNER_H

#include <string>
#include <memory>
#include <thread>
#include <atomic>

// Forward declarations
class ISimulator;
class IAudioBuffer;
namespace input { class IOSInputProvider; }
namespace presentation { class IOSPresentation; }
class ILogging;
namespace telemetry {
    class ITelemetryWriter;
    class ITelemetryReader;
    class InMemoryTelemetry;
}

// ============================================================================
// IOSRunnerContext - Context handle for iOS simulation
// ============================================================================
// Internal context held by IOSRunner
// ============================================================================

struct IOSRunnerContext {
    std::unique_ptr<ISimulator> simulator;
    std::unique_ptr<IAudioBuffer> audioBuffer;
    std::unique_ptr<input::IOSInputProvider> inputProvider;
    std::unique_ptr<presentation::IOSPresentation> presentation;
    std::unique_ptr<ILogging> logger;
    std::unique_ptr<telemetry::InMemoryTelemetry> telemetry;
    std::unique_ptr<std::thread> simulationThread;

    IOSRunnerContext() = default;
    ~IOSRunnerContext() = default;

    // Non-copyable, non-movable
    IOSRunnerContext(const IOSRunnerContext&) = delete;
    IOSRunnerContext& operator=(const IOSRunnerContext&) = delete;
    IOSRunnerContext(IOSRunnerContext&&) = delete;
    IOSRunnerContext& operator=(IOSRunnerContext&&) = delete;
};

// ============================================================================
// IOSRunner - Main iOS simulation runner
// ============================================================================
// Mirrors CLIMain.cpp: creates simulator, telemetry, audio buffer,
// and runs simulation on background thread.
// ============================================================================

class IOSRunner {
public:
    IOSRunner();
    ~IOSRunner();

    // Non-copyable
    IOSRunner(const IOSRunner&) = delete;
    IOSRunner& operator=(const IOSRunner&) = delete;

    // Start simulation on background thread (mirrors CLIMain.cpp)
    bool start();

    // Stop simulation
    void stop();

    // Control forwarding (called from .mm wrapper)
    void setThrottle(double position);
    void setIgnition(bool enabled);
    void setStarterMotor(bool enabled);

    // Read telemetry (called from .mm wrapper) — NO shadow copies
    telemetry::ITelemetryReader* getTelemetryReader();

    bool isRunning() const;

private:
    std::unique_ptr<IOSRunnerContext> context_;
};

#endif // IOS_RUNNER_H
