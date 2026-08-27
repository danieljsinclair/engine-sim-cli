// CLIMain.cpp - Main entry point implementation
// Uses IAudioBufferFactory directly (no adapter layer)
// Phase E: Creates BridgeSimulator (ISimulator) instead of raw EngineSimAPI

#include "CLIMain.h"

#include "CLIconfig.h"
#include "CliException.h"
#include "ReplayTimeValidator.h"
#include "common/PresetExceptions.h"

#include "strategy/IAudioBuffer.h"
#include "hardware/NullAudioHardwareProvider.h"
#include "telemetry/ITelemetryProvider.h"
#include "simulation/SimulationLoop.h"
#include "session/ISimulatorSession.h"
#include "simulator/SimulatorFactory.h"
#include "simulator/EngineSimTypes.h"
#include "io/IInputProvider.h"
#include "input/KeyboardInputProvider.h"
#include "input/KeyboardInput.h"
#include "input/OverlayInputProvider.h"
#include "io/IPresentation.h"
#include "presentation/ConsolePresentation.h"
#include "presentation/CsvPresentation.h"
#include "presentation/PresentationCollection.h"
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
#include "twin/WheelCoupling.h"
#include "twin/CouplingModelSelector.h"
#include "twin/GearboxCsvLogger.h"

#include "engine-sim/include/simulator.h"
#include "engine-sim/include/units.h"

