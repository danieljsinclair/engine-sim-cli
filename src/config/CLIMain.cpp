// CLIMain.cpp - Main entry point implementation
// Refactored with dependency injection

#include "CLIMain.h"

#include "CLIconfig.h"
#include "AudioPlayer.h"
#include "audio/modes/IAudioMode.h"
#include "simulation/SimulationLoop.h"
#include "input/IInputProvider.h"
#include "input/KeyboardInputProvider.h"
#include "presentation/IPresentation.h"
#include "presentation/ConsolePresentation.h"
#include "bridge/engine_sim_loader.h"
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

SimulationConfig CreateSimulationConfig(const CommandLineArgs& args) {
    SimulationConfig config;

    // SRP: CLI just passes raw script path - Bridge handles path resolution
    config.configPath = args.engineConfig ? args.engineConfig : "";
    config.assetBasePath = "";  // Empty - Bridge will derive from configPath

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

    // Setup signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Create CLI logger for internal logging
    auto cliLogger = std::make_unique<ConsoleLogger>();

    // Parse command line arguments
    CommandLineArgs args;
    if (!parseArguments(argc, argv, args)) {
        return 1;
    }

    // Load engine-sim library dynamically
    EngineSimAPI engineAPI = {};
    ShowConfigHeader(args, engineAPI.GetVersion());

    // SETUP simulator
    const int sampleRate = 44100;
    SimulationConfig config = CreateSimulationConfig(args);
    config.logger = cliLogger.get();  // Inject logger into config

    // Create dependencies with logger injection
    IAudioMode* audioMode = createAudioModeFactory(&engineAPI, config.syncPull, cliLogger.get()).release();
    input::IInputProvider* inputProvider = createInputProvider(args.interactive, cliLogger.get());
    presentation::IPresentation* presentation = createPresentation(args);

    // MAIN LOOP - runs the simulation with injected dependencies
    int result = runSimulation(config, engineAPI, audioMode, inputProvider, presentation);

    // Cleanup dependencies we injected
    delete audioMode;
    delete inputProvider;
    delete presentation;

    return result;
}
