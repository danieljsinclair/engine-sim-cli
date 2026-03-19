// CLIMain.cpp - Main entry point implementation
// Refactored with dependency injection (Feedback #2, #4)

#include "CLIMain.h"

#include "AudioConfig.h"
#include "AudioPlayer.h"
#include "AudioPlayerFactory.h"
#include "audio/modes/IAudioMode.h"
#include "SimulationLoop.h"
#include "KeyboardInput.h"
#include "engine_sim_loader.h"

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
// Dependency Constructors (Feedback #4 - Helper functions to construct before call)
// ============================================================================

namespace {

KeyboardInput* createKeyboardInput(bool interactive) {
    if (!interactive) {
        return nullptr;
    }
    return new KeyboardInput();
}

} // anonymous namespace

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
    if (!LoadEngineSimLibrary(engineAPI, args.sineMode)) {
        std::cerr << "ERROR: Failed to load engine-sim library\n";
        return 1;
    }

    // Verify build ID
    if (engineAPI.GetVersion) {
        std::cout << "[Bridge: " << engineAPI.GetVersion() << "]\n";
    }

    std::cout << "Configuration:\n";
    if (args.sineMode) {
        std::cout << "  Mode: RPM-Linked Sine Wave Test\n";
        std::cout << "  Mapping: 600 RPM = 100 Hz, 6000 RPM = 1000 Hz\n";
        std::cout << "  Engine: Default (Subaru EJ25)\n";
    } else {
        std::cout << "  Engine: " << args.engineConfig << "\n";
    }
    std::cout << "  Output: " << (args.outputWav ? args.outputWav : "(none - audio not saved)") << "\n";
    if (args.interactive) {
        std::cout << "  Duration: (interactive - runs until quit)\n";
    } else {
        std::cout << "  Duration: " << args.duration << " seconds\n";
    }
    if (args.targetRPM > 0) {
        std::cout << "  Target RPM: " << args.targetRPM << "\n";
    }
    if (args.targetLoad >= 0) {
        std::cout << "  Target Load: " << static_cast<int>(args.targetLoad * 100) << "%\n";
    }
    std::cout << "  Interactive: " << (args.interactive ? "Yes" : "No") << "\n";
    std::cout << "  Audio Playback: " << (args.playAudio ? "Yes" : "No") << "\n";
    std::cout << "  Audio Mode: " << (args.syncPull ? "Sync-Pull (default)" : "Threaded (cursor-chasing)") << "\n";
    if (args.crankingVolume != 1.0f) {
        std::cout << "  Cranking Volume: " << args.crankingVolume << "x\n";
    }
    if (args.silent) {
        std::cout << "  Silent: Yes (zero volume, full audio pipeline)\n";
    }
    std::cout << "\n";

    // Create dependencies BEFORE calling runSimulation (Dependency Injection)
    // Note: We create AudioPlayer but don't create a simulator here anymore.
    // The simulator is created in SimulationLoop to avoid issues with the real bridge.
    const int sampleRate = 44100;
    
    // AudioPlayer - will be created in SimulationLoop after simulator is ready
    AudioPlayer* audioPlayer = nullptr;
    
    // Inject audioMode - factory decides internally based on API capabilities
    IAudioMode* audioMode = createAudioModeFactory(&engineAPI).release();
    
    // Inject keyboardInput - constructed before call (Feedback #4)
    KeyboardInput* keyboardInput = createKeyboardInput(args.interactive);

    // Run simulation - simulator is created inside runSimulation
    int result = runSimulation(args, engineAPI, audioPlayer, audioMode, keyboardInput);

    // Cleanup injected dependencies
    delete audioPlayer;
    delete audioMode;
    delete keyboardInput;

    // Cleanup: unload library
    UnloadEngineSimLibrary(engineAPI);

    return result;
}
