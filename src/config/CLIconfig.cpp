// CLIConfig.cpp - Audio loop configuration implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#include "CLIconfig.h"
#include "config/ExecutablePath.h"
#include "simulation/SimulationLoop.h"
#include "ANSIColors.h"

#include <CLI/CLI.hpp>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// ============================================================================
// Command Line Parsing
// ============================================================================

namespace {

// Accepted --afterfire-pop-overlap values. A CLI::CheckedTransformer over this
// map gives the flag a NAMED domain: the parser rejects anything else with the
// valid words listed, so the numeric enum values never appear on the command
// line. Order matches the enum.
// std::less<> (transparent): lets lookups compare against a string_view or a
// literal without constructing a std::string key for the comparison.
using PopOverlapModeMap = std::map<std::string, AfterfirePopOverlap, std::less<>>;

const PopOverlapModeMap& popOverlapModeNames() {
    static const PopOverlapModeMap names{
        {"suppress", AfterfirePopOverlap::SuppressWhilePlaying},
        {"sum",      AfterfirePopOverlap::SumOnTop},
    };
    return names;
}

// Reverse lookup for display (--help default, run banner). Reads the SAME map the
// parser uses, so the word printed is always a word the parser accepts — there is
// no second table to drift. Falls back to the numeric value if a mode is ever
// added to the enum without a name (visible, rather than silently mislabelled).
std::string popOverlapModeName(AfterfirePopOverlap mode) {
    std::string name = "unknown(" + std::to_string(static_cast<int>(mode)) + ")";

    const auto& names = popOverlapModeNames();
    if (const auto match = std::find_if(names.begin(), names.end(),
            [mode](const auto& entry) { return entry.second == mode; });
        match != names.end()) {
        name = match->first;
    }

    return name;
}

}  // namespace

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
    std::cout << "  --enable-afterfire   Enable exhaust afterfire (auto-ignition of unburnt fuel in hot runners)\n";
    std::cout << "  --afterfire-misfire-map-kpa <kPa>    Manifold pressure below which cycles misfire (default: " << afterfireDefaults.misfireManifoldPressurePa / 1000.0 << ")\n";
    std::cout << "  --afterfire-ignition-delay-s <s>     Induction time at the reference temperature (default: " << afterfireDefaults.ignitionDelayRefS << ")\n";
    std::cout << "  --afterfire-activation-temp-k <K>    Arrhenius activation temperature (default: " << afterfireDefaults.activationTempK << ")\n";
    std::cout << "  --afterfire-ref-temp-k <K>           Reference temperature for the induction time (default: " << afterfireDefaults.refTempK << ")\n";
    std::cout << "  --afterfire-autoignition-temp-k <K>  Auto-ignition temperature floor (default: " << afterfireDefaults.autoIgnitionTempK << ")\n";
    std::cout << "  --afterfire-min-fuel <0-1>           Minimum runner RAW fuel mole fraction (default: " << afterfireDefaults.minRawFuelFraction << ")\n";
    std::cout << "  --afterfire-min-oxygen <0-1>         Minimum runner O2 mole fraction (default: " << afterfireDefaults.minOxygenMoleFraction << ")\n";
    std::cout << "  --afterfire-energy-scale <0-10>      Trim on released energy, 1 = physical (default: " << afterfireDefaults.energyScale << ")\n";
    std::cout << "  --afterfire-wav <path|glob>        Custom afterfire pop WAV file or glob (e.g. es/sound-library/new/*.wav). Default: engine default.\n";
    std::cout << "  --afterfire-pop-overlap <sum|suppress>  Overlapping pop handling on one exhaust channel: sum = layered, suppress = sounding crack finishes and the new pop is dropped (default: "
              << popOverlapModeName(afterfireDefaults.popOverlapMode) << ")\n";
    std::cout << "  --afterfire-min-pop-interval-ms <ms>    Minimum spacing between accepted pops on one channel, 0 disables (default: "
              << afterfireDefaults.minPopIntervalMs << ")\n";
    std::cout << "  --afterfire-diagnostics              Print afterfire event/non-ignition counters at exit\n\n";
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
                 "Enable exhaust afterfire (auto-ignition of unburnt fuel in hot runners)");
    app.add_option("--afterfire-misfire-map-kpa", afterfire.misfireManifoldPressurePa,
                   "Manifold pressure below which cycles misfire (Pa)")->check(CLI::Range(0.0, 500000.0));
    app.add_option("--afterfire-ignition-delay-s", afterfire.ignitionDelayRefS,
                   "Induction time at the reference temperature (s)")->check(CLI::Range(1e-6, 10.0));
    app.add_option("--afterfire-activation-temp-k", afterfire.activationTempK,
                   "Arrhenius activation temperature (K)")->check(CLI::Range(0.0, 100000.0));
    app.add_option("--afterfire-ref-temp-k", afterfire.refTempK,
                   "Reference temperature for the induction time (K)")->check(CLI::Range(1.0, 10000.0));
    app.add_option("--afterfire-autoignition-temp-k", afterfire.autoIgnitionTempK,
                   "Auto-ignition temperature floor (K)")->check(CLI::Range(0.0, 10000.0));
    app.add_option("--afterfire-min-fuel", afterfire.minRawFuelFraction,
                   "Minimum runner raw-fuel mole fraction")->check(CLI::Range(0.0, 1.0));
    app.add_option("--afterfire-min-oxygen", afterfire.minOxygenMoleFraction,
                   "Minimum runner O2 mole fraction")->check(CLI::Range(0.0, 1.0));
    app.add_option("--afterfire-energy-scale", afterfire.energyScale,
                   "Trim on released energy, 1 = physical")->check(CLI::Range(0.0, 10.0));
    app.add_option("--afterfire-gain", afterfire.customGain,
                   "Afterfire MASTER VOLUME (default: 0.6, range 0-10). Scales the pop as a whole: 0 = silent (no physical crackle, no WAV), 1 = full physical crackle + WAV, higher = louder than physical.")->check(CLI::Range(0.0, 10.0));
    app.add_option("--afterfire-wav", afterfire.afterfireWavPath,
                   "Custom afterfire pop WAV file or glob (e.g. es/sound-library/new/*.wav). Default: engine default.");
    // Pop PLAYBACK behaviour (the WAV overlay), not the physics: how a pop that
    // arrives while another is still sounding on the same exhaust channel is
    // admitted. Neither mode ever restarts a sounding pop. Layering is the
    // default; suppression is the opt-out (see AfterfirePopOverlap).
    app.add_option("--afterfire-pop-overlap", afterfire.popOverlapMode,
                   "Overlapping pop handling on one exhaust channel: 'sum' (default, layer them so both play to completion) or 'suppress' (let the sounding crack finish and drop the new pop)")
        ->transform(CLI::CheckedTransformer(popOverlapModeNames(), CLI::ignore_case));
    app.add_option("--afterfire-min-pop-interval-ms", afterfire.minPopIntervalMs,
                   "Minimum spacing in audio ms between ACCEPTED pops on one exhaust channel; 0 disables the floor (default: "
                       + std::to_string(DEFAULT_AFTERFIRE_MIN_POP_INTERVAL_MS) + ")")
        ->check(CLI::Range(0.0, 5000.0));
    app.add_flag("--afterfire-diagnostics", afterfire.diagnostics,
                 "Print afterfire event/non-ignition counters at exit");
}

}  // namespace

