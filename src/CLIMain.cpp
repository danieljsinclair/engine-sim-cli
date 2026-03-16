// CLIMain.cpp - Main entry point implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#include "CLIMain.h"

#include "AudioConfig.h"
#include "SimulationLoop.h"
#include "engine_sim_loader.h"

#include <iostream>
#include <csignal>

// ============================================================================
// Signal Handler
// ============================================================================

void signalHandler(int signal) {
    g_running.store(false);
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
        std::cout << "  Mapping: 600 RPM = 100Hz, 6000 RPM = 1000Hz\n";
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
    if (args.silent) {
        std::cout << "  Silent: Yes (zero volume, full audio pipeline)\n";
    }
    std::cout << "\n";

    // Run simulation - pass the loaded engineAPI
    int result = runSimulation(args, engineAPI);

    // Cleanup: unload library
    UnloadEngineSimLibrary(engineAPI);

    return result;
}
