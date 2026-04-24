// CLIConfig.cpp - Audio loop configuration implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#include "CLIconfig.h"
#include "simulation/SimulationLoop.h"

#include <CLI/CLI.hpp>
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
    std::cout << "  --sim-freq <Hz>      Physics Hz (default: " << EngineSimDefaults::SIMULATION_FREQUENCY
              << ", range: " << (EngineSimDefaults::SIMULATION_FREQUENCY / 10) << "-" << (EngineSimDefaults::SIMULATION_FREQUENCY * 10) << ")\n";
    std::cout << "  --synth-latency <s>  Synthesizer latency in seconds (default: " << EngineSimDefaults::TARGET_SYNTH_LATENCY << ")\n";
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
    CLI::App app{"Engine Simulator CLI v2.0"};
    app.set_help_flag("-h,--help", "Show help information");
    app.allow_extras(false);

    double loadArg = -1.0;
    std::string scriptPath;
    std::string positionalEngineConfig;

    app.add_option("--rpm", args.targetRPM, "Target RPM to maintain (default: auto)") ->check(CLI::Range(0.0, 20000.0));
    app.add_option("--load", loadArg, "FIXED throttle load percentage (ignored in interactive mode)") ->check(CLI::Range(0.0, 100.0));
    app.add_option("--output", args.outputWav, "Output WAV file path");
    app.add_option("--duration", args.duration, "Duration in seconds (default: 3.0, ignored in interactive)") ->default_val(3.0);
    app.add_option("--sim-freq", args.simulationFrequency, "Physics Hz (default: " + std::to_string(EngineSimDefaults::SIMULATION_FREQUENCY) + ")") ->check(CLI::Range(EngineSimDefaults::SIMULATION_FREQUENCY / 10, EngineSimDefaults::SIMULATION_FREQUENCY * 10));
    app.add_option("--synth-latency", args.synthLatency, "Synthesizer latency in seconds (default: " + std::to_string(EngineSimDefaults::TARGET_SYNTH_LATENCY) + ")") ->check(CLI::Range(0.001, 0.5));
    app.add_option("--pre-fill-ms", args.preFillMs, "Pre-fill buffer ms for sync-pull mode") ->check(CLI::Range(10, 500));
    app.add_option("--cranking-volume", args.crankingVolume, "Volume boost during cranking (when ignition ON, RPM < 600, no exhaust flow)") ->default_val(1.0f);
    app.add_option("output_wav", args.outputWav, "Output WAV file") ->required(false);

    auto scriptOpt = app.add_option("--script", scriptPath, "Path to engine .mr configuration file");
    auto defaultEngineOpt = app.add_flag("--default-engine", args.useDefaultEngine, "Use default engine from main repo (ignores config file)");
    auto engineConfigOpt = app.add_option("engine_config", positionalEngineConfig, "Engine configuration file") ->required(false);

    // Mutual exclusions
    defaultEngineOpt->excludes(scriptOpt);
    defaultEngineOpt->excludes(engineConfigOpt);
    scriptOpt->excludes(engineConfigOpt);

    bool threadedFlag = false;
    bool silentFlag = false;
    app.add_flag("--play,--play-audio", args.playAudio, "Play audio to speakers in real-time");
    app.add_flag("--interactive", args.interactive, "Enable interactive keyboard control (overrides --load)");
    app.add_flag("--threaded", threadedFlag, "Use threaded circular buffer (cursor-chasing) (sync-pull is default)");
    app.add_flag("--silent", silentFlag, "Run full audio pipeline at zero volume (for testing)");
    app.add_flag("--sine", args.sineMode, "Generate 440Hz sine wave test tone (no engine sim)");

    try {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e) {
        app.exit(e);
        return false;
    }

    args.syncPull = !threadedFlag;
    if (loadArg >= 0.0) args.targetLoad = loadArg / 100.0;
    if (silentFlag) args.silent = args.playAudio = true;
    if (args.interactive) g_interactiveMode.store(true);

    args.engineConfig = args.useDefaultEngine ? "(default engine)" : (scriptPath.empty() ? std::move(positionalEngineConfig) : std::move(scriptPath));

    auto fail = [&](const char* message) {
        std::cerr << message;
        return false;
    };

    if (args.engineConfig.empty() && !args.sineMode) {
        std::cerr << "ERROR: Engine configuration file is required\n       Use --script <path>, --sine, or provide positional argument\n\n";
        printUsage(argv[0]);
        return false;
    }

    if (args.targetRPM < 0 || args.targetRPM > 20000)    return fail("ERROR: RPM must be between 0 and 20000\n");
    if (args.targetLoad < -1.0 || args.targetLoad > 1.0) return fail("ERROR: Load must be between 0 and 100\n");
    if (args.targetRPM > 0 && args.targetLoad < 0)       args.targetLoad = -1.0;  // Auto mode

    return true;
}

// ============================================================================
// Shows the configuration on startup in a banner format
// ============================================================================
void ShowConfigHeader(const SimulationConfig& config, const char* engineAPIVersion = "unknown") {
    // Verify build ID
    if (engineAPIVersion != nullptr) {
        std::cout << "[Bridge: " << engineAPIVersion << "]\n";
    }

    std::cout << "Configuration:\n";
    std::cout << "  Engine: " << (config.configPath.empty() ? "(default)" : config.configPath) << "\n";
    std::cout << "  Output: " << (config.outputWav == nullptr ? "(none - audio not saved)" : config.outputWav) << "\n";
    if (config.interactive) {
        std::cout << "  Duration: (interactive - runs until quit)\n";
    } else {
        std::cout << "  Duration: " << config.duration << " seconds\n";
    }
    if (config.targetRPM > 0) {
        std::cout << "  Target RPM: " << config.targetRPM << "\n";
    }
    if (config.targetLoad >= 0) {
        std::cout << "  Target Load: " << static_cast<int>(config.targetLoad * 100) << "%\n";
    }
    std::cout << "  Interactive: " << (config.interactive ? "Yes" : "No") << "\n";
    std::cout << "  Audio Playback: " << (config.playAudio ? "Yes" : "No") << "\n";
    std::cout << "  Audio Mode: " << (config.syncPull ? "Sync-Pull (default)" : "Threaded (cursor-chasing)") << "\n";
    std::cout << "  Volume: " << config.volume << "\n";
    if (config.volume == 0.0f) {
        std::cout << "  Silent: Yes (zero volume, full audio pipeline)\n";
    }
    if (config.engineConfig) {
        std::cout << "  Sim Freq: \x1b[32m" << config.engineConfig->simulationFrequency << " Hz\x1b[0m\n";
        if (config.engineConfig->targetSynthesizerLatency > 0.0) {
            std::cout << "  Synth Latency: \x1b[32m" << config.engineConfig->targetSynthesizerLatency << "s\x1b[0m\n";
        }
    }
    std::cout << "  Pre-fill Ms: " << config.preFillMs << "\n";
    std::cout << "\n";
}
