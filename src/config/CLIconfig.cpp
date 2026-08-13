// CLIConfig.cpp - Audio loop configuration implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#include "CLIconfig.h"
#include "simulation/SimulationLoop.h"
#include "ANSIColors.h"

#include <CLI/CLI.hpp>
#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ============================================================================
// Command Line Parsing
// ============================================================================

void printUsage(const char* progName) {
    std::cout << "Engine Simulator CLI v2.0\n";
    std::cout << "Usage: " << progName << " [options]\n";
    std::cout << "   OR: " << progName << " --script <engine_config.mr|.json> [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --script <path>      Path to engine config (.mr script or .json preset)\n";
    std::cout << "  --load <0-100>       Dyno load torque percentage (engine works against this)\n";
    std::cout << "  --interactive        Enable interactive keyboard control\n";
    std::cout << "  --play, --play-audio Play audio to speakers in real-time\n";
    std::cout << "  --duration <seconds> Duration in seconds (default: 3.0, ignored in interactive)\n";
    std::cout << "  --output <path>      Output WAV file path\n";
    std::cout << "  --connect-demo       Run VirtualICE twin demo (gearbox mode per --auto/--manual)\n";
    std::cout << "  --auto               Use automatic gearbox (default in --connect-demo is manual)\n";
    std::cout << "  --manual             Use manual gearbox (default)\n";
    std::cout << "  --sine               Generate 440Hz sine wave test tone (no engine sim)\n";
    std::cout << "  --threaded           Use threaded circular buffer (cursor-chasing) (sync-pull is default)\n";
    std::cout << "  --silent             Run full audio pipeline at zero volume (for testing)\n";
    std::cout << "  --deterministic      Headless fixed-timestep replay: reproducible per-frame output (gate/diagnosis mode)\n";
    std::cout << "  --cranking-volume    Volume boost during cranking (when ignition ON, RPM < 600, no exhaust flow)\n";
    std::cout << "  --sim-freq <Hz>      Physics Hz (default: " << EngineSimDefaults::SIMULATION_FREQUENCY
              << ", range: " << (EngineSimDefaults::SIMULATION_FREQUENCY / 10) << "-" << (EngineSimDefaults::SIMULATION_FREQUENCY * 10) << ")\n";
    std::cout << "  --synth-latency <s>  Synthesizer latency in seconds (default: " << EngineSimDefaults::TARGET_SYNTH_LATENCY << ")\n";
    std::cout << "  --pre-fill-ms <ms>   Pre-fill buffer ms for sync-pull mode (default: " << EngineSimDefaults::DEFAULT_PREFILL_MS << ")\n";
    std::cout << "  --diagnostic-frames  Show per-frame audio buffer timing line (req=/got=/took=/room=)\n";
    std::cout << "  --diagnostic-freq    Show per-frame update-call frequency line (calls=/need/kfps)\n";
    std::cout << "  --csv-out <file>     Write machine-parseable per-frame CSV (all fields: timecode, rpm,\n";
    std::cout << "                       gas, gear, clutch%, roadImpliedRpm, creepReliefFired, torques, state)\n";
    std::cout << "                       to <file> alongside the console line (for automated smoke-tests)\n\n";
    std::cout << "NOTES:\n";
    std::cout << "  Default: cycles through all .json presets in engine-sim-bridge/preset/\n";
    std::cout << "  --load enables dyno brake mode (physics-driven RPM, not rev limiter)\n";
    std::cout << "  Default mode is sync-pull (synchronous render in audio callback)\n";
    std::cout << "  Use --threaded for cursor-chasing circular buffer mode\n";
    std::cout << "  --sim-freq affects both modes - lower values reduce CPU load\n";
    std::cout << "  --live-telemetry and --interactive are mutually exclusive (CSV stdin vs keyboard)\n\n";
    std::cout << "Interactive Controls:\n";
    std::cout << "  A                      Toggle ignition on/off (starts ON)\n";
    std::cout << "  S                      Toggle starter motor on/off\n";
    std::cout << "  UP/DOWN Arrows or K/J  Increase/decrease throttle\n";
    std::cout << "  W                      Increase throttle\n";
    std::cout << "  SPACE                  Apply brake\n";
    std::cout << "  R                      Reset to idle\n";
    std::cout << "  C                      Increase dyno load torque\n";
    std::cout << "  D                      Decrease dyno load torque\n";
    std::cout << "  E                      Release dyno (free-revving)\n";
    std::cout << "  ] / [                  Shift up / shift down\n";
    std::cout << "  P                      Cycle to next engine preset (in .json preset mode)\n";
    std::cout << "  Q/ESC                  Quit\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << progName << " --interactive --play              # Cycle presets, interactive\n";
    std::cout << "  " << progName << " --script v8_engine.mr --load 50 --interactive --play\n";
    std::cout << "  " << progName << " --sine --interactive --play\n";
    std::cout << "  " << progName << " --load 75 --play                   # Default presets with load\n";
    std::cout << "  " << progName << " --live-telemetry --script C63_M156_V3.mr --silent < recording.csv  # Drive a named engine from CSV\n";
}

