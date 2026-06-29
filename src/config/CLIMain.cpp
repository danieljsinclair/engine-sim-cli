// CLIMain.cpp - Main entry point implementation
// Uses IAudioBufferFactory directly (no adapter layer)
// Phase E: Creates BridgeSimulator (ISimulator) instead of raw EngineSimAPI

#include "CLIMain.h"

#include "CLIconfig.h"

#include "strategy/IAudioBuffer.h"
#include "telemetry/ITelemetryProvider.h"
#include "simulation/SimulationLoop.h"
#include "session/ISimulatorSession.h"
#include "simulator/SimulatorFactory.h"
#include "simulator/EngineSimTypes.h"
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
#include "simulator/BridgeSimulator.h"
#include "twin/IceVehicleProfile.h"
#include "twin/GearboxCsvLogger.h"

#include "engine-sim/include/simulator.h"
#include "engine-sim/include/units.h"

#include <iostream>
#include <csignal>
#include <memory>
#include <stdexcept>
#include <vector>

// ============================================================================
// Signal Handler
// ============================================================================

namespace {
ISimulatorSession* g_sessionForSignal = nullptr;
}

void signalHandler(int signal) {
    (void)signal;
    if (g_sessionForSignal) g_sessionForSignal->stop();
}

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
// Throws std::runtime_error with a descriptive message if validation fails.
void validateReplayTimeSlicing(const CommandLineArgs& args,
                               input::ReplayTelemetryProvider* replay) {
    if (!replay) return;
    const double traceDur = replay->durationS();

    if (args.replayStartFromS >= 0.0 && traceDur > 0.0 && args.replayStartFromS >= traceDur) {
        std::cerr << "ERROR: --start-from " << args.replayStartFromS
                  << "s is past end of trace (" << traceDur << "s)\n";
        throw std::runtime_error("start-from beyond trace duration");
    }
    if (args.replayEndAtS >= 0.0 && traceDur > 0.0 && args.replayEndAtS > traceDur) {
        std::cerr << "WARNING: --end-at " << args.replayEndAtS
                  << "s is past end of trace (" << traceDur
                  << "s); will play to end\n";
        replay->setEndAtS(-1.0);
    }
    if (args.replayStartFromS >= 0.0 && args.replayEndAtS >= 0.0
        && args.replayStartFromS >= args.replayEndAtS) {
        std::cerr << "ERROR: --start-from (" << args.replayStartFromS
                  << "s) must be before --end-at (" << args.replayEndAtS << "s)\n";
        throw std::runtime_error("start-from >= end-at");
    }
}

InputContext createInputProvider(const SimulationConfig& config, ILogging* /*logger*/, const CommandLineArgs& args) {
    InputContext ctx;

    // Replay mode: the telemetry CSV is the sole input source (no keyboard).
    // --start is implicit — the provider fires the starter on frame 0.
    if (!args.replayTelemetryPath.empty()) {
        auto replay = std::make_unique<input::ReplayTelemetryProvider>(
            args.replayTelemetryPath, /*autoStart=*/true, /*autoGearbox=*/args.autoGearbox);
        if (!replay->Initialize()) {
            throw std::runtime_error("Failed to initialize replay telemetry: " + replay->GetLastError());
        }
        // Wire Q/P keyboard for replay mode (same pattern as the keyboard path).
        auto kb = std::make_unique<::KeyboardInput>();
        replay->setKeyboardInput(kb.get());
        // Wire time slicing and validate against trace duration.
        replay->setStartFromS(args.replayStartFromS);
        replay->setEndAtS(args.replayEndAtS);
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
    // --start: one-shot starter pulse so the CrankingController cranks the engine.
    if (args.autoStart) {
        target->setStarter();
    }

    // Auto gearbox modes: create the vehicle-twin provider as a speed enhancer
    // and route the shift keys to its PRND selector (P/R/N/D).
    if (args.connectDemo || args.autoGearbox) {
        auto throttle = std::make_unique<input::DemoThrottleSource>();
        auto gearSelector = std::make_unique<input::GearSelectorInput>();
        auto ignition = std::make_unique<input::IgnitionInput>();

        auto demoProvider = std::make_unique<input::DemoInputProvider>(
            std::move(throttle),
            std::move(gearSelector),
            std::move(ignition),
            twin::IceVehicleProfile::zf8hp45()
        );

        if (!args.gearboxLogPath.empty()) {
            static twin::GearboxCsvLogger gearboxLogger(args.gearboxLogPath);
            if (gearboxLogger.isOpen()) {
                demoProvider->setGearboxLogger(&gearboxLogger);
                std::cout << "  Gearbox log: " << args.gearboxLogPath << std::endl;
            } else {
                std::cerr << "  WARNING: Could not open gearbox log: " << args.gearboxLogPath << std::endl;
            }
        }

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
            throw std::runtime_error("Failed to initialize demo input provider");
        }

        ctx.demoProvider = std::move(demoProvider);
    }

    auto provider = std::make_unique<input::KeyboardInputProvider>(
        std::move(keyboard), target.get());

    if (!provider->Initialize()) {
        throw std::runtime_error("Failed to initialize keyboard input provider");
    }

    ctx.target = std::move(target);
    ctx.provider = std::move(provider);
    return ctx;
}

