// CLIMain.cpp - Main entry point implementation
// Uses IAudioBufferFactory directly (no adapter layer)
// Phase E: Creates BridgeSimulator (ISimulator) instead of raw EngineSimAPI

#include "CLIMain.h"

#include "CLIconfig.h"
#include "CliException.h"
#include "ReplayTimeValidator.h"
#include "common/PresetExceptions.h"

#include "strategy/IAudioBuffer.h"
#include "telemetry/ITelemetryProvider.h"
#include "simulation/SimulationLoop.h"
#include "session/ISimulatorSession.h"
#include "simulator/SimulatorFactory.h"
#include "simulator/EngineSimTypes.h"
#include "simulator/ScriptLoadHelpers.h"
#include "io/IInputProvider.h"
#include "input/KeyboardInputProvider.h"
#include "input/KeyboardInput.h"
#include "io/IPresentation.h"
#include "presentation/ConsolePresentation.h"
#include "common/ILogging.h"
#include "config/ANSIColors.h"
#include <Verification.h>

// Bridge headers for connect-demo mode
#include "input/DemoInputProvider.h"
#include "input/DemoThrottleSource.h"
#include "input/IDemoControls.h"
#include "input/EngineInputTarget.h"
#include "input/IDemoSpeedEnhancer.h"
#include "input/GearSelectorInput.h"
#include "input/IgnitionInput.h"
#include "input/IKeyboardInput.h"
#include "input/ReplayTelemetryProvider.h"
#include "input/LiveTelemetryProvider.h"
#include "simulator/BridgeSimulator.h"
#include "twin/IceVehicleProfile.h"
#include "twin/GearboxCsvLogger.h"

#include "engine-sim/include/simulator.h"
#include "engine-sim/include/units.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "config/KqueueSignalStopController.h"
#include "config/ExecutablePath.h"

// ============================================================================
// Signal handling: no handler, no global.
// ============================================================================
// On macOS, SIGINT/SIGTERM are watched by the injected ISignalStopController
// provider (kqueue EVFILT_SIGNAL): the provider blocks the signals and a reader
// thread stops the attached session when one arrives. There is no signal-handler
// function here and no file-scope pointer -- the provider is created via the
// factory below in main() and held as a local. See KqueueSignalStopController.h
// for the Open/Closed provider structure (other platforms add their own class).

// ============================================================================
// Dependency Constructors - Create injectable providers
// ============================================================================

