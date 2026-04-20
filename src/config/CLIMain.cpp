// CLIMain.cpp - Main entry point implementation
// Uses IAudioBufferFactory directly (no adapter layer)
// Phase E: Creates BridgeSimulator (ISimulator) instead of raw EngineSimAPI

#include "CLIMain.h"

#include "CLIconfig.h"

#include "strategy/IAudioBuffer.h"
#include "telemetry/ITelemetryProvider.h"
#include "simulation/SimulationLoop.h"
#include "simulator/SimulatorFactory.h"
#include "io/IInputProvider.h"
#include "input/KeyboardInputProvider.h"
#include "io/IPresentation.h"
#include "presentation/ConsolePresentation.h"
#include "common/ILogging.h"
#include "config/ANSIColors.h"

#include <iostream>
#include <csignal>
#include <memory>
#include <stdexcept>

// ============================================================================
// Signal Handler
// ============================================================================

void signalHandler(int signal) {
    (void)signal;
    g_running.store(false);
}

// ============================================================================
// Dependency Constructors - Create injectable providers
// ============================================================================

namespace {

input::IInputProvider* createInputProvider(bool interactive, ILogging* logger) {
    if (interactive) {
        auto provider = std::make_unique<input::KeyboardInputProvider>(logger);
        if (provider->Initialize()) {
            return provider.release();
        }
        throw std::runtime_error("Failed to initialize input provider");
    }
    return nullptr;
}

presentation::IPresentation* createPresentation(const CommandLineArgs& args) {
    presentation::PresentationConfig config;
    config.interactive = args.interactive;
    config.duration = args.duration;
    config.showDiagnostics = true;

    auto pres = std::make_unique<presentation::ConsolePresentation>();
    if (pres->Initialize(config)) {
        return pres.release();
    }
    throw std::runtime_error("Failed to initialize presentation");
}

} // anonymous namespace

SimulationConfig CreateSimulationConfig(const CommandLineArgs& args) {
    SimulationConfig config;

    config.configPath = args.engineConfig ? args.engineConfig : "";
    config.assetBasePath = "";

    config.duration = args.duration;
    config.interactive = args.interactive;
    config.playAudio = args.playAudio;
    config.volume = args.silent ? 0.0f : 1.0f;
    config.syncPull = args.syncPull;
    config.targetRPM = args.targetRPM;
    config.targetLoad = args.targetLoad;
    config.useDefaultEngine = args.useDefaultEngine;
    config.simulationFrequency = args.simulationFrequency;
    config.preFillMs = args.preFillMs;
    if (args.outputWav) config.outputWav = args.outputWav;

    // Color the simulator label for CLI output
    std::string name = config.configPath.empty() ? "[DEFAULT]" : config.configPath;
    config.simulatorLabel = ANSIColors::CYAN + name + ANSIColors::RESET;

    return config;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    int result = 1;
    
    std::cout << "Engine Simulator CLI v2.0\n";
    std::cout << "========================\n\n";

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    CommandLineArgs args;
    if (parseArguments(argc, argv, args)) {
        ShowConfigHeader(args, ISimulator::getVersion());

        // Create shared telemetry (simulator and strategies write, presentation reads)
        auto cliLogger = std::make_unique<ConsoleLogger>();
        auto telemetry = std::make_unique<telemetry::InMemoryTelemetry>();

        // Create simulator via factory
        SimulatorType simType = args.sineMode ? SimulatorType::SineWave : SimulatorType::PistonEngine;
        std::unique_ptr<ISimulator> simulator = SimulatorFactory::create(simType, cliLogger.get());

        // Create strategy via factory - pass telemetry so strategies push diagnostics
        SimulationConfig config = CreateSimulationConfig(args);
        AudioMode mode = config.syncPull ? AudioMode::SyncPull : AudioMode::Threaded;
        std::unique_ptr<IAudioBuffer> audioStrategy = IAudioBufferFactory::createStrategy(mode, cliLogger.get(), telemetry.get());
        input::IInputProvider* inputProvider = createInputProvider(args.interactive, cliLogger.get());
        presentation::IPresentation* presentation = createPresentation(args);

        // Run simulation with ISimulator (holy trinity: ISimulator -> IAudioBuffer -> IAudioHardwareProvider)
        try
        {
            result = runSimulation(config, *simulator, audioStrategy.get(), inputProvider, presentation, telemetry.get(), telemetry.get(), cliLogger.get());
        }
        catch (const std::exception& e) {
            std::cerr << "ERROR: " << e.what() << std::endl;
        }

        delete inputProvider;
        delete presentation;
    }
    return result;
}
