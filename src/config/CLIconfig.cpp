// CLIConfig.cpp - Audio loop configuration implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#include "CLIconfig.h"
#include "simulation/SimulationLoop.h"
#include "ANSIColors.h"

#include <CLI/CLI.hpp>
#include <algorithm>
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

    std::cout << "  --duration <seconds> Run for N seconds; with --replay-telemetry/--live-telemetry:\n"
                 "                       window from the start point (same as --end-at start+N)\n";
    std::cout << "  --output <path>      Output WAV file path\n";
    std::cout << "  --connect-demo       Run VirtualICE twin demo (gearbox mode per --auto/--manual)\n";
    std::cout << "  --auto               Use automatic gearbox (default for --replay-telemetry: a\n"
                 "                       PRND-only CSV cannot shift a manual box)\n";
    std::cout << "  --manual             Use manual gearbox (default except --replay-telemetry)\n";
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
    std::cout << "  --live-telemetry can be combined with --interactive (keyboard overlay on CSV stdin)\n\n";
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
    std::cout << "  " << progName << " --interactive              # Cycle presets, interactive\n";
    std::cout << "  " << progName << " --script v8_engine.mr --load 50 --interactive\n";
    std::cout << "  " << progName << " --sine --interactive\n";
    std::cout << "  " << progName << " --load 75                   # Default presets with load\n";
    std::cout << "  " << progName << " --live-telemetry --script C63_M156_V3.mr --silent < recording.csv  # Drive a named engine from CSV\n";
}

// Forward declaration — defined below parseArguments.
bool processArgs(CommandLineArgs& args, const std::string& scriptPath,
                 const std::string& positionalEngineConfig, double loadArg,
                 bool threadedFlag, bool silentFlag,
                 bool interactiveExplicit = false);

