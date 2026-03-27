// CLIMain.cpp - Main entry point implementation
// Refactored with dependency injection

#include "CLIMain.h"

#include "CLIconfig.h"
#include "AudioPlayer.h"
#include "audio/modes/IAudioMode.h"
#include "SimulationLoop.h"
#include "interfaces/IInputProvider.h"
#include "interfaces/KeyboardInputProvider.h"
#include "interfaces/IPresentation.h"
#include "interfaces/ConsolePresentation.h"
#include "engine_sim_loader.h"
#include "EngineConfig.h"

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

input::IInputProvider* createInputProvider(bool interactive) {
    if (interactive) {
        auto provider = std::make_unique<input::KeyboardInputProvider>();
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
    config.sineMockMode = args.sineMockMode;
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

    // Parse command line arguments
    CommandLineArgs args;
    if (!parseArguments(argc, argv, args)) {
        return 1;
    }

    // Load engine-sim library dynamically based on mode
    EngineSimAPI engineAPI = {};
    // TODO: Make bridge handle sine mode internally so we don't need special casing
    bool useMock = args.sineMockMode;  // Only use mock for --sine-mock, not --sine
    if (!LoadEngineSimLibrary(engineAPI, useMock)) {
        std::cerr << "ERROR: Failed to load engine-sim library\n";
        return 1;
    }

    ShowConfigHeader(args, engineAPI.GetVersion());

    // SETUP simulator
    const int sampleRate = 44100;
    SimulationConfig config = CreateSimulationConfig(args);
    IAudioMode* audioMode = createAudioModeFactory(&engineAPI, config.syncPull).release();
    input::IInputProvider* inputProvider = createInputProvider(args.interactive);
    presentation::IPresentation* presentation = createPresentation(args);

    // MAIN LOOP - runs the simulation with injected dependencies
    int result = runSimulation(config, engineAPI, audioMode, inputProvider, presentation);

    // Cleanup dependencies we injected
    delete audioMode;
    delete inputProvider;
    delete presentation;

    // Cleanup: unload library
    UnloadEngineSimLibrary(engineAPI);

    return result;
}