namespace {

constexpr const char* DEFAULT_PRESET_DIR = "engine-sim-bridge/preset/";

// Owning context for input components — target and demoProvider must outlive
// the KeyboardInputProvider (which holds non-owning pointers to them).
// target is always EngineInputTarget; demoProvider is an optional speed enhancer.
struct InputContext {
    std::unique_ptr<input::IKeyActionTarget> target;
    std::unique_ptr<input::IInputProvider> demoProvider;  // demo mode only (speed enhancer)
    std::unique_ptr<input::IInputProvider> provider;
    std::unique_ptr<::KeyboardInput> keyboard;  // owned for replay Q/P
};

// Validate replay time-slicing args against the actual trace duration.
// Throws CliException with a descriptive message if validation fails.
// Extracted to ReplayTimeValidator.{h,cpp} (against IReplayTimeline) so it is
// unit-testable; called from createInputProvider below.

// Attach a CSV gearbox logger to a provider when --gearbox-log is set. The
// logger is a function-local static so it outlives the provider for the run.
// Shared by the live, replay and demo input paths (DRY).
template <typename Provider>
void attachGearboxLogger(Provider& provider, const std::string& logPath) {
    if (logPath.empty()) return;
    static twin::GearboxCsvLogger gearboxLogger(logPath);
    if (gearboxLogger.isOpen()) {
        provider.setGearboxLogger(&gearboxLogger);
        std::cout << "  Gearbox log: " << logPath << std::endl;
    } else {
        std::cerr << "  WARNING: Could not open gearbox log: " << logPath << std::endl;
    }
}

InputContext createInputProvider(const SimulationConfig& config, ILogging* /*logger*/, const CommandLineArgs& args) {
    InputContext ctx;

    // Live telemetry mode: read decoded CSV from stdin (vehicle-sim --stdout-csv
    // piped in), one row at a time. Live and recorded replay share the same stdin
    // CSV contract, so the consumer cannot tell them apart. --start is implicit —
    // the provider fires the starter on frame 0.
    if (args.liveTelemetry) {
        auto live = std::make_unique<input::LiveTelemetryProvider>(
            std::cin, /*autoStart=*/true);
        if (!live->Initialize()) {
            throw CliException("Failed to initialize live telemetry: " + live->GetLastError());
        }
        // Wire --start-from time slicing + the optional gearbox logger.
        live->setStartFromS(args.replay.startFromS);
        attachGearboxLogger(*live, args.gearbox.logPath);
        ctx.provider = std::move(live);
        return ctx;
    }

    // Replay mode: the telemetry CSV is the sole input source (no keyboard).
    // --start is implicit — the provider fires the starter on frame 0.
    if (!args.replay.telemetryPath.empty()) {
        auto replay = std::make_unique<input::ReplayTelemetryProvider>(
            args.replay.telemetryPath, /*autoStart=*/true, /*autoGearbox=*/args.gearbox.automatic);
        if (!replay->Initialize()) {
            throw CliException("Failed to initialize replay telemetry: " + replay->GetLastError());
        }
        // Wire Q/P keyboard for replay mode (same pattern as the keyboard path).
        auto kb = std::make_unique<::KeyboardInput>();
        replay->setKeyboardInput(kb.get());
        // Wire time slicing and validate against trace duration.
        replay->setStartFromS(args.replay.startFromS);
        replay->setEndAtS(args.replay.endAtS);
        validateReplayTimeSlicing(args, replay.get());
        ctx.keyboard = std::move(kb);
        ctx.provider = std::move(replay);
        return ctx;
    }

    // Unified code path: always use EngineInputTarget as the keyboard target
    auto keyboard = std::make_unique<::KeyboardInput>();
    auto target = std::make_unique<input::EngineInputTarget>();
    // Engage the automatic gearbox for --auto or --connect-demo. Both wire the
    // vehicle-twin provider (owns AutomaticGearbox + PRND selector + longitudinal
    // dynamics) so the keyboard can select P/R/N/D and the box auto-shifts.
    // (--auto and --connect-demo currently share one speed source; splitting
    //  that for real-vehicle integration is the deferred interface refactor.)
    target->setGearAutoMode(config.autoGearbox || args.connectDemo);
    // --throttle <0..1>: latch a held throttle so non-interactive runs (--duration)
    // actually drive the engine. Persists via EngineInputTarget's latch.
    if (args.holdThrottle >= 0.0f) {
        target->setThrottle(static_cast<double>(args.holdThrottle));
    }
    // --start: hold the starter switch depressed (S:1) until the engine catches,
    // so the engine cranks and fires non-interactively instead of sitting at 0 RPM.
    // setAutoStart() latches a held starter that the target releases once RPM is up.
    if (args.autoStart) {
        target->setAutoStart();
    }

    // Auto gearbox modes: create the vehicle-twin provider as a speed enhancer
    // and route the shift keys to its PRND selector (P/R/N/D).
    if (args.connectDemo || args.gearbox.automatic) {
        auto throttle = std::make_unique<input::DemoThrottleSource>();
        auto gearSelector = std::make_unique<input::GearSelectorInput>();
        auto ignition = std::make_unique<input::IgnitionInput>();

        auto demoProvider = std::make_unique<input::DemoInputProvider>(
            std::move(throttle),
            std::move(gearSelector),
            std::move(ignition),
            twin::IceVehicleProfile::zf8hp45()
        );

        attachGearboxLogger(*demoProvider, args.gearbox.logPath);

        // Wire demoProvider as speed enhancer to EngineInputTarget
        target->setSpeedEnhancer(demoProvider.get());
        // Route shift keys to the demo provider's PRNDL selector so the keyboard
        // can drive it into DRIVE (P/R/N/D) for the automatic gearbox.
        target->setDemoControls(demoProvider.get());

        // Auto-engage DRIVE so the user can just press throttle and drive.
        input::IDemoControls* demoControls = demoProvider.get();
        demoControls->shiftUp();  // P → R
        demoControls->shiftUp();  // R → N
        demoControls->shiftUp();  // N → D

        if (!demoProvider->Initialize()) {
            throw CliException("Failed to initialize demo input provider");
        }

        ctx.demoProvider = std::move(demoProvider);
    }

    auto provider = std::make_unique<input::KeyboardInputProvider>(
        std::move(keyboard), target.get());

    if (!provider->Initialize()) {
        throw CliException("Failed to initialize keyboard input provider");
    }

    ctx.target = std::move(target);
    ctx.provider = std::move(provider);
    return ctx;
}

std::unique_ptr<presentation::IPresentation> createPresentation(const SimulationConfig& config) {
    presentation::PresentationConfig presConfig;
    // SimulationConfig is the source of truth; PresentationConfig receives copies for display purposes only
    // Note: interactive conceptually belongs to IInputProvider but is surfaced here for presentation
    presConfig.interactive = config.interactive;
    presConfig.duration = config.duration;
    presConfig.diagnostics = config.diagnostics;

    if (auto pres = std::make_unique<presentation::ConsolePresentation>(); pres->Initialize(presConfig)) {
        return pres;
    }
    throw CliException("Failed to initialize presentation");
}

// Fail-fast preflight for a user-named script.
//
// Without this the CLI reports the first missing thing it happens to trip over,
// deep inside the Piranha compiler or the WAV loader, in terms of a path the
// user never typed. Checking up front lets the error name the script the user
// asked for, the asset base derived from it, and the specific missing file.
//
// Two invariants, both fatal (throw CliException -> exit 1):
//   1. the script itself exists and is readable;
//   2. the resolved asset base actually contains sound-library/ — the WAV tree
//      whose impulse responses every exhaust system needs. A tree without it
//      (e.g. es_new/, which has the .mr files but an empty sound-library/)
//      would otherwise run mute or die later with a confusing message.
void verifyScriptRuntimeAssets(const std::string& scriptPath) {
    namespace fs = std::filesystem;

    // error_code overloads throughout: the throwing ones raise filesystem_error
    // on ELOOP/EACCES rather than reporting "absent", which turns a bad symlink
    // into an uncaught exception (SIGABRT) instead of a diagnosable CLI error.
    std::error_code ec;
    if (!fs::exists(scriptPath, ec) || ec) {
        throw CliException("Engine script not found: " + scriptPath
                           + (ec ? " (" + ec.message() + ")" : ""));
    }

    const std::string normalized = ScriptLoadHelpers::normalizeScriptPath(scriptPath);
    const std::string assetBase = ScriptLoadHelpers::resolveAssetBasePath(normalized, "");

    const fs::path soundLibrary = fs::path(assetBase) / "sound-library";
    if (!fs::exists(soundLibrary, ec) || ec) {
        throw CliException(
            "Required audio assets not found: " + soundLibrary.string()
            + "\n  (script: " + scriptPath + ", asset base: " + assetBase + ")"
            + "\n  The asset base must be the directory that directly contains"
              " 'sound-library/'. Run the engine from a tree that has the WAVs"
              " (e.g. es/, not es_new/).");
    }

    // sound-library/ can exist but be empty — that is exactly the es_new/ case.
    // An empty tree yields no impulse responses, i.e. a silent run, so treat it
    // as missing rather than letting the run proceed with no audio.
    // One `!ec` per call, not two: `ec` is reused across both probes, so a second
    // identical check adds nothing (it re-reads the same variable the is_empty
    // call just overwrote). exists() must succeed cleanly before is_empty() is
    // trusted, and is_empty() must itself report no error.
    const fs::path smooth = soundLibrary / "smooth";
    if (fs::exists(smooth, ec) && !ec && fs::is_empty(smooth, ec)) {
        throw CliException(
            "Audio asset directory is empty: " + smooth.string()
            + "\n  (script: " + scriptPath + ", asset base: " + assetBase + ")"
            + "\n  Impulse-response WAVs are required; a run from this tree"
              " would produce no exhaust audio.");
    }
}

std::vector<std::string> resolveConfigPaths(const CommandLineArgs& args, ILogging* logger) {
    const std::string& scriptPath = args.engineConfig;
    constexpr const char* presetDir = DEFAULT_PRESET_DIR;

    // .mr or .json script: run directly, no preset scan. Preflight the runtime
    // files first so a missing script / missing WAV tree is reported here, in
    // the user's own terms, rather than as a late failure or a silent mute run.
    if (scriptPath.size() >= 3) {
        verifyScriptRuntimeAssets(scriptPath);
        return {scriptPath};
    }

    // Resolve the preset directory relative to the running executable so the
    // CLI works when launched from any CWD. Falls back to the PWD-relative
    // presetDir when exe-relative resolution finds nothing.
    const std::string resolvedPresetDir = cli::ExecutablePath::resolveResource(presetDir);

    // No script specified: default to cycling all presets. No current selection
    // is tracked here, so currentFullPath is empty (currentIndex stays at its
    // default 0 — the CLI cycles from the first preset regardless).
    if (auto presetDiscovery = SimulatorFactory::discoverPresetPaths(resolvedPresetDir, /*currentFullPath=*/{}); !presetDiscovery.presets.empty()) {
        std::vector<std::string> paths;
        for (const auto& preset : presetDiscovery.presets) {
            paths.push_back(preset.fullPath);
        }
        logger->info(LogMask::BRIDGE, std::to_string(paths.size()) + " found (P to cycle)");
        return paths;
    }

    // No engine config and no presets found
    throw CliException("No engine presets found at " + resolvedPresetDir + ". Use --script <path> to specify an engine.");
}

}  // anonymous namespace