presentation::IPresentation* createPresentation(const SimulationConfig& config) {
    presentation::PresentationConfig presConfig;
    // SimulationConfig is the source of truth; PresentationConfig receives copies for display purposes only
    // Note: interactive conceptually belongs to IInputProvider but is surfaced here for presentation
    presConfig.interactive = config.interactive;
    presConfig.duration = config.duration;
    presConfig.diagnostics = config.diagnostics;

    if (auto pres = std::make_unique<presentation::ConsolePresentation>(); pres->Initialize(presConfig)) {
        return pres.release();
    }
    throw std::runtime_error("Failed to initialize presentation");
}

std::vector<std::string> resolveConfigPaths(const CommandLineArgs& args, ILogging* logger) {
    const std::string& scriptPath = args.engineConfig;
    constexpr const char* presetDir = DEFAULT_PRESET_DIR;

    // .mr or .json script: run directly, no preset scan
    if (scriptPath.size() >= 3) {
        return {scriptPath};
    }

    // No script specified: default to cycling all presets
    if (auto presetDiscovery = SimulatorFactory::discoverPresetPaths(presetDir); !presetDiscovery.presets.empty()) {
        std::vector<std::string> paths;
        for (const auto& preset : presetDiscovery.presets) {
            paths.push_back(preset.fullPath);
        }
        logger->info(LogMask::BRIDGE, std::to_string(paths.size()) + " found (P to cycle)");
        return paths;
    }

    // No engine config and no presets found
    throw std::runtime_error("No engine presets found at " + std::string(presetDir) + ". Use --script <path> to specify an engine.");
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
    config.volume = args.silent ? 0.0f : config.volume;
    config.syncPull = args.syncPull != config.syncPull ? args.syncPull : config.syncPull;
    config.targetLoad = args.targetLoad != config.targetLoad ? args.targetLoad : config.targetLoad;
    config.preFillMs = (args.preFillMs > 0) ? args.preFillMs : config.preFillMs;

    if (!args.outputWav.empty()) config.outputWav = args.outputWav.c_str();

    // Apply CLI overrides on top of EngineSimDefaults (from ISimulatorConfig inline initializers)
    // simulationFrequency: 0 means "use engine's built-in frequency" (piston engines get it from
    // their script). SineEngine has no built-in frequency, so the factory applies the default.
    // If the user provides an explicit value, use that; otherwise leave as 0 (engine decides).
    if (args.simulationFrequency > 0) {
        config.engineConfig.simulationFrequency = args.simulationFrequency;
    }
    config.engineConfig.targetSynthesizerLatency = (args.synthLatency > 0.0) ? args.synthLatency : config.engineConfig.targetSynthesizerLatency;

    // Gearbox mode: --auto enables automatic gearbox, default is manual
    config.autoGearbox = args.autoGearbox;

    // Color the simulator label for CLI output
    std::string name = config.configPath.empty() ? "[DEFAULT]" : config.configPath;
    config.simulatorLabel = ANSIColors::CYAN + name + ANSIColors::RESET;

    // Factory instruction
    config.simulatorType = args.sineMode ? SimulatorType::SineWave : SimulatorType::PistonEngine;

    // Forward selective debug categories to the presentation layer
    config.diagnostics = args.diagnostics;

    return config;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    int result = 1;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    auto cliLogger = std::make_unique<ConsoleLogger>();
    auto telemetry = std::make_unique<telemetry::InMemoryTelemetry>();

    if (CommandLineArgs args; parseArguments(argc, argv, args)) {
        SimulationConfig config = CreateSimulationConfig(args);
        ShowConfigHeader(config, ISimulator::getVersion());

        auto inputCtx = createInputProvider(config, cliLogger.get(), args);
        auto* inputProvider = inputCtx.provider.get();
        // --replay-telemetry: when --duration isn't given (and not interactive),
        // default to the trace's full length so each capture just runs to its end.
        if (!config.interactive && args.duration <= 0.0) {
            if (const auto* replay = dynamic_cast<const input::ReplayTelemetryProvider*>(inputCtx.provider.get())) {
                config.duration = replay->durationS();
            }
        }
        auto presentation = createPresentation(config);

        ASSERT(inputProvider || !config.interactive, "Interactive mode requires an input provider");
        ASSERT(presentation, "A presentation provider must be created successfully");

        try {
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

                // Build SessionDependencies from the available dependencies
                SessionDependencies deps;
                deps.audioBuffer = audioBuffer.get();
                deps.inputProvider = inputProvider;
                deps.presentation = presentation;
                deps.telemetryWriter = telemetry.get();
                deps.telemetryReader = telemetry.get();
                deps.logger = cliLogger.get();

                session = createSession(config, currentPath, std::move(simulator), deps, std::move(session));

                // Expose session to signal handler and keyboard provider
                g_sessionForSignal = session.get();
                if (auto* kb = dynamic_cast<input::KeyboardInputProvider*>(inputCtx.provider.get())) kb->setSession(session.get());
                if (auto* replay = dynamic_cast<input::ReplayTelemetryProvider*>(inputCtx.provider.get())) replay->setSession(session.get());

                // DRY: reconfigure ANY gearbox-bearing provider to match the ACTUAL engine
                // preset's transmission. Works for both replay (ReplayTelemetryProvider) and
                // keyboard --auto (DemoInputProvider → VirtualIceInputProvider → VirtualIceTwin).
                // The default zf8hp45 has different ratios than a C63 7-speed or GM LS 6-speed.
                if (auto* bridgeSim = dynamic_cast<BridgeSimulator*>(simulator.get())) {
                    const auto* rawSim = bridgeSim->getInternalSimulator();
                    const auto* trans = rawSim ? rawSim->getTransmission() : nullptr;
                    const auto* vehicle = rawSim ? rawSim->getVehicle() : nullptr;
                    if (trans && vehicle && trans->getGearCount() > 0) {
                        std::vector<double> ratios;
                        for (int g = 0; g < trans->getGearCount(); ++g) {
                            ratios.push_back(trans->getGearRatio(g));
                        }
                        // Replay path
                        if (auto* replay = dynamic_cast<input::ReplayTelemetryProvider*>(inputCtx.provider.get())) {
                            replay->reconfigureProfile(ratios, vehicle->getDiffRatio(),
                                                        vehicle->getTireRadius());
                        }
                        // Keyboard --auto path (via DemoInputProvider)
                        if (auto* demo = dynamic_cast<input::DemoInputProvider*>(inputCtx.demoProvider.get())) {
                            demo->reconfigureProfile(ratios, vehicle->getDiffRatio(),
                                                     vehicle->getTireRadius());
                        }
                    }
                }

                result = session->run();
                presetIndex = (presetIndex + 1) % paths.size();
            }//while

            // Tell the user why playback stopped
            if (config.interactive) {
                std::cout << "\nPlayback stopped: user quit (Q or Ctrl-C)." << std::endl;
            } else if (config.duration > 0.0) {
                std::cout << "\nPlayback stopped: " << config.duration << "s duration reached."
                          << "\n  (use --interactive for open-ended, --duration <N> for longer)" << std::endl;
            } else {
                std::cout << "\nPlayback stopped: end of replay trace." << std::endl;
            }

            g_sessionForSignal = nullptr;
            
            if (session) {
                session->close();
            }
        }
        catch (const std::exception& e) {
            cliLogger->error(LogMask::BRIDGE, std::string(e.what()));
            result = 1;
        }

        delete presentation;
    }
    return result;
}