bool parseArguments(int argc, char* argv[], CommandLineArgs& args) {
    CLI::App app{"Engine Simulator CLI v2.0"};
    app.set_help_flag("-h,--help", "Show help information");
    app.allow_extras(false);

    double loadArg = -1.0;
    std::string scriptPath;
    std::string positionalEngineConfig;

    app.add_option("--load", loadArg, "Dyno load torque percentage (engine works against this)") ->check(CLI::Range(0.0, 100.0));
    app.add_option("--output", args.outputWav, "Output WAV file path");
    app.add_option("--duration", args.duration,
                   "Run for N seconds; with --replay-telemetry/--live-telemetry: window from the "
                   "start point (same as --end-at start+N); mutually exclusive with --end-at");
    app.add_option("--sim-freq", args.audio.simulationFrequency, "Physics Hz (default: " + std::to_string(EngineSimDefaults::SIMULATION_FREQUENCY) + ")") ->check(CLI::Range(EngineSimDefaults::SIMULATION_FREQUENCY / 10, EngineSimDefaults::SIMULATION_FREQUENCY * 10));
    app.add_option("--synth-latency", args.audio.synthLatency, "Synthesizer latency in seconds (default: " + std::to_string(EngineSimDefaults::TARGET_SYNTH_LATENCY) + ")") ->check(CLI::Range(0.001, 0.5));
    app.add_option("--pre-fill-ms", args.audio.preFillMs, "Pre-fill buffer ms for sync-pull mode") ->check(CLI::Range(10, 500));
    app.add_option("--cranking-volume", args.audio.crankingVolume, "Volume boost during cranking (when ignition ON, RPM < 600, no exhaust flow)") ->default_val(1.0f);
    app.add_option("--throttle", args.holdThrottle, "Hold throttle at 0..1 (non-interactive driving / autobox diagnostics)")->check(CLI::Range(0.0, 1.0));
    app.add_flag("--start", args.start.autoStart, "Auto-crank the engine at startup (implicit with --replay-telemetry)");
    app.add_option("--starter-delay", args.start.starterDelayMs,
        "Starter-then-ignition delay in ms (McLaren mod: crank BEFORE ignition). "
        "0 = combined start (DEFAULT; starter+ignition together). "
        "A positive value engages the starter, then fires ignition after N ms — "
        "the user can elongate cranking for as long as they want by also holding "
        "the 'S' key. Per-engine .mr starter_torque/speed still apply.")
        ->check(CLI::Range(0, 10000));
    auto replayTelemetryOpt = app.add_option("--replay-telemetry", args.replay.telemetryPath, "Replay a timecoded telemetry CSV (time_s,throttle_pct,road_speed_kmh,gear,clutch_pct) as the input source (implies --start)");

    app.add_option("--start-from", args.replay.startFrom, "Start replay/live-telemetry at this time (seconds, mm:ss, or hh:mm:ss); file replay skips there instantly — rows before the offset are never simulated (arrival state is synthesized at the offset)");
    app.add_option("--end-at", args.replay.endAt, "Stop replay/live-telemetry at this time (seconds or mm:ss); plays to input end if past it");
    app.add_option("output_wav", args.outputWav, "Output WAV file") ->required(false);

    auto connectDemoOpt = app.add_flag("--connect-demo", args.connectDemo, "Run VirtualICE twin demo with automatic gearbox");
    auto scriptOpt = app.add_option("--script", scriptPath, "Path to engine config (.mr script or .json preset)");
    auto engineConfigOpt = app.add_option("engine_config", positionalEngineConfig, "Engine configuration file") ->required(false);

    auto liveTelemetryOpt = app.add_flag("--live-telemetry", args.twin.liveTelemetry, "Read live telemetry CSV from stdin (vehicle-sim --stdout-csv piped in) as the input source (implies --start)");

    app.add_option("--wheel-coupling", args.twin.wheelCoupling,
        "Live clutch wheel-coupling mode - which wheel speed drives the\n"
        "slip math. Valid options:\n"
        "  pin    - mirrors replay: pins sim vehicle speed to the CSV speed\n"
        "           (DEFAULT; the road-driven path the road-test tunes against)\n"
        "  free   - leaves sim speed independent so the mph-vs-target\n"
        "           diagnostic stays visible\n"
        "  torque - MATCH mode: injects recorded motor_torque_nm at the\n"
        "           transmission input so road speed emerges from the solver")
        ->capture_default_str();

    app.add_option("--pin-tau-ms", args.twin.pinTauMs,
        "PIN wheel-coupling compliance in milliseconds. The road speed signal\n"
        "updates only ~5.5 Hz in held steps, so the rigid pin (tau 0, DEFAULT)\n"
        "teleports engine rpm between levels - the audible 'piano keys'.\n"
        "A positive tau makes the pin chase the road-implied speed with a\n"
        "critically-damped response; ~150 ms is the tuned road value. 0 is\n"
        "bit-identical to the rigid pin (the regression contract). Scoped to\n"
        "the pin target only: the gearbox shift map still sees the raw speed.")
        ->capture_default_str();

    app.add_flag("--effective-throttle", args.twin.effectiveThrottle,
        "Derive the twin's ENGINE-DRIVE throttle from the commanded motor\n"
        "torque when Autopilot holds speed with the pedal at rest (pedal 0.00\n"
        "+ positive torque renders the engine silent today). While the pedal\n"
        "sits at/below the 2% foot-off deadband and commanded torque is at or\n"
        "above 20 Nm, effective = max(pedal, torque/600 Nm) - regen contributes\n"
        "zero. DEFAULT OFF; off is bit-identical to today's output. Scoped to\n"
        "the engine drive only (gearbox/coupling/pin keep the raw signal).");

    app.add_flag("--torque-informed-gearbox", args.twin.torqueInformedGearbox,
        "Feed the commanded motor torque (sign + magnitude) into the gearbox\n"
        "shift DECISION as a demand hint: pull -> +torque/600*0.30 bias, AP\n"
        "braking -> +|torque|/1000*0.30 (positive both ways - braking must\n"
        "never read as lift-off coast). Below 20 Nm is true coast (no bias).\n"
        "DEFAULT OFF; off is bit-identical to today's decisions. Decision\n"
        "input only - never physics, never road speed.");

    app.add_option("--coupling-model", args.twin.couplingModel,
        "Live clutch coupling model - how the live twin derives the\n"
        "engine<->drivetrain clutch pressure each frame. Valid options:\n"
        "  torque-converter - fluid-coupling pump/turbine + TR/K curves\n"
        "                     (DEFAULT; the chosen approach)\n"
        "  clutch-map       - declarative smooth governor curve (never opens the\n"
        "                     clutch, so it cannot bang-bang oscillate; fallback)\n"
        "  legacy           - historical slip-lock + binary creep-drag relief\n"
        "                     (the path that oscillated; kept for A/B comparison)")
        ->capture_default_str();

    app.add_option("--span-tame", args.spanTame,
        "Output-stage span taming (0.0=off, 1.0=full). Soft-knee compressor\n"
        "pinned: ratio R(x)=1+5x, makeup gain m(x)=10^(12*(1-1/R)/20),\n"
        "knee [-18,-6] dBFS, safety soft-clip at 0.90/0.95. Off (default)\n"
        "is bit-identical to the legacy audio path.")
        ->check(CLI::Range(0.0, 1.0));

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

    bool threadedFlag = false;
    bool silentFlag = false;
    bool interactiveExplicit = false;
    auto interactiveOpt = app.add_flag("--interactive", interactiveExplicit, "Enable interactive keyboard control (can be combined with --replay-telemetry for keyboard overlay on file replay)");
    // --live-telemetry reads CSV from stdin, so --interactive (keyboard) is
    // incompatible — the keyboard can't read stdin that the CSV is consuming.
    // Keep them mutually exclusive. --replay-telemetry (file-based) CAN combine
    // with --interactive for keyboard overlay (handled in CLIMain).
    liveTelemetryOpt->excludes(interactiveOpt);
    app.add_flag("--silent", silentFlag, "Run full audio pipeline at zero volume (for testing)");
    auto threadedOpt = app.add_flag("--threaded", threadedFlag, "Use threaded circular buffer (cursor-chasing) (sync-pull is default)");
    auto deterministicOpt = app.add_flag("--deterministic", args.deterministic,
        "Headless fixed-timestep replay: physics advances on the loop thread at the "
        "fixed update interval (no audio callback thread, no wall-clock pacing). "
        "Identical invocations produce identical per-frame output — the reproducible "
        "mode for gate runs and diagnosis. Implies --silent audio behavior.");
    // Headless mode has no audio strategy choice and no speakers.
    deterministicOpt->excludes(threadedOpt);
    app.add_option("--gearbox-log", args.gearbox.logPath, "Log gearbox decisions to CSV file")->expected(0, 1);
    app.add_flag("--sine", args.sineMode, "Generate 440Hz sine wave test tone (no engine sim)");
    auto autoFlag = app.add_flag("--auto", args.gearbox.automatic, "Use automatic gearbox (default for --replay-telemetry)");
    auto manualFlag = app.add_flag("--manual", args.gearbox.manual, "Use manual gearbox (default except --replay-telemetry)");
    autoFlag->excludes(manualFlag);

    app.add_flag("--diagnostic-frames", args.diagnostics.frames,
                 "Show per-frame audio buffer timing line (req=/got=/took=/room=)");
    app.add_flag("--diagnostic-freq", args.diagnostics.freq,
                 "Show per-frame update-call frequency line (calls=/need/kfps)");

    app.add_option("--csv-out", args.csvOut,
                   "Write machine-parseable per-frame CSV (all fields: timecode, rpm, gas, gear, "
                   "clutch%, roadImplied, relief, torques, state) to <file> alongside the console line.\n"
                   "                       Without a value, a UTC timestamped roadtest_<timestamp>.csv is generated "
                   "(reruns never overwrite). With a value, the value is used verbatim.")->expected(0, 1);

    try {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e) {
        app.exit(e);
        return false;
    }

    return processArgs(args, scriptPath, positionalEngineConfig, loadArg, threadedFlag, silentFlag, interactiveExplicit);
}