SimulationConfig CreateSimulationConfig(const CommandLineArgs& args) {
    SimulationConfig config;

    config.configPath = args.engineConfig;
    config.assetBasePath = "";

    // Resolve CLI args (0-sentinel pattern: use named constants from EngineSimDefaults if arg is 0)
    config.interactive = args.interactive != config.interactive ? args.interactive : config.interactive;
    config.playAudio = args.playAudio != config.playAudio ? args.playAudio : config.playAudio;
    // Interactive mode runs until user quits (duration=0). Non-interactive defaults to 3s.
    const double defaultDuration = config.interactive ? 0.0 : config.duration;
    config.duration = args.duration > 0.0 ? args.duration : defaultDuration;
    // Engine master volume: --silent forces 0.0 (full pipeline, muted) and wins
    // over --engine-volume. When --engine-volume is given (>= 0.0), it overrides
    // the bridge default; -1.0 sentinel leaves the default untouched so omitting
    // the flag is behaviour-neutral.
    //
    // The knob is carried by TWO fields that must stay in sync:
    //   * config.volume (outer)      -> drives the hardware speaker gain only
    //   * config.engineConfig.volume (inner, ISimulatorConfig) -> the value
    //     renderOnDemand() actually multiplies into the engine/WAV buffer
    // Both are set here so the flag controls engine loudness on every sink
    // (speaker AND --output WAV), and the banner (which reads the outer field)
    // agrees with what is heard/written.
    if (args.silent) {
        config.volume = 0.0f;
        config.engineConfig.volume = 0.0f;
    } else if (args.engineVolume >= 0.0f) {
        config.volume = args.engineVolume;
        config.engineConfig.volume = args.engineVolume;
    }
    config.syncPull = args.syncPull != config.syncPull ? args.syncPull : config.syncPull;
    config.targetLoad = args.targetLoad != config.targetLoad ? args.targetLoad : config.targetLoad;
    config.preFillMs = (args.audio.preFillMs > 0) ? args.audio.preFillMs : config.preFillMs;

    if (!args.outputWav.empty()) config.outputWav = args.outputWav.c_str();

    // Apply CLI overrides on top of EngineSimDefaults (from ISimulatorConfig inline initializers)
    // simulationFrequency: 0 means "use engine's built-in frequency" (piston engines get it from
    // their script). SineEngine has no built-in frequency, so the factory applies the default.
    // If the user provides an explicit value, use that; otherwise leave as 0 (engine decides).
    if (args.audio.simulationFrequency > 0) {
        config.engineConfig.simulationFrequency = args.audio.simulationFrequency;
    }
    config.engineConfig.targetSynthesizerLatency = (args.audio.synthLatency > 0.0) ? args.audio.synthLatency : config.engineConfig.targetSynthesizerLatency;

    // Gearbox mode: --auto enables automatic gearbox, default is manual
    config.autoGearbox = args.gearbox.automatic;

    // Color the simulator label for CLI output
    std::string name = config.configPath.empty() ? "[DEFAULT]" : config.configPath;
    config.simulatorLabel = ANSIColors::CYAN + name + ANSIColors::RESET;

    // Factory instruction
    config.simulatorType = args.sineMode ? SimulatorType::SineWave : SimulatorType::PistonEngine;

// Forward selective debug categories to the presentation layer
    config.diagnostics = args.diagnostics;

    return config;
}