#include <iostream>
#include <memory>
#include <stdexcept>
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
            std::cin, /*autoStart=*/true, /*liveStream=*/true);
        if (!live->Initialize()) {
            throw CliException("Failed to initialize live telemetry: " + live->GetLastError());
        }
        // Wire --start-from time slicing + the optional gearbox logger.
        live->setStartFromS(args.replay.startFromS);

        // Validate + forward the live clutch wheel-coupling mode. Must be one of
        // the supported strategies; reject anything else rather than silently
        // falling back to FREE (fail-fast on a typo'd mode).
        if (args.wheelCoupling == "free") {
            live->setWheelCouplingMode(twin::WheelCouplingMode::Free);
        } else if (args.wheelCoupling == "pin") {
            live->setWheelCouplingMode(twin::WheelCouplingMode::Pin);
        } else if (args.wheelCoupling == "torque") {
            live->setWheelCouplingMode(twin::WheelCouplingMode::Torque);
        } else {
            throw CliException(
                "--wheel-coupling must be 'free', 'pin' or 'torque', got: " + args.wheelCoupling);
        }

        // Validate + forward the live clutch coupling MODEL (how the clutch
        // pressure is derived). Fail-fast on a typo'd model name (mirrors the
        // --wheel-coupling validation above). torque-converter is the default
        // (the chosen fluid-coupling approach); clutch-map is the smooth governor
        // fallback; legacy is the historical bang-bang relief path (kept for A/B
        // comparison).
        if (args.couplingModel == "clutch-map") {
            live->setCouplingModel(twin::CouplingModelKind::ClutchMap);
        } else if (args.couplingModel == "torque-converter") {
            live->setCouplingModel(twin::CouplingModelKind::TorqueConverter);
        } else if (args.couplingModel == "legacy") {
            live->setCouplingModel(twin::CouplingModelKind::Legacy);
        } else {
            throw CliException(
                "--coupling-model must be 'clutch-map', 'torque-converter' or 'legacy', got: "
                + args.couplingModel);
        }

        attachGearboxLogger(*live, args.gearbox.logPath);
        // Warm-boot the twin to RUNNING + warm cruise basin BEFORE the first real
        // frame (mirrors replay's primeTwinToRunning). Without this the live twin +
        // core start COLD and --live-telemetry --start-from blows massive negative
        // exhaust flow (reversion). Called AFTER the coupling flags above so the
        // twin primes with the chosen coupling (CLI sets them post-Initialize).
        live->warmBootToRunning();

        ctx.provider = std::move(live);
        return ctx;
    }

    // Replay mode: the telemetry CSV is the sole input source (no keyboard).
    // --start is implicit — the provider fires the starter on frame 0.
    if (!args.replay.telemetryPath.empty()) {
        auto replay = std::make_unique<input::ReplayTelemetryProvider>(
            args.replay.telemetryPath, /*autoStart=*/true, /*autoGearbox=*/args.gearbox.automatic);
        // Forward the coupling flags so the replay DRIVE branch exercises the SAME
        // coupling code as the live path. Must be set BEFORE Initialize() so the
        // twin is constructed with them (Initialize() seeds the twin from these).
        // Validate + forward the wheel-coupling strategy (fail-fast on a typo).
        if (args.wheelCoupling == "free") {
            replay->setWheelCouplingMode(twin::WheelCouplingMode::Free);
        } else if (args.wheelCoupling == "pin") {
            replay->setWheelCouplingMode(twin::WheelCouplingMode::Pin);
        } else if (args.wheelCoupling == "torque") {
            replay->setWheelCouplingMode(twin::WheelCouplingMode::Torque);
        } else {
            throw CliException(
                "--wheel-coupling must be 'free', 'pin' or 'torque', got: " + args.wheelCoupling);
        }
        // Validate + forward the coupling MODEL (fail-fast on a typo, mirroring
        // the --wheel-coupling validation above).
        if (args.couplingModel == "clutch-map") {
            replay->setCouplingModel(twin::CouplingModelKind::ClutchMap);
        } else if (args.couplingModel == "torque-converter") {
            replay->setCouplingModel(twin::CouplingModelKind::TorqueConverter);
        } else if (args.couplingModel == "legacy") {
            replay->setCouplingModel(twin::CouplingModelKind::Legacy);
        } else {
            throw CliException(
                "--coupling-model must be 'clutch-map', 'torque-converter' or 'legacy', got: "
                + args.couplingModel);
        }
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

        // Overlay mode: keyboard overlay on replay CSV file input. Replay reads
        // from a file (not stdin), so --interactive is compatible: keyboard
        // overrides throttle/gear/brake on top of the CSV-driven replay. Only
        // when --interactive was EXPLICITLY passed (not defaulted because no
        // --duration).
        if (args.interactiveExplicit) {
            auto target_ov = std::make_unique<input::EngineInputTarget>();
            target_ov->setGearAutoMode(config.autoGearbox || args.connectDemo);
            if (args.holdThrottle >= 0.0f) target_ov->setThrottle(static_cast<double>(args.holdThrottle));
            if (args.autoStart) target_ov->setStarter();
            auto overlay = std::make_unique<input::OverlayInputProvider>(
                std::move(replay), std::move(kb), target_ov.get());
            if (!overlay->Initialize()) {
                throw CliException("Failed to initialize overlay provider: " + overlay->GetLastError());
            }
            ctx.target = std::move(target_ov);
            ctx.provider = std::move(overlay);
            return ctx;
        }
        // Attach the gearbox decision logger when requested, so the oracle
        // (section D: parse per-frame gear/rpm/mph) can validate replay runs.
        attachGearboxLogger(*replay, args.gearbox.logPath);
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
    // --start: one-shot starter pulse so the CrankingController cranks the engine.
    if (args.autoStart) {
        target->setStarter();
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

    // When --csv-out is set the Collection fans every call out to
    // all children; the loop holds one IPresentation.
    auto collection = std::make_unique<presentation::PresentationCollection>();
    collection->add(std::make_unique<presentation::ConsolePresentation>());
    if (!config.csvOutPath.empty()) {
        collection->add(std::make_unique<presentation::CsvPresentation>(config.csvOutPath));
    }

    if (!collection->Initialize(presConfig)) {
        throw CliException("Failed to initialize presentation");
    }
    return collection;
}

std::vector<std::string> resolveConfigPaths(const CommandLineArgs& args, ILogging* logger) {
    const std::string& scriptPath = args.engineConfig;
    constexpr const char* presetDir = DEFAULT_PRESET_DIR;

    // .mr or .json script: run directly, no preset scan
    if (scriptPath.size() >= 3) {
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
    config.interactive = false;
    config.playAudio = args.playAudio;
    // Duration semantics: interactive mode and both telemetry variants
    // (--live-telemetry stdin CSV, --replay-telemetry file CSV) are driven by
    // the CSV input / --end-at, NOT by --duration. So duration defaults to 0
    // (run until CSV ends or user quits) for those modes. Only a bare
    // (non-interactive, non-telemetry) run defaults to the 3s preset.
    const bool telemetryDriven = args.liveTelemetry || !args.replay.telemetryPath.empty();
    const double defaultDuration =
        (config.interactive || telemetryDriven)
            ? 0.0
            : EngineSimDefaults::DEFAULT_DURATION_SECONDS;
    config.duration = args.duration > 0.0 ? args.duration : defaultDuration;
    config.volume = args.silent ? 0.0f : config.volume;
    config.syncPull = args.syncPull != config.syncPull ? args.syncPull : config.syncPull;
    config.deterministic = args.deterministic;
    config.deterministicTickLock = args.deterministic;
    config.targetLoad = args.targetLoad != config.targetLoad ? args.targetLoad : config.targetLoad;
    config.preFillMs = (args.audio.preFillMs > 0) ? args.audio.preFillMs : config.preFillMs;

    if (!args.outputWav.empty()) config.outputWav = args.outputWav.c_str();
    config.csvOutPath = args.csvOut;

    // Apply CLI overrides on top of EngineSimDefaults (from ISimulatorConfig inline initializers)
    // simulationFrequency: 0 means "use engine's built-in frequency" (piston engines get it from
    // their script). SineEngine has no built-in frequency, so the factory applies the default.
    // If the user provides an explicit value, use that; otherwise leave as 0 (engine decides).
    if (args.audio.simulationFrequency > 0) {
        config.engineConfig.simulationFrequency = args.audio.simulationFrequency;
    }
    config.engineConfig.targetSynthesizerLatency = (args.audio.synthLatency > 0.0) ? args.audio.synthLatency : config.engineConfig.targetSynthesizerLatency;

    // Paced-replay mode: the sim is paced to a recording (deterministic replay,
    // or live/replay telemetry whose warm-start prefix steps the full sim on the
    // loop thread) rather than free-running real-time audio. Disable the
    // audio-latency substep governor in the engine-sim core so the per-frame
    // step count is deterministic and the warm-start can't tip into the
    // reversion (negative-exhaust-flow) attractor. Free-running audio mode
    // (interactive/threaded) keeps the governor for latency tracking.
    config.engineConfig.pacedReplay =
        args.deterministic || args.liveTelemetry || !args.replay.telemetryPath.empty();

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

    // The LIVE path (--live-telemetry) builds its twin with a hardcoded
    // zf8hp45 default profile. If the named .mr did NOT supply a transmission +
    // vehicle, that default would silently drive the engine (Bug C3). Fail fast
    // rather than hiding the geometry mismatch — determinism over silent C63.
    if (const auto* live = dynamic_cast<input::LiveTelemetryProvider*>(inputCtx.provider.get())) {
        (void)live;
        if (!trans || !vehicle || trans->getGearCount() <= 0) {
            throw CliException(
                "Live telemetry requested but the loaded script supplies no transmission/"
                "vehicle geometry. The auto-gearbox twin has no ratios to match against. "
                "Add a `vehicle` + `transmission` node (or `import` a shared block such as "
                "tesla_y_performance.mr) to the .mr. Refusing to silently fall back to zf8hp45.");
        }
    }

    // Replay / demo paths may legitimately run on the default profile when no
    // geometry is present (legacy scripts), so leave the provider's default.
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

        auto inputCtx = createInputProvider(config, cliLogger.get(), args);
        auto* inputProvider = inputCtx.provider.get();
        if (!config.interactive && args.duration <= 0.0) {
            if (const auto* replay = dynamic_cast<const input::ReplayTelemetryProvider*>(inputCtx.provider.get())) {
                // --replay-telemetry: default to the trace's full length so each
                // capture just runs to its end.
                config.duration = replay->durationS();
            } else if (const auto* live = dynamic_cast<const input::LiveTelemetryProvider*>(inputCtx.provider.get())) {
                // --live-telemetry: the sim must run FOR AS LONG AS stdin is open
                // and exit cleanly at real EOF (see SimulationLoop.cpp:542 IsConnected
                // exit). A finite default duration (3s) would terminate the run
                // prematurely — the streaming provider owns termination, not the
                // wall-clock. duration=0 means "no time limit"; the loop then ends
                // only when the provider reports !IsConnected(). An explicit
                // --duration still wins (checked above, so we leave it untouched).
                (void)live;
                config.duration = 0.0;
            }
        }
        auto presentation = createPresentation(config);

        ASSERT(inputProvider || !config.interactive, "Interactive mode requires an input provider");
        ASSERT(presentation, "A presentation provider must be created successfully");

        // Determine paths to run
        auto paths = resolveConfigPaths(args, cliLogger.get());

        // Create audio buffer once (client owns for session lifetime)
        AudioMode audioMode;
        if (config.deterministic) {
            audioMode = AudioMode::Deterministic;
        } else if (config.syncPull) {
            audioMode = AudioMode::SyncPull;
        } else {
            audioMode = AudioMode::Threaded;
        }
        auto audioBuffer = IAudioBufferFactory::createBuffer(audioMode, cliLogger.get(), telemetry.get());

        // cycle through the available engine presets unless a specific one is configured
        // Each initSimulation() creates a new session, subsequent uses runs hot-swap on the same session
        std::unique_ptr<ISimulatorSession> session;
        result = EXIT_BUT_CONTINUE_NEXT;
        size_t presetIndex = 0;
        while (result == EXIT_BUT_CONTINUE_NEXT) {
            const std::string& currentPath = paths[presetIndex];
            auto simulator = SimulatorFactory::createAndConfigure(config, currentPath, "", cliLogger.get(), telemetry.get(),
                args.couplingModel == "torque-converter");

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

            // Deterministic mode injects the null hardware provider: no audio
            // callback thread exists at all, so nothing wall-clocked can touch
            // the simulation. Live default keeps the real provider.
            std::unique_ptr<IAudioHardwareProvider> hardwareOverride;
            if (config.deterministic) {
                hardwareOverride = std::make_unique<NullAudioHardwareProvider>();
            }
            session = createSession(config, currentPath, std::move(simulator), deps, std::move(session),
                                    std::move(hardwareOverride));

            // Expose session to the signal-stop controller and keyboard provider.
            // The controller never dereferences the session from a signal handler;
            // it stores the pointer for its reader thread to call stop() on.
            stopController->attachSession(session.get());
            if (auto* kb = dynamic_cast<input::KeyboardInputProvider*>(inputCtx.provider.get())) kb->setSession(session.get());
            if (auto* replay = dynamic_cast<input::ReplayTelemetryProvider*>(inputCtx.provider.get())) replay->setSession(session.get());

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