namespace {

// Generate a timestamped filename so reruns never overwrite prior logs:
//   <prefix><YYYYmmdd_HHMMSS><extension>
// e.g. generateTimestampedFilename("gearbox_", ".csv", false) ->
//   "gearbox_20260831_031500.csv".
//
// The timestamp is the current wall-clock time broken down by localtime_r
// (local, useUtc=false) or gmtime_r (UTC, useUtc=true). The road-test CSV logs
// use UTC (captures travel across timezones); gearbox logs use local. Thread-
// safe (no static buffers, no ::localtime). The format is fixed so all
// generated logs sort chronologically by name.
std::string generateTimestampedFilename(const std::string& prefix,
                                         const std::string& extension,
                                         bool useUtc) {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    if (useUtc) {
        gmtime_r(&time, &tm);
    } else {
        localtime_r(&time, &tm);
    }
    // std::string buffer (not a C-style array) — matches the original gearbox
    // log idiom and keeps the generated-logs path free of S5945.
    std::string buf(32, '\0');
    const auto n = std::strftime(buf.data(), buf.size(), "%Y%m%d_%H%M%S", &tm);
    buf.resize(n);
    return prefix + buf + extension;
}

}  // namespace

bool processArgs(CommandLineArgs& args, const std::string& scriptPath, const std::string& positionalEngineConfig, double loadArg, bool threadedFlag, bool silentFlag, bool interactiveExplicit) {
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

    // Default to interactive mode unless --duration is given. This is the
    // "open-ended run" default (no duration = run until quit). It is NOT the
    // same as an explicit --interactive (keyboard overlay): the overlay path in
    // CLIMain only activates when the user passed --interactive on the command
    // line (interactiveExplicit), not when we defaulted here. Without that
    // distinction, every live/replay run without --duration would try to build
    // an overlay provider and fail (double-init on stdin).
    //
    // Telemetry-driven runs (--live-telemetry, --replay-telemetry) are NOT
    // interactive even without --duration: the CSV input / --end-at bounds the
    // run, the keyboard overlay is excluded (mutual exclusion in parseArguments),
    // and the stop-reporter must not claim "user quit" for a trace-driven end.
    // Only a bare (keyboard-driven, non-telemetry, non-deterministic,
    // non-connect-demo) run without --duration defaults to interactive.
    const bool telemetryDriven = args.twin.liveTelemetry || !args.replay.telemetryPath.empty();
    if (args.duration <= 0.0 && !telemetryDriven && !args.deterministic && !args.connectDemo) {
        args.interactive = true;
    }
    args.interactiveExplicit = interactiveExplicit;

    // Replay telemetry defaults the gearbox to AUTO unless the user explicitly
    // opted into manual control (--manual, or --interactive for the keyboard
    // overlay). A replay CSV carries only a PRND selector — there is no +/-
    // gear channel — so a manual replay can never select a gear and sits
    // stationary free-revving ("DM-", 0 mph, engine unloaded). The live path
    // is untouched: LiveTelemetryProvider has no manual gearbox mode to flip.
    // (Owner ruling 2026-09-03: replay must self-drive by default.)
    const bool manualReplayOptOut = args.gearbox.manual || args.interactiveExplicit;
    if (!args.replay.telemetryPath.empty() && !args.gearbox.automatic && !manualReplayOptOut) {
        args.gearbox.automatic = true;
    }

    // Implicit settings when connectDemo is true
    if (args.connectDemo) {
        args.playAudio = true;
        args.interactive = true;
    }

    // Auto-generate gearbox log filename if flag given without value.
    // Local time (the gearbox log is a local diagnostic, not a traveling capture).
    if (args.gearbox.logPath == "true") {
        args.gearbox.logPath = generateTimestampedFilename("gearbox_", ".csv", /*useUtc=*/false);
    }

    // Auto-generate road-test CSV filename if flag given without value.
    // UTC so a log's name is timezone-independent (captures travel across zones
    // and the owner compares logs from different locations). Reruns never
    // overwrite: every invocation gets a fresh timestamp.
    if (args.csvOut == "true") {
        args.csvOut = generateTimestampedFilename("roadtest_", ".csv", /*useUtc=*/true);
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

    // --duration + a telemetry-driven mode is a WINDOW: N seconds from the
    // start point — replay measures from --start-from's arrival on the
    // recording clock, live from attach (both providers run the same
    // elapsed-seconds clock, so one formula covers both). Resolved onto the
    // --end-at path so the provider's single time-slicing mechanism bounds
    // the run; the raw --duration is consumed (reset to 0) so no downstream
    // duration logic double-bounds it. Passing --duration AND --end-at gives
    // two stop conditions — refuse rather than guess precedence.
    // (Supersedes the parse-time fail-fast that rejected every
    // --duration + telemetry combination.)
    if (args.duration > 0.0 && telemetryDriven) {
        if (!args.replay.endAt.empty()) {
            std::cerr << "ERROR: --duration and --end-at are mutually exclusive "
                      << "(both bound the end of the run) — pass one or the other.\n";
            return false;
        }
        const double windowStartS = std::max(0.0, args.replay.startFromS);
        args.replay.endAtS = windowStartS + args.duration;
        args.duration = 0.0;
    }

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
    if (config.targetLoad >= 0) {
        std::cout << "  Dyno Load: " << static_cast<int>(config.targetLoad * 100)
                  << "% (" << static_cast<int>(config.targetLoad * EngineSimDefaults::DYNO_MAX_TORQUE_FT_LBS) << " ft*lbs)\n";
    }
    std::cout << "  Interactive: " << (config.interactive ? "Yes" : "No") << "\n";
    std::cout << "  Audio Playback: " << (config.playAudio ? "Yes" : "No") << "\n";
    const char* audioModeLabel;
    if (config.deterministic) {
        audioModeLabel = "Deterministic (headless fixed-timestep)";
    } else if (config.syncPull) {
        audioModeLabel = "Sync-Pull (default)";
    } else {
        audioModeLabel = "Threaded (cursor-chasing)";
    }
    std::cout << "  Audio Mode: " << audioModeLabel << "\n";
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