// Reconfigure gearbox-bearing input providers to match the simulator's actual
// transmission ratios. Localizes the BridgeSimulator/provider casts into one
// cohesive unit (SRP) so the run loop stays flat. Open/Closed note: the cast
// here is the seam — providers expose reconfigureProfile() but it is not yet on
// the shared IInputProvider interface (that lives in engine-sim-bridge). When it
// is promoted there, this helper collapses to a single polymorphic call.
void reconfigureGearboxProviders(ISimulator* simulator, const InputContext& inputCtx) {
    auto* bridgeSim = dynamic_cast<BridgeSimulator*>(simulator);
    if (!bridgeSim) return;

    const auto* rawSim = bridgeSim->getInternalSimulator();
    const auto* trans = rawSim ? rawSim->getTransmission() : nullptr;
    const auto* vehicle = rawSim ? rawSim->getVehicle() : nullptr;
    // Anticipated bad/empty-preset state (no transmission or vehicle, or a
    // preset with no gears) — silently leave the provider's default profile.
    if (!trans || !vehicle || trans->getGearCount() <= 0) return;

    std::vector<double> ratios;
    ratios.reserve(static_cast<size_t>(trans->getGearCount()));
    for (int g = 0; g < trans->getGearCount(); ++g) {
        ratios.push_back(trans->getGearRatio(g));
    }

    // Replay path
    if (auto* replay = dynamic_cast<input::ReplayTelemetryProvider*>(inputCtx.provider.get())) {
        replay->reconfigureProfile(ratios, vehicle->getDiffRatio(), vehicle->getTireRadius());
    }
    // Live --live-telemetry path (CSV stdin drives the twin). The named engine
    // loaded via --script may have different ratios than the twin's default ZF
    // profile, so reconfigure the box to match (e.g. a C63 M156).
    if (auto* live = dynamic_cast<input::LiveTelemetryProvider*>(inputCtx.provider.get())) {
        live->reconfigureProfile(ratios, vehicle->getDiffRatio(), vehicle->getTireRadius());
    }
    // Keyboard --auto path (via DemoInputProvider)
    if (auto* demo = dynamic_cast<input::DemoInputProvider*>(inputCtx.demoProvider.get())) {
        demo->reconfigureProfile(ratios, vehicle->getDiffRatio(), vehicle->getTireRadius());
    }
}