// Forward declaration — defined below parseArguments.
bool processArgs(CommandLineArgs& args, const std::string& scriptPath,
                 const std::string& positionalEngineConfig, double loadArg,
                 bool threadedFlag, bool silentFlag);

bool parseArguments(int argc, char* argv[], CommandLineArgs& args) {
    CLI::App app{"Engine Simulator CLI v2.0"};
    app.set_help_flag("-h,--help", "Show help information");
    app.allow_extras(false);

    double loadArg = -1.0;
    std::string scriptPath;
    std::string positionalEngineConfig;

    app.add_option("--load", loadArg, "Dyno load torque percentage (engine works against this)") ->check(CLI::Range(0.0, 100.0));
    app.add_option("--output", args.outputWav, "Output WAV file path");
    app.add_option("--duration", args.duration, "Duration in seconds (default: 3.0, ignored in interactive)");
    app.add_option("--sim-freq", args.audio.simulationFrequency, "Physics Hz (default: " + std::to_string(EngineSimDefaults::SIMULATION_FREQUENCY) + ")") ->check(CLI::Range(EngineSimDefaults::SIMULATION_FREQUENCY / 10, EngineSimDefaults::SIMULATION_FREQUENCY * 10));
    app.add_option("--synth-latency", args.audio.synthLatency, "Synthesizer latency in seconds (default: " + std::to_string(EngineSimDefaults::TARGET_SYNTH_LATENCY) + ")") ->check(CLI::Range(0.001, 0.5));
    app.add_option("--pre-fill-ms", args.audio.preFillMs, "Pre-fill buffer ms for sync-pull mode") ->check(CLI::Range(10, 500));
    app.add_option("--cranking-volume", args.audio.crankingVolume, "Volume boost during cranking (when ignition ON, RPM < 600, no exhaust flow)") ->default_val(1.0f);
    app.add_option("--throttle", args.holdThrottle, "Hold throttle at 0..1 (non-interactive driving / autobox diagnostics)")->check(CLI::Range(0.0, 1.0));
    app.add_flag("--start", args.autoStart, "Auto-crank the engine at startup (implicit with --replay-telemetry)");
    auto replayTelemetryOpt = app.add_option("--replay-telemetry", args.replay.telemetryPath, "Replay a timecoded telemetry CSV (time_s,throttle_pct,road_speed_kmh,gear,clutch_pct) as the input source (implies --start)");

    app.add_option("--start-from", args.replay.startFrom, "Start replay/live-telemetry at this time (seconds, mm:ss, or hh:mm:ss)");
    app.add_option("--end-at", args.replay.endAt, "Stop replay at this time (seconds or mm:ss)")
        ->needs("--replay-telemetry");
    app.add_option("output_wav", args.outputWav, "Output WAV file") ->required(false);

    auto connectDemoOpt = app.add_flag("--connect-demo", args.connectDemo, "Run VirtualICE twin demo with automatic gearbox");
    auto scriptOpt = app.add_option("--script", scriptPath, "Path to engine config (.mr script or .json preset)");
    auto engineConfigOpt = app.add_option("engine_config", positionalEngineConfig, "Engine configuration file") ->required(false);

    auto liveTelemetryOpt = app.add_flag("--live-telemetry", args.liveTelemetry, "Read live telemetry CSV from stdin (vehicle-sim --stdout-csv piped in) as the input source (implies --start)");

    app.add_option("--wheel-coupling", args.wheelCoupling,
        "Live clutch wheel-coupling mode - which wheel speed drives the\n"
        "slip math. Valid options:\n"
        "  pin    - mirrors replay: pins sim vehicle speed to the CSV speed\n"
        "           (DEFAULT; the road-driven path the road-test tunes against)\n"
        "  free   - leaves sim speed independent so the mph-vs-target\n"
        "           diagnostic stays visible\n"
        "  torque - MATCH mode: injects recorded motor_torque_nm at the\n"
        "           transmission input so road speed emerges from the solver")
        ->capture_default_str();

    app.add_option("--coupling-model", args.couplingModel,
        "Live clutch coupling model - how the live twin derives the\n"
        "engine<->drivetrain clutch pressure each frame. Valid options:\n"
        "  torque-converter - fluid-coupling pump/turbine + TR/K curves\n"
        "                     (DEFAULT; the chosen approach)\n"
        "  clutch-map       - declarative smooth governor curve (never opens the\n"
        "                     clutch, so it cannot bang-bang oscillate; fallback)\n"
        "  legacy           - historical slip-lock + binary creep-drag relief\n"
        "                     (the path that oscillated; kept for A/B comparison)")
        ->capture_default_str();

    // Mutual exclusions
    scriptOpt->excludes(engineConfigOpt);
    connectDemoOpt->excludes(scriptOpt);
    connectDemoOpt->excludes(engineConfigOpt);
    // --live-telemetry COMBINES with --script so the user can drive a NAMED
    // engine from CSV stdin (e.g. the C63 V3). Without this, --live-telemetry is
    // locked to preset[0] (the alphabetical first preset): resolveConfigPaths only
    // scans the preset dir when engineConfig is empty, so the named .mr never
    // loads. (The positional engine_config stays excluded — output_wav is the
    // first positional, so a bare positional never reaches engine_config.) Live
    // CSV is still mutually exclusive with the other input sources.
    liveTelemetryOpt->excludes(engineConfigOpt);
    liveTelemetryOpt->excludes(connectDemoOpt);
    liveTelemetryOpt->excludes(replayTelemetryOpt);

    // --live-telemetry and --interactive are mutually exclusive: live telemetry
    // drives the sim from a CSV stream (no keyboard input), while --interactive
    // drives it from the keyboard. The two input sources conflict. Rejected with
    // a clear error at parse time (CLI11's excludes emits a descriptive message).

    bool threadedFlag = false;
    bool silentFlag = false;
    auto playOpt = app.add_flag("--play,--play-audio", args.playAudio, "Play audio to speakers in real-time");
    auto interactiveOpt = app.add_flag("--interactive", args.interactive, "Enable interactive keyboard control");
    // --live-telemetry and --interactive are mutually exclusive (declared here
    // because interactiveOpt is created in this block; the comment above at the
    // liveTelemetryOpt exclusions documents intent). Live CSV stdin vs keyboard
    // input — the two input sources conflict.
    liveTelemetryOpt->excludes(interactiveOpt);
    auto threadedOpt = app.add_flag("--threaded", threadedFlag, "Use threaded circular buffer (cursor-chasing) (sync-pull is default)");
    app.add_flag("--silent", silentFlag, "Run full audio pipeline at zero volume (for testing)");
    auto deterministicOpt = app.add_flag("--deterministic", args.deterministic,
        "Headless fixed-timestep replay: physics advances on the loop thread at the "
        "fixed update interval (no audio callback thread, no wall-clock pacing). "
        "Identical invocations produce identical per-frame output — the reproducible "
        "mode for gate runs and diagnosis. Implies --silent audio behavior.");
    // Headless mode has no audio strategy choice and no speakers.
    deterministicOpt->excludes(threadedOpt);
    deterministicOpt->excludes(playOpt);
    app.add_option("--gearbox-log", args.gearbox.logPath, "Log gearbox decisions to CSV file")->expected(0, 1);
    app.add_flag("--sine", args.sineMode, "Generate 440Hz sine wave test tone (no engine sim)");
    auto autoFlag = app.add_flag("--auto", args.gearbox.automatic, "Use automatic gearbox");
    auto manualFlag = app.add_flag("--manual", args.gearbox.manual, "Use manual gearbox (default)");
    autoFlag->excludes(manualFlag);

    app.add_flag("--diagnostic-frames", args.diagnostics.frames,
                 "Show per-frame audio buffer timing line (req=/got=/took=/room=)");
    app.add_flag("--diagnostic-freq", args.diagnostics.freq,
                 "Show per-frame update-call frequency line (calls=/need/kfps)");

    app.add_option("--csv-out", args.csvOut,
                   "Write machine-parseable per-frame CSV (all fields: timecode, rpm, gas, gear, "
                   "clutch%, roadImplied, relief, torques, state) to <file> alongside the console line");

    try {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e) {
        app.exit(e);
        return false;
    }

    return processArgs(args, scriptPath, positionalEngineConfig, loadArg, threadedFlag, silentFlag);
}

