// IOSRunner.cpp - iOS main entry point implementation
// Follows CLIMain.cpp pattern: SimulatorFactory -> IAudioBufferFactory -> runSimulation()

#include "IOSRunner.h"

#include "simulator/SimulatorFactory.h"
#include "simulator/EngineSimTypes.h"
#include "simulation/SimulationLoop.h"
#include "strategy/IAudioBuffer.h"
#include "io/IInputProvider.h"
#include "IOSInputProvider.h"
#include "io/IPresentation.h"
#include "IOSPresentation.h"
#include "common/ILogging.h"
#include "telemetry/ITelemetryProvider.h"

#include <stdexcept>
#include <thread>
#include <chrono>

// ============================================================================
// Private helper functions
// ============================================================================

namespace {

// Create simulation config with iOS-specific settings
SimulationConfig createSimulationConfig() {
    SimulationConfig config;

    config.interactive = true;   // iOS is always interactive
    config.playAudio = true;     // iOS always plays audio
    config.duration = 0.0;       // 0 = infinite (until stop requested)
    config.syncPull = false;     // iOS uses threaded mode
    config.useDefaultEngine = true;  // Use sine mode (no Pirahna on iOS)

    // ISimulatorConfig default ctor populates from EngineSimDefaults — same pattern as CLIMain
    config.simulatorLabel = "[iOS]";

    return config;
}

// Simulation loop thread function
void simulationThreadFunc(IOSRunnerContext* context, SimulationConfig config) {
    if (!context || !context->simulator || !context->audioBuffer) {
        return;
    }

    try {
        // Run the simulation loop (blocking call)
        runSimulation(
            config,
            *context->simulator,
            context->audioBuffer.get(),
            context->inputProvider.get(),
            context->presentation.get(),
            context->telemetry.get(),
            context->telemetry.get(),
            context->logger.get()
        );
    } catch (const std::exception& e) {
        if (context->logger) {
            context->logger->error(LogMask::BRIDGE, "Simulation thread error: %s", e.what());
        }
    }
}

} // anonymous namespace

// ============================================================================
// IOSRunner implementation
// ============================================================================

IOSRunner::IOSRunner()
    : context_(std::make_unique<IOSRunnerContext>()) {
}

IOSRunner::~IOSRunner() {
    stop();
}

bool IOSRunner::start() {
    if (context_->simulationThread && context_->simulationThread->joinable()) {
        // Already started
        return false;
    }

    try {
        // Create logger
        context_->logger = std::make_unique<ConsoleLogger>();
        context_->logger->info(LogMask::BRIDGE, "iOS simulation starting...");

        // Create telemetry
        context_->telemetry = std::make_unique<telemetry::InMemoryTelemetry>();

        // Create simulation config
        SimulationConfig config = createSimulationConfig();

        // Create simulator via factory (SineWave - no Pirahna on iOS)
        context_->simulator = SimulatorFactory::create(
            SimulatorType::SineWave,
            "",  // Empty script path for sine mode
            "",  // Empty asset base path
            config.engineConfig,
            context_->logger.get(),
            context_->telemetry.get()
        );

        if (!context_->simulator) {
            context_->logger->error(LogMask::BRIDGE, "Failed to create simulator");
            return false;
        }

        // Create audio buffer (threaded mode for iOS)
        AudioMode mode = AudioMode::Threaded;
        context_->audioBuffer = IAudioBufferFactory::createBuffer(
            mode,
            context_->logger.get(),
            context_->telemetry.get()
        );

        if (!context_->audioBuffer) {
            context_->logger->error(LogMask::BRIDGE, "Failed to create audio buffer");
            return false;
        }

        // Create iOS-specific input provider
        context_->inputProvider = std::make_unique<input::IOSInputProvider>();
        if (!context_->inputProvider->Initialize()) {
            context_->logger->error(LogMask::BRIDGE, "Failed to initialize input provider");
            return false;
        }

        // Create iOS-specific presentation layer
        context_->presentation = std::make_unique<presentation::IOSPresentation>(
            context_->telemetry.get()
        );

        presentation::PresentationConfig presConfig;
        presConfig.interactive = true;
        presConfig.duration = 0.0;  // Infinite
        if (!context_->presentation->Initialize(presConfig)) {
            context_->logger->error(LogMask::BRIDGE, "Failed to initialize presentation");
            return false;
        }

        // Start simulation on background thread
        context_->simulationThread = std::unique_ptr<std::thread>(
            new std::thread(simulationThreadFunc, context_.get(), std::move(config))
        );

        context_->logger->info(LogMask::BRIDGE, "iOS simulation started successfully");

        return true;

    } catch (const std::exception& e) {
        if (context_->logger) {
            context_->logger->error(LogMask::BRIDGE, "Failed to start simulation: %s", e.what());
        }
        return false;
    }
}

void IOSRunner::stop() {
    if (!context_->simulationThread || !context_->simulationThread->joinable()) {
        return;
    }

    // Request stop via input provider
    if (context_->inputProvider) {
        context_->inputProvider->requestStop();
    }

    // Wait for simulation thread to complete
    if (context_->simulationThread->joinable()) {
        context_->simulationThread->join();
    }

    // Cleanup is automatic via unique_ptr
    context_->simulationThread.reset();
}

void IOSRunner::setThrottle(double position) {
    if (context_->inputProvider) {
        context_->inputProvider->setThrottle(position);
    }
}

void IOSRunner::setIgnition(bool enabled) {
    if (context_->inputProvider) {
        context_->inputProvider->setIgnition(enabled);
    }
}

void IOSRunner::setStarterMotor(bool enabled) {
    if (context_->inputProvider) {
        context_->inputProvider->setStarterMotor(enabled);
    }
}

telemetry::ITelemetryReader* IOSRunner::getTelemetryReader() {
    return context_->telemetry.get();
}

bool IOSRunner::isRunning() const {
    return context_->simulationThread && context_->simulationThread->joinable();
}