// Roll the per-chamber afterfire counters up into one engine-level view.
// Counters sum (an engine's pop count is the sum of its chambers'); the
// last-event readings take the maximum, which answers "did ANY chamber reach a
// meaningful pop, and how big was it" — the question a smoke run is asking.
struct AfterfireSummary {
    int totalEvents = 0;
    int skippedTooCold = 0;
    int skippedNoFuel = 0;
    int skippedNoOxygen = 0;
    int skippedNotReady = 0;
    int skippedThrottle = 0;
    int misfireCycles = 0;
    double maxIgnitionProgress = 0.0;
    double maxRunnerTempK = 0.0;
    double maxRawFuelFraction = 0.0;
    double minManifoldPressurePa = 0.0;
    double peakPressure = 0.0;
    double energyReleased = 0.0;
    double eventRpm = 0.0;
    double eventRunnerTempK = 0.0;
};

AfterfireSummary summariseAfterfire(const std::vector<AfterfireDiagnostics>& chambers) {
    AfterfireSummary summary;
    for (const auto& chamber : chambers) {
        summary.totalEvents += chamber.eventCount;
        summary.skippedTooCold += chamber.skippedTooCold;
        summary.skippedNoFuel += chamber.skippedNoFuel;
        summary.skippedNoOxygen += chamber.skippedNoOxygen;
        summary.skippedNotReady += chamber.skippedNotReady;
        summary.skippedThrottle += chamber.skippedThrottle;
        summary.misfireCycles += chamber.misfireCycles;
        summary.maxIgnitionProgress = std::max(summary.maxIgnitionProgress, chamber.maxIgnitionProgress);
        summary.maxRunnerTempK = std::max(summary.maxRunnerTempK, chamber.maxRunnerTempK);
        summary.maxRawFuelFraction =
            std::max(summary.maxRawFuelFraction, chamber.maxRawFuelFraction);
        // Minimum across chambers, skipping the sentinel 0 that means "never sampled".
        if (chamber.minManifoldPressurePa > 0.0) {
            summary.minManifoldPressurePa = (summary.minManifoldPressurePa == 0.0)
                ? chamber.minManifoldPressurePa
                : std::min(summary.minManifoldPressurePa, chamber.minManifoldPressurePa);
        }
        summary.peakPressure = std::max(summary.peakPressure, chamber.lastEventPeakPressure);
        summary.energyReleased = std::max(summary.energyReleased, chamber.lastEventEnergyReleased);
        summary.eventRpm = std::max(summary.eventRpm, chamber.lastEventRpm);
        summary.eventRunnerTempK = std::max(summary.eventRunnerTempK, chamber.lastEventRunnerTempK);
    }
    return summary;
}