// Resolve a --afterfire-wav argument to an install-root-relative location while
// leaving any glob metacharacters in the FILENAME untouched.
//
// ExecutablePath::resolveResource() is a generic resource resolver: it probes
// std::filesystem::exists() on the whole candidate. Handing it "dir/smooth_2*.wav"
// therefore always misses (no file is literally named "smooth_2*.wav"), so it
// falls through to its best-effort install-relative path and the glob never
// reaches a directory that exists. Splitting the argument keeps each component
// doing one job: ExecutablePath locates the DIRECTORY, resolveAfterfireWavPaths
// (bridge side) expands the PATTERN within it.
//
// The leaf is re-appended verbatim, so a literal filename behaves exactly as
// before and a glob is preserved for the bridge to expand.
std::string resolveAfterfireWavArgument(const std::string& rawPath) {
    std::string resolved;

    if (!rawPath.empty()) {
        const std::filesystem::path raw(rawPath);
        const std::string leaf = raw.filename().string();

        if (raw.has_parent_path()) {
            const std::string parent = raw.parent_path().string();
            // Resolve the directory only — it contains no glob characters, so
            // the exists() probe inside resolveResource is meaningful.
            const std::string resolvedDir = cli::ExecutablePath::resolveResource(parent);
            resolved = (std::filesystem::path(resolvedDir) / leaf).string();
        }
        else {
            // A bare filename with no directory component: resolve as-is. A glob
            // here refers to the CWD, which resolveAfterfireWavPaths handles.
            resolved = cli::ExecutablePath::resolveResource(rawPath);
        }
    }

    return resolved;
}

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
    // scriptOpt->excludes(engineConfigOpt);  // Allow both --script and positional engine_config
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
    //
    // --replay-telemetry is the exception: the trace itself bounds the run, so a
    // replay without --duration is a bounded batch job, not an open-ended
    // session. Forcing interactive=true here would suppress the trace-length
    // default in applyReplayTraceDuration (which requires !interactive) and
    // leave the run spinning on the trace's last row forever. An explicit
    // --interactive still wins, because CLI11 has already set the flag by now.
    if (args.duration <= 0.0 && args.replay.telemetryPath.empty()) {
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

    // Resolve the afterfire WAV path relative to the executable's install root
    // (the same base the sound-library WAVs ship under), so --afterfire-wav
    // works regardless of the caller's CWD. The bridge receives an absolute
    // path-or-glob and expands it (glob -> pick one file) at configure time.
    // An empty path is left empty (engine default IR is used).
    if (!args.afterfire.afterfireWavPath.empty()) {
        args.afterfire.afterfireWavPath =
            resolveAfterfireWavArgument(args.afterfire.afterfireWavPath);
    }

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
        std::cout << "    Auto-ignition temp:   " << afterfire.autoIgnitionTempK << " K\n";
        std::cout << "    Induction time:       " << afterfire.ignitionDelayRefS * 1000.0
                  << " ms at " << afterfire.refTempK << " K\n";
        std::cout << "    Activation temp:      " << afterfire.activationTempK << " K\n";
        std::cout << "    Misfire below MAP:    " << afterfire.misfireManifoldPressurePa / 1000.0 << " kPa\n";
        std::cout << "    Min raw fuel frac:    " << afterfire.minRawFuelFraction << "\n";
        std::cout << "    Min oxygen fraction:  " << afterfire.minOxygenMoleFraction << "\n";
        std::cout << "    Energy scale:         " << afterfire.energyScale << "\n";
        std::cout << "    Pop gain:             " << afterfire.customGain
                  << " (mixes pop WAV onto exhaust; 0=off, higher=louder vs engine)\n";
        std::cout << "    Pop overlap:          " << popOverlapModeName(afterfire.popOverlapMode)
                  << " (sum = layered, suppress = sounding crack finishes)\n";
        std::cout << "    Min pop interval:     " << afterfire.minPopIntervalMs << " ms"
                  << (afterfire.minPopIntervalMs > 0.0 ? "\n" : " (floor disabled)\n");
        std::cout << "    Diagnostics:          " << (afterfire.diagnostics ? "Yes" : "No") << "\n";
        std::cout << "\n";
    }
}
