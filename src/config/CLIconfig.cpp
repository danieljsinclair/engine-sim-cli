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
    std::cout << "  --cranking-volume    Volume boost during cranking (when ignition ON, RPM < 600, no exhaust flow)\n";
    std::cout << "  --sim-freq <Hz>      Physics Hz (default: " << EngineSimDefaults::SIMULATION_FREQUENCY
              << ", range: " << (EngineSimDefaults::SIMULATION_FREQUENCY / 10) << "-" << (EngineSimDefaults::SIMULATION_FREQUENCY * 10) << ")\n";
    std::cout << "  --synth-latency <s>  Synthesizer latency in seconds (default: " << EngineSimDefaults::TARGET_SYNTH_LATENCY << ")\n";
    std::cout << "  --pre-fill-ms <ms>   Pre-fill buffer ms for sync-pull mode (default: " << EngineSimDefaults::DEFAULT_PREFILL_MS << ")\n";
    std::cout << "  --diagnostic-frames  Show per-frame audio buffer timing line (req=/got=/took=/room=)\n";
    std::cout << "  --diagnostic-freq    Show per-frame update-call frequency line (calls=/need/kfps)\n";
    // Defaults are read off a default-constructed AfterfireConfig rather than
    // typed in as literals, so --help can never advertise a stale number.
    const AfterfireConfig afterfireDefaults;
    std::cout << "  --enable-afterfire   Enable guarded exhaust afterfire pops on all chambers\n";
    std::cout << "  --afterfire-intensity <0-1>          Pop pressure intensity (default: " << afterfireDefaults.intensity << ")\n";
    std::cout << "  --afterfire-cooldown-ms <ms>         Minimum gap between pops on one chamber (default: " << afterfireDefaults.cooldownMs << ")\n";
    std::cout << "  --afterfire-throttle-cutoff <0-1>    Only pop below this throttle (default: " << afterfireDefaults.throttleCutoff << ")\n";
    std::cout << "  --afterfire-rpm-min <rpm>            Only pop above this RPM (default: " << afterfireDefaults.rpmMin << ")\n";
    std::cout << "  --afterfire-fuel-fraction <0-0.02>   Exhaust fuel fraction per pop (default: " << afterfireDefaults.fuelFraction << ")\n";
    std::cout << "  --afterfire-probability <0-1>        Per-candidate pop probability (default: " << afterfireDefaults.probability << ")\n";
    std::cout << "  --afterfire-decel-window-ms <ms>     Decel detection window (default: " << afterfireDefaults.decelWindowMs << ")\n";
    std::cout << "  --afterfire-max-events-per-decel <n> Max pops per decel event (default: " << afterfireDefaults.maxEventsPerDecel << ")\n";
    std::cout << "  --afterfire-rpm-fall-threshold <rpm> RPM drop per step that counts as overrun (default: " << afterfireDefaults.rpmFallThreshold << ")\n";
    std::cout << "  --afterfire-pop-interval-ms <ms>     Min sim-time between any two pops (default: " << afterfireDefaults.globalPopIntervalMs << ")\n";
    std::cout << "  --afterfire-diagnostics              Print afterfire pop/skip counters at exit\n\n";
    std::cout << "NOTES:\n";
    std::cout << "  Default: cycles through all .json presets in engine-sim-bridge/preset/\n";
    std::cout << "  --load enables dyno brake mode (physics-driven RPM, not rev limiter)\n";
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
    std::cout << "  " << progName << " --live-telemetry --script C63_M156_V2b.mr --silent < recording.csv  # Drive a named engine from CSV\n";
}

// Forward declaration — defined below parseArguments.
bool processArgs(CommandLineArgs& args, const std::string& scriptPath,
                 const std::string& positionalEngineConfig, double loadArg,
                 bool threadedFlag, bool silentFlag);