// Report the afterfire counters for --afterfire-diagnostics. The skipped*
// tallies are the point: when a run produces no pops they name the PHYSICAL
// precondition that was missing (pipe never reached auto-ignition, no unburnt
// fuel present, no oxygen left, or reactive-but-scavenged-too-soon). Read them
// with maxIgnitionProgress: a value near 1.0 means the charge was on the verge
// of lighting and the pipe just needs to hold it a little longer, whereas a
// value near 0 means the mixture was never reactive in the first place. That
// distinction is the difference between tuning a parameter and guessing.
//
// skippedThrottle is reported separately because it is not a physical
// precondition at all: it counts cycles where the pedal never fell below the
// overrun cutoff, so the gate refused before any chemistry was considered. A
// run with events = 0 and a large skippedThrottle means the input never
// commanded a lift — read it BEFORE concluding anything about the physics.
//
// getAfterfireDiagnostics() is a BridgeSimulator member rather than an
// ISimulator one, so the cast is the seam — the same pattern (and the same
// reason) as reconfigureGearboxProviders above. An empty vector means the
// counters are not observable: either ATG_ENGINE_SIM_AFTERFIRE_SPIKE was not
// compiled in (the bridge accessor is a no-op returning {}) or no engine was
// loaded. Both cases are reported as unavailable rather than as "0 events",
// because "0 events" would falsely imply the gates were evaluated and refused.
void printAfterfireDiagnostics(const ISimulator* simulator) {
    // pointer-to-const: reading the counters is the only thing done here, and
    // getAfterfireDiagnostics() is a const member, so nothing needs write access.
    const auto* bridgeSimulator = dynamic_cast<const BridgeSimulator*>(simulator);
    const auto chambers = bridgeSimulator ? bridgeSimulator->getAfterfireDiagnostics()
                                          : std::vector<AfterfireDiagnostics>{};

    if (chambers.empty()) {
        std::cout << "\nAfterfire diagnostics: unavailable"
                  << " (no chamber counters — engine-sim built without"
                     " ATG_ENGINE_SIM_AFTERFIRE_SPIKE, or no engine loaded)"
                  << std::endl;
    } else {
        const AfterfireSummary summary = summariseAfterfire(chambers);
        std::cout << "\nAfterfire diagnostics (" << chambers.size() << " chambers):"
                  << "\n  events             = " << summary.totalEvents
                  << "\n  not ignited: tooCold = " << summary.skippedTooCold
                  << ", noFuel = " << summary.skippedNoFuel
                  << ", noOxygen = " << summary.skippedNoOxygen
                  << ", inductionIncomplete = " << summary.skippedNotReady
                  << "\n  not overrun: throttle not cut = " << summary.skippedThrottle
                  << "\n  misfire cycles (raw fuel into exhaust) = " << summary.misfireCycles
                  << ", min manifold pressure = " << summary.minManifoldPressurePa / 1000.0 << " kPa"
                  << "\n  exhaust runner peaks: T = " << summary.maxRunnerTempK << " K"
                  << ", rawFuelFraction = " << summary.maxRawFuelFraction
                  << ", ignitionProgress = " << summary.maxIgnitionProgress
                  << "\n  last event: peakPressure = " << summary.peakPressure
                  << ", energy = " << summary.energyReleased
                  << ", rpm = " << summary.eventRpm
                  << ", runnerT = " << summary.eventRunnerTempK << " K"
                  << std::endl;
    }
}

