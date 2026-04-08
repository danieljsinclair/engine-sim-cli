// CLIConfig.cpp - Audio loop configuration implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#include "CLIconfig.h"

#include <iostream>
#include <string>
#include <cmath>
#include <iomanip>

// ============================================================================
// Global State (required for signal handling)
// ============================================================================

std::atomic<bool> g_running(true);
std::atomic<bool> g_interactiveMode(false);

// ============================================================================
// Command Line Parsing
// ============================================================================

void printUsage(const char* progName) {
    std::cout << "Engine Simulator CLI v2.0\n";
    std::cout << "Usage: " << progName << " [options] <engine_config.mr> <output.wav>\n";
    std::cout << "   OR: " << progName << " --script <engine_config.mr> [options] [output.wav]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --script <path>      Path to engine .mr configuration file\n";
    std::cout << "  --rpm <value>        Target RPM to maintain (default: auto)\n";
    std::cout << "  --load <0-100>       FIXED throttle load percentage (ignored in interactive mode)\n";
    std::cout << "  --interactive        Enable interactive keyboard control (overrides --load)\n";
    std::cout << "  --play, --play-audio Play audio to speakers in real-time\n";
    std::cout << "  --duration <seconds> Duration in seconds (default: 3.0, ignored in interactive)\n";
    std::cout << "  --output <path>      Output WAV file path\n";
    std::cout << "  --default-engine     Use default engine from main repo (ignores config file)\n";
    std::cout << "  --sine               Generate 440Hz sine wave test tone (no engine sim)\n";
    std::cout << "  --threaded           Use threaded circular buffer (cursor-chasing) (sync-pull is default)\n";
    std::cout << "  --silent             Run full audio pipeline at zero volume (for testing)\n";
    std::cout << "  --cranking-volume    Volume boost during cranking (when ignition ON, RPM < 600, no exhaust flow)\n";
    std::cout << "  --sim-freq <Hz>      Physics Hz (default: " << EngineConstants::DEFAULT_SIMULATION_FREQUENCY 
              << ", range: " << EngineConstants::MIN_SIMULATION_FREQUENCY << "-" << EngineConstants::MAX_SIMULATION_FREQUENCY << ")\n";
    std::cout << "  --pre-fill-ms <ms>  Pre-fill buffer ms for sync-pull mode (default: 50)\n\n";
    std::cout << "NOTES:\n";
    std::cout << "  --load sets a FIXED throttle for non-interactive mode only\n";
    std::cout << "  In interactive mode, use J/K or Up/Down arrows to control load\n";
    std::cout << "  Use --rpm for RPM control mode (throttle auto-adjusts)\n";
    std::cout << "  Default mode is sync-pull (synchronous render in audio callback)\n";
    std::cout << "  Use --threaded for cursor-chasing circular buffer mode\n";
    std::cout << "  --sim-freq affects both modes - lower values reduce CPU load\n\n";
    std::cout << "Interactive Controls:\n";
    std::cout << "  A                      Toggle ignition on/off (starts ON)\n";
    std::cout << "  S                      Toggle starter motor on/off\n";
    std::cout << "  UP/DOWN Arrows or K/J  Increase/decrease throttle\n";
    std::cout << "  W                      Increase throttle\n";
    std::cout << "  SPACE                  Apply brake\n";
    std::cout << "  R                      Reset to idle\n";
    std::cout << "  Q/ESC                  Quit\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << progName << " --script v8_engine.mr --rpm 850 --duration 5 --output output.wav\n";
    std::cout << "  " << progName << " --script v8_engine.mr --interactive --play\n";
    std::cout << "  " << progName << " --script engine-sim-bridge/engine-sim/assets/main.mr --interactive --output recording.wav\n";
    std::cout << "  " << progName << " --default-engine --rpm 2000 --play --output engine.wav\n";
    std::cout << "  " << progName << " --default-engine --cranking-volume 2.0 --play  # 2x volume during cranking\n";
}