namespace {

// Register the afterfire flag and its tuning options against the bridge's
// AfterfireConfig. Extracted so parseArguments keeps a single responsibility
// (assemble the parser) rather than also owning one subsystem's tuning surface;
// the options bind directly to the struct that is handed to the factory, so
// there is no CLI-side copy of the tuning values to keep in step.
void addAfterfireOptions(CLI::App& app, AfterfireConfig& afterfire) {
    app.add_flag("--enable-afterfire", afterfire.enabled,
                 "Enable guarded exhaust afterfire pops on all chambers");
    app.add_option("--afterfire-intensity", afterfire.intensity,
                   "Pop pressure intensity (0-1)")->check(CLI::Range(0.0, 1.0));
    app.add_option("--afterfire-cooldown-ms", afterfire.cooldownMs,
                   "Minimum gap between pops on one chamber (ms)")->check(CLI::Range(0.0, 5000.0));
    app.add_option("--afterfire-throttle-cutoff", afterfire.throttleCutoff,
                   "Only pop below this throttle (0-1)")->check(CLI::Range(0.0, 1.0));
    app.add_option("--afterfire-rpm-min", afterfire.rpmMin,
                   "Only pop above this RPM")->check(CLI::Range(0.0, 20000.0));
    app.add_option("--afterfire-fuel-fraction", afterfire.fuelFraction,
                   "Exhaust fuel fraction per pop (0-0.02)")->check(CLI::Range(0.0, 0.02));
    app.add_option("--afterfire-probability", afterfire.probability,
                   "Per-candidate pop probability (0-1)")->check(CLI::Range(0.0, 1.0));
    app.add_option("--afterfire-decel-window-ms", afterfire.decelWindowMs,
                   "Decel detection window (ms)")->check(CLI::Range(0.0, 10000.0));
    app.add_option("--afterfire-max-events-per-decel", afterfire.maxEventsPerDecel,
                   "Max pops per decel event")->check(CLI::Range(0, 20));
    app.add_option("--afterfire-rpm-fall-threshold", afterfire.rpmFallThreshold,
                   "RPM drop per step that counts as overrun")->check(CLI::Range(0.0, 5000.0));
    app.add_option("--afterfire-pop-interval-ms", afterfire.globalPopIntervalMs,
                   "Min sim-time between any two pops (ms)")->check(CLI::Range(0.0, 5000.0));
    app.add_flag("--afterfire-diagnostics", afterfire.diagnostics,
                 "Print afterfire pop/skip counters at exit");
}

}  // namespace

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

    // Mutual exclusions
    scriptOpt->excludes(engineConfigOpt);
    connectDemoOpt->excludes(scriptOpt);
    connectDemoOpt->excludes(engineConfigOpt);
    // --live-telemetry COMBINES with --script so the user can drive a NAMED
    // engine from CSV stdin (e.g. the C63 V2b). Without this, --live-telemetry is
    // locked to preset[0] (the alphabetical first preset): resolveConfigPaths only
    // scans the preset dir when engineConfig is empty, so the named .mr never
    // loads. (The positional engine_config stays excluded — output_wav is the
    // first positional, so a bare positional never reaches engine_config.) Live
    // CSV is still mutually exclusive with the other input sources.
    liveTelemetryOpt->excludes(engineConfigOpt);
    liveTelemetryOpt->excludes(connectDemoOpt);
    liveTelemetryOpt->excludes(replayTelemetryOpt);

    bool threadedFlag = false;
    bool silentFlag = false;
    app.add_flag("--play,--play-audio", args.playAudio, "Play audio to speakers in real-time");
    app.add_flag("--interactive", args.interactive, "Enable interactive keyboard control");
    app.add_flag("--threaded", threadedFlag, "Use threaded circular buffer (cursor-chasing) (sync-pull is default)");
    app.add_flag("--silent", silentFlag, "Run full audio pipeline at zero volume (for testing)");
    app.add_option("--gearbox-log", args.gearbox.logPath, "Log gearbox decisions to CSV file")->expected(0, 1);
    app.add_flag("--sine", args.sineMode, "Generate 440Hz sine wave test tone (no engine sim)");
    auto autoFlag = app.add_flag("--auto", args.gearbox.automatic, "Use automatic gearbox");
    auto manualFlag = app.add_flag("--manual", args.gearbox.manual, "Use manual gearbox (default)");
    autoFlag->excludes(manualFlag);

    app.add_flag("--diagnostic-frames", args.diagnostics.frames,
                 "Show per-frame audio buffer timing line (req=/got=/took=/room=)");
    app.add_flag("--diagnostic-freq", args.diagnostics.freq,
                 "Show per-frame update-call frequency line (calls=/need/kfps)");

    addAfterfireOptions(app, args.afterfire);

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
    args.syncPull = !threadedFlag;
    if (loadArg >= 0.0) args.targetLoad = loadArg / 100.0;
    if (silentFlag) {
        args.playAudio = true;
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
    std::cout << "  Audio Mode: " << (config.syncPull ? "Sync-Pull (default)" : "Threaded (cursor-chasing)") << "\n";
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

// ============================================================================
// Afterfire banner
// ============================================================================
// Separate from ShowConfigHeader because afterfire is NOT part of the bridge's
// SimulationConfig — it is applied to the simulator after creation via
// SimulatorFactory::configureAfterfire(), so the CLI carries it alongside the
// SimulationConfig rather than inside it. Printing it here keeps the CLI's
// console output in one translation unit (SRP) instead of leaking std::cout
// formatting into CLIMain.
void ShowAfterfireHeader(const AfterfireConfig& afterfire) {
    if (afterfire.enabled) {
        std::cout << "  Afterfire: " << ANSIColors::GREEN << "Enabled" << ANSIColors::RESET << "\n";
        std::cout << "    Intensity:            " << afterfire.intensity << "\n";
        std::cout << "    Cooldown:             " << afterfire.cooldownMs << " ms\n";
        std::cout << "    Throttle cutoff:      " << afterfire.throttleCutoff << "\n";
        std::cout << "    RPM min:              " << afterfire.rpmMin << "\n";
        std::cout << "    Fuel fraction:        " << afterfire.fuelFraction << "\n";
        std::cout << "    Probability:          " << afterfire.probability << "\n";
        std::cout << "    Decel window:         " << afterfire.decelWindowMs << " ms\n";
        std::cout << "    Max events per decel: " << afterfire.maxEventsPerDecel << "\n";
        std::cout << "    RPM fall threshold:   " << afterfire.rpmFallThreshold << "\n";
        std::cout << "    Min pop interval:     " << afterfire.globalPopIntervalMs << " ms\n";
        std::cout << "    Diagnostics:          " << (afterfire.diagnostics ? "Yes" : "No") << "\n";
        std::cout << "\n";
    }
}