// --replay-telemetry with no explicit --duration: run to the end of the trace.
//
// Extracted from main() so the "how long should this run?" decision lives in one
// named place rather than as a nested conditional in the entry point. Only the
// replay provider carries a trace length, so a non-replay input source leaves
// the configured duration untouched.
void applyReplayTraceDuration(SimulationConfig& config, const CommandLineArgs& args,
                              const InputContext& inputCtx) {
    const bool durationUnset = !config.interactive && args.duration <= 0.0;
    const auto* replay = durationUnset
        ? dynamic_cast<const input::ReplayTelemetryProvider*>(inputCtx.provider.get())
        : nullptr;

    if (replay) {
        config.duration = replay->durationS();
    }
}

// Publish the live session to the input providers that need to act on it (Q to
// quit, P to cycle presets). Both casts are the same seam as
// reconfigureGearboxProviders: setSession is a concrete-provider member, not an
// IInputProvider one, so only the provider actually in use is told. Extracted
// from main() because it runs on every preset-cycle iteration and its two casts
// are one responsibility, not two steps of the entry point.
void setSessionOnInputProviders(const InputContext& inputCtx, ISimulatorSession* session) {
    if (auto* kb = dynamic_cast<input::KeyboardInputProvider*>(inputCtx.provider.get())) {
        kb->setSession(session);
    }
    if (auto* replay = dynamic_cast<input::ReplayTelemetryProvider*>(inputCtx.provider.get())) {
        replay->setSession(session);
    }
}

// Print why playback stopped, based on how the session ended. Single exit point.
void reportStopReason(const SimulationConfig& config) {
    if (config.interactive) {
        std::cout << "\nPlayback stopped: user quit (Q or Ctrl-C)." << std::endl;
    } else if (config.duration > 0.0) {
        std::cout << "\nPlayback stopped: " << config.duration << "s duration reached."
                  << "\n  (use --interactive for open-ended, --duration <N> for longer)" << std::endl;
    } else {
        std::cout << "\nPlayback stopped: end of replay trace." << std::endl;
    }
}

// ============================================================================
// Main Entry Point
// ============================================================================