bool parseArguments(int argc, char* argv[], CommandLineArgs& args) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        }
        else if (arg == "--rpm") {
            if (++i >= argc) {
                std::cerr << "ERROR: --rpm requires a value\n";
                return false;
            }
            args.targetRPM = std::atof(argv[i]);
        }
        else if (arg == "--load") {
            if (++i >= argc) {
                std::cerr << "ERROR: --load requires a value\n";
                return false;
            }
            args.targetLoad = std::atof(argv[i]) / 100.0;
        }
        else if (arg == "--interactive") {
            args.interactive = true;
            g_interactiveMode.store(true);
        }
        else if (arg == "--play" || arg == "--play-audio") {
            args.playAudio = true;
        }
        else if (arg == "--script") {
            if (++i >= argc) {
                std::cerr << "ERROR: --script requires a path\n";
                return false;
            }
            args.engineConfig = argv[i];
        }
        else if (arg == "--duration") {
            if (++i >= argc) {
                std::cerr << "ERROR: --duration requires a value\n";
                return false;
            }
            args.duration = std::atof(argv[i]);
        }
        else if (arg == "--default-engine") {
            args.useDefaultEngine = true;
        }
        else if (arg == "--output") {
            if (++i >= argc) {
                std::cerr << "ERROR: --output requires a path\n";
                return false;
            }
            args.outputWav = argv[i];
        }
        else if (arg == "--sine") {
            args.sineMode = true;
        }
        else if (arg == "--threaded") {
            args.syncPull = false;
        }
        else if (arg == "--silent") {
            args.silent = true;
            args.playAudio = true;  // --silent implies --play (full audio pipeline)
        }
        else if (arg == "--cranking-volume") {
            if (++i >= argc) {
                std::cerr << "ERROR: --cranking-volume requires a value\n";
                return false;
            }
            args.crankingVolume = std::atof(argv[i]);
        }
        else if (arg == "--sim-freq") {
            if (++i >= argc) {
                std::cerr << "ERROR: --sim-freq requires a value (" << EngineConstants::MIN_SIMULATION_FREQUENCY << "-" << EngineConstants::MAX_SIMULATION_FREQUENCY << " Hz)\n";
                return false;
            }
            args.simulationFrequency = std::atoi(argv[i]);
            if (args.simulationFrequency < EngineConstants::MIN_SIMULATION_FREQUENCY || args.simulationFrequency > EngineConstants::MAX_SIMULATION_FREQUENCY) {
                std::cerr << "ERROR: --sim-freq must be between " << EngineConstants::MIN_SIMULATION_FREQUENCY << " and " << EngineConstants::MAX_SIMULATION_FREQUENCY << "\n";
                return false;
            }
        }
        else if (arg == "--pre-fill-ms") {
            if (++i >= argc) {
                std::cerr << "ERROR: --pre-fill-ms requires a value (10-500 ms)\n";
                return false;
            }
            args.preFillMs = std::atoi(argv[i]);
            if (args.preFillMs < 10 || args.preFillMs > 500) {
                std::cerr << "ERROR: --pre-fill-ms must be between 10 and 500\n";
                return false;
            }
        }
        else if (arg.rfind("--", 0) == 0) {
            std::cerr << "ERROR: Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return false;
        }
        else if (args.useDefaultEngine && args.outputWav == nullptr) {
            // When using default engine, first positional arg is output file
            args.outputWav = argv[i];
        }
        else if (!args.useDefaultEngine && args.engineConfig == nullptr) {
            args.engineConfig = argv[i];
        }
        else if (args.outputWav == nullptr) {
            args.outputWav = argv[i];
        }
        else {
            std::cerr << "ERROR: Unexpected argument: " << arg << "\n";
            return false;
        }
    }

    // Use default engine if requested
    if (args.useDefaultEngine) {
        // Will use main.mr from main repo
        args.engineConfig = "(default engine)";
    }

    // Engine config is required unless in sine mode
    if (args.engineConfig == nullptr && !args.sineMode) {
        std::cerr << "ERROR: Engine configuration file is required\n";
        std::cerr << "       Use --script <path>, --sine, or provide positional argument\n\n";
        printUsage(argv[0]);
        return false;
    }

    // Validate arguments
    if (args.targetRPM < 0 || args.targetRPM > 20000) {
        std::cerr << "ERROR: RPM must be between 0 and 20000\n";
        return false;
    }

    if (args.targetLoad < -1.0 || args.targetLoad > 1.0) {
        std::cerr << "ERROR: Load must be between 0 and 100\n";
        return false;
    }

    // Auto-enable RPM mode if target RPM is specified and load is not
    if (args.targetRPM > 0 && args.targetLoad < 0) {
        args.targetLoad = -1.0;  // Auto mode
    }

    return true;
}

// ============================================================================
// Shows the configuration on startup in a banner format
// ============================================================================
void ShowConfigHeader(CommandLineArgs& args, const char* engineAPIVersion = "unknown") {
    // Verify build ID
    if (engineAPIVersion != nullptr) {
        std::cout << "[Bridge: " << engineAPIVersion << "]\n";
    }

    std::cout << "Configuration:\n";
    if (args.sineMode) {
        std::cout << "  Mode: RPM-Linked Sine Wave Test\n";
        std::cout << "  Mapping: 600 RPM = 100 Hz, 6000 RPM = 1000 Hz\n";
        std::cout << "  Engine: Default (Subaru EJ25)\n";
    } else {
        std::cout << "  Engine: " << (args.engineConfig ? args.engineConfig : "(none)") << "\n";
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
    std::cout << "  Sim Freq: \x1b[32m" << args.simulationFrequency << " Hz\x1b[0m\n";
    std::cout << "\n";
}
