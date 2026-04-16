// CLIMain.cpp - Main entry point implementation
// Uses IAudioStrategyFactory directly (no adapter layer)
// Phase E: Creates BridgeSimulator (ISimulator) instead of raw EngineSimAPI

#include "CLIMain.h"

#include "CLIconfig.h"

#include "IAudioStrategy.h"
#include "ITelemetryProvider.h"
#include "simulation/SimulationLoop.h"
#include "BridgeSimulator.h"
#include "input/IInputProvider.h"
#include "input/KeyboardInputProvider.h"
#include "presentation/IPresentation.h"
#include "presentation/ConsolePresentation.h"
#include "simulation/EngineConfig.h"
#include "ILogging.h"

#include <iostream>
#include <csignal>
#include <memory>

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
    return nullptr;
}

} // anonymous namespace

SimulationConfig CreateSimulationConfig(const CommandLineArgs& args, ILogging* logger) {
    SimulationConfig config(logger);

    config.configPath = args.engineConfig ? args.engineConfig : "";
    config.assetBasePath = "";

    config.duration = args.duration;
    config.interactive = args.interactive;
    config.playAudio = args.playAudio;
    config.volume = args.silent ? 0.0f : 1.0f;
    config.sineMode = args.sineMode;
    config.syncPull = args.syncPull;
    config.targetRPM = args.targetRPM;
    config.targetLoad = args.targetLoad;
    config.useDefaultEngine = args.useDefaultEngine;
    config.simulationFrequency = args.simulationFrequency;
    config.preFillMs = args.preFillMs;
    if (args.outputWav) config.outputWav = args.outputWav;

    return config;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "Engine Simulator CLI v2.0\n";
    std::cout << "========================\n\n";

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    auto cliLogger = std::make_unique<ConsoleLogger>();

    CommandLineArgs args;
    if (!parseArguments(argc, argv, args)) {
        return 1;
    }

    BridgeSimulator simulator;
    ShowConfigHeader(args, ISimulator::getVersion());

    const int sampleRate = 44100;
    SimulationConfig config = CreateSimulationConfig(args, cliLogger.get());

    // Create shared telemetry (simulator and strategies write, presentation reads)
    auto telemetry = std::make_unique<telemetry::InMemoryTelemetry>();
    simulator.setTelemetryWriter(telemetry.get());

    // Create strategy via factory - pass telemetry so strategies push diagnostics
    AudioMode mode = config.syncPull ? AudioMode::SyncPull : AudioMode::Threaded;
    std::unique_ptr<IAudioStrategy> audioStrategy = IAudioStrategyFactory::createStrategy(mode, cliLogger.get(), telemetry.get());

    // Wire telemetry into simulation config for presentation reads
    config.telemetryWriter = telemetry.get();
    config.telemetryReader = telemetry.get();

    input::IInputProvider* inputProvider = createInputProvider(args.interactive, cliLogger.get());
    presentation::IPresentation* presentation = createPresentation(args);

    // Run simulation with ISimulator (holy trinity: ISimulator -> IAudioStrategy -> IAudioHardwareProvider)
    int result = runSimulation(config, simulator, audioStrategy.get(), inputProvider, presentation);

    delete inputProvider;
    delete presentation;

    return result;
}