bool processArgs(CommandLineArgs& args, const std::string& scriptPath, const std::string& positionalEngineConfig, double loadArg, bool threadedFlag, bool silentFlag) {
    args.syncPull = !threadedFlag && !args.deterministic;
    if (loadArg >= 0.0) args.targetLoad = loadArg / 100.0;
    if (silentFlag) {
        args.playAudio = true;
        args.silent = true;
    }
    if (args.deterministic) {
        // Headless: zero volume by construction (no audio output exists), and
        // the deterministic strategy replaces the sync-pull/threaded choice.
        args.silent = true;
    }

    // Default to interactive mode unless --duration is given.
    if (args.duration <= 0.0) {
        args.interactive = true;
    }

    // Implicit settings when connectDemo is true
    if (args.connectDemo) {
        args.playAudio = true;
        args.interactive = true;
    }

    // Auto-generate gearbox log filename if flag given without value
    if (args.gearbox.logPath == "true") {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        struct tm tm_local;
        localtime_r(&time, &tm_local);
        std::string buf(64, '\0');
        std::strftime(buf.data(), buf.size(), "gearbox_%Y%m%d_%H%M%S.csv", &tm_local);
        args.gearbox.logPath = buf.c_str();
    }

    args.engineConfig = scriptPath.empty() ? positionalEngineConfig : scriptPath;

    auto fail = [&](const char* message) {
        std::cerr << message;
        return false;
    };

    if (args.targetLoad < -1.0 || args.targetLoad > 1.0) return fail("ERROR: Load must be between 0 and 100\n");

    // Parse time strings (plain seconds or mm:ss or hh:mm:ss) into doubles.
    if (!args.replay.startFrom.empty()) {
        args.replay.startFromS = parseReplayTimeToSeconds(args.replay.startFrom);
        if (args.replay.startFromS < 0.0) {
            std::cerr << "ERROR: Invalid --start-from time: " << args.replay.startFrom << "\n";
            return false;
        }
    }
    if (!args.replay.endAt.empty()) {
        args.replay.endAtS = parseReplayTimeToSeconds(args.replay.endAt);
        if (args.replay.endAtS < 0.0) {
            std::cerr << "ERROR: Invalid --end-at time: " << args.replay.endAt << "\n";
            return false;
        }
    }

    return true;
}