int main(int argc, char* argv[]) {
    int result = 1;

    // Signal-stop controller: the macOS provider (kqueue) blocks SIGINT/SIGTERM
    // and a reader thread stops the attached session when one arrives. Held as a
    // local; its reader thread is joined in its destructor at return. No handler,
    // no file-scope pointer.
    auto stopController = createSignalStopController();

    auto cliLogger = std::make_unique<ConsoleLogger>();
    auto telemetry = std::make_unique<telemetry::InMemoryTelemetry>();

    if (CommandLineArgs args; parseArguments(argc, argv, args)) {
        try {
        SimulationConfig config = CreateSimulationConfig(args);
        ShowConfigHeader(config, ISimulator::getVersion());
        ShowAfterfireHeader(args.afterfire);

        auto inputCtx = createInputProvider(config, cliLogger.get(), args);
        auto* inputProvider = inputCtx.provider.get();
        // --replay-telemetry: when --duration isn't given (and not interactive),
        // default to the trace's full length so each capture just runs to its end.
        applyReplayTraceDuration(config, args, inputCtx);
        auto presentation = createPresentation(config);

        ASSERT(inputProvider || !config.interactive, "Interactive mode requires an input provider");
        ASSERT(presentation, "A presentation provider must be created successfully");

        // Determine paths to run
        auto paths = resolveConfigPaths(args, cliLogger.get());

        // Create audio buffer once (client owns for session lifetime)
        AudioMode audioMode = config.syncPull ? AudioMode::SyncPull : AudioMode::Threaded;
        auto audioBuffer = IAudioBufferFactory::createBuffer(audioMode, cliLogger.get(), telemetry.get());

        // cycle through the available engine presets unless a specific one is configured
        // Each initSimulation() creates a new session, subsequent uses runs hot-swap on the same session
        std::unique_ptr<ISimulatorSession> session;
        result = EXIT_BUT_CONTINUE_NEXT;
        size_t presetIndex = 0;
        while (result == EXIT_BUT_CONTINUE_NEXT) {
            const std::string& currentPath = paths[presetIndex];
            auto simulator = SimulatorFactory::createAndConfigure(config, currentPath, "", cliLogger.get(), telemetry.get());

            // Afterfire: guarded exhaust pops on throttle-cut overrun. Applied
            // directly to the simulator, NOT through SimulationConfig — the trio
            // bridge's SimulationConfig carries no afterfire fields, so this is a
            // post-creation configuration step alongside configureLoadTorque.
            //
            // Inside the preset-cycle loop on purpose: each cycle builds a fresh
            // simulator with fresh chambers, so tuning applied once outside would
            // be silently lost the moment the user pressed P.
            //
            // Unconditional: args.afterfire.enabled is false when the flag is
            // absent, and pushing a disabled config is the explicit "off" state.
            // BridgeSimulator::configureAfterfire warns and no-ops when
            // ATG_ENGINE_SIM_AFTERFIRE_SPIKE was not compiled in.
            //
            // A false return means --afterfire-wav named a file or glob that
            // matched nothing. That is a user error: continuing would run the
            // whole simulation with the engine's default impulse response while
            // appearing to honour the flag, so fail fast with the bad argument
            // named rather than producing silently wrong audio.
            if (!SimulatorFactory::configureAfterfire(simulator.get(), args.afterfire, cliLogger.get())) {
                throw CliException("--afterfire-wav '" + args.afterfire.afterfireWavPath
                                   + "' matched no files");
            }

            // Build SessionDependencies from the available dependencies
            SessionDependencies deps;
            deps.audioBuffer = audioBuffer.get();
            deps.inputProvider = inputProvider;
            deps.presentation = presentation.get();
            deps.telemetryWriter = telemetry.get();
            deps.telemetryReader = telemetry.get();
            deps.logger = cliLogger.get();

            // Match gearbox-bearing providers to the preset's transmission while we
            // still own the simulator (createSession takes it by move below).
            reconfigureGearboxProviders(simulator.get(), inputCtx);

            session = createSession(config, currentPath, std::move(simulator), deps, std::move(session));

            // Expose session to the signal-stop controller and keyboard provider.
            // The controller never dereferences the session from a signal handler;
            // it stores the pointer for its reader thread to call stop() on.
            stopController->attachSession(session.get());
            setSessionOnInputProviders(inputCtx, session.get());

            result = session->run();
            // Detach before the session may be hot-swapped/recreated next loop
            // iteration, so a late stop request can't touch a stale session.
            stopController->detach();
            presetIndex = (presetIndex + 1) % paths.size();
        }//while

        // Tell the user why playback stopped
        reportStopReason(config);

        // No session remains; detach so any stray signal is inert.
        stopController->detach();

        // The loop body always assigns a non-null session (createSession returns
        // a SimulatorSession and the loop runs at least once since result starts
        // as EXIT_BUT_CONTINUE_NEXT). A null session here is a can't-happen
        // invariant violation — fail-fast rather than silently skip close().
        ASSERT(session, "session must exist after the run loop");

        // --afterfire-diagnostics: report the counters accumulated during the run,
        // read from the final session's simulator before it is closed.
        if (args.afterfire.diagnostics) {
            printAfterfireDiagnostics(session->getSimulator());
        }

        session->close();
        }
        // Expected CLI errors: clean exit with the message. Unexpected exceptions
        // are NOT caught here — they propagate to std::terminate (fail-fast) so
        // real bugs surface rather than being swallowed as a generic exit 1.
        catch (const CliException& e) {
            cliLogger->error(LogMask::BRIDGE, std::string(e.what()));
            result = 1;
        }
        catch (const SimulatorException& e) {
            cliLogger->error(LogMask::BRIDGE, std::string(e.what()));
            result = 1;
        }

        // presentation (unique_ptr) destructs here, freeing the provider.
    }
    return result;
}