// ============================================================================
// Time string parsing
// ============================================================================
double parseReplayTimeToSeconds(const std::string& s) {
    if (s.empty()) return -1.0;

    // Reject trailing/leading colons (std::getline silently drops empty tokens
    // at the ends, so "01:" would parse as ["01"] — treat as invalid).
    if (s.front() == ':' || s.back() == ':') return -1.0;

    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ':')) {
        parts.push_back(part);
    }

    try {
        if (parts.size() == 1) {
            return std::stod(parts[0]);
        }
        if (parts.size() == 2) {
            return std::stod(parts[0]) * 60.0 + std::stod(parts[1]);
        }
        if (parts.size() == 3) {
            return std::stod(parts[0]) * 3600.0
                 + std::stod(parts[1]) * 60.0
                 + std::stod(parts[2]);
        }
    } catch (const std::invalid_argument&) {
        // std::stod: token isn't a number.
        return -1.0;
    } catch (const std::out_of_range&) {
        // std::stod: token parses but the value is out of double range.
        return -1.0;
    }

    return -1.0;
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
    if (config.targetLoad >= 0) {
        std::cout << "  Dyno Load: " << static_cast<int>(config.targetLoad * 100)
                  << "% (" << static_cast<int>(config.targetLoad * EngineSimDefaults::DYNO_MAX_TORQUE_FT_LBS) << " ft*lbs)\n";
    }
    std::cout << "  Interactive: " << (config.interactive ? "Yes" : "No") << "\n";
    std::cout << "  Audio Playback: " << (config.playAudio ? "Yes" : "No") << "\n";
    std::cout << "  Audio Mode: " << (config.deterministic ? "Deterministic (headless fixed-timestep)"
                              : (config.syncPull ? "Sync-Pull (default)" : "Threaded (cursor-chasing)")) << "\n";
    std::cout << "  Volume: " << config.volume << "\n";
    if (config.volume == 0.0f) {
        std::cout << "  Silent: Yes (zero volume, full audio pipeline)\n";
    }
    std::cout << "  Sim Freq: " << ANSIColors::GREEN << config.engineConfig.simulationFrequency << " Hz" << ANSIColors::RESET << "\n";
    if (config.engineConfig.targetSynthesizerLatency > 0.0) {
        std::cout << "  Synth Latency: " << ANSIColors::GREEN << config.engineConfig.targetSynthesizerLatency << "s" << ANSIColors::RESET << "\n";
    }
    std::cout << "  Pre-fill: " << config.preFillMs << "ms\n";
    std::cout << "  Gearbox: " << (config.autoGearbox ? "Auto" : "Manual") << "\n";
    std::cout << "\n";
}
