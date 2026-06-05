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
#include "input/DemoKeyboardInputProvider.h"
#include "io/IPresentation.h"
#include "presentation/ConsolePresentation.h"
#include "common/ILogging.h"
#include "config/ANSIColors.h"

// Bridge headers for connect-demo mode
#include "input/DemoInputProvider.h"
#include "input/DemoThrottleSource.h"
#include "input/IDemoControls.h"
#include "input/GearSelectorInput.h"
#include "input/IgnitionInput.h"
#include "twin/IceVehicleProfile.h"
#include "twin/GearboxCsvLogger.h"

#include "engine-sim/include/simulator.h"
#include "engine-sim/include/units.h"

#include <iostream>
#include <csignal>
#include <memory>
#include <stdexcept>
#include <vector>
#include <common/Verification.h>

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

input::IInputProvider* createInputProvider(const SimulationConfig& config, ILogging* logger, const CommandLineArgs& args) {
    // Connect-demo mode: VirtualICE twin with automatic gearbox
    if (args.connectDemo) {
        // Create sub-components without keyboard dependency
        auto throttle = std::make_unique<input::DemoThrottleSource>();
        auto gearSelector = std::make_unique<input::GearSelectorInput>();
        auto ignition = std::make_unique<input::IgnitionInput>();

        // Create DemoInputProvider (implements both IInputProvider and IDemoControls)
        auto provider = std::make_unique<input::DemoInputProvider>(
            std::move(throttle),
            std::move(gearSelector),
            std::move(ignition),
            twin::IceVehicleProfile::zf8hp45()
        );

        // Enable gearbox CSV logging if requested
        if (!args.gearboxLogPath.empty()) {
            static twin::GearboxCsvLogger gearboxLogger(args.gearboxLogPath);
            if (gearboxLogger.isOpen()) {
                provider->setGearboxLogger(&gearboxLogger);
                std::cout << "  Gearbox log: " << args.gearboxLogPath << std::endl;
            } else {
                std::cerr << "  WARNING: Could not open gearbox log: " << args.gearboxLogPath << std::endl;
            }
        }

        // Get IDemoControls interface (same object as provider)
        input::IDemoControls* controls = dynamic_cast<input::IDemoControls*>(provider.get());

        // Create CLI keyboard (static for termios persistence)
        auto keyboard = std::make_unique<::KeyboardInput>();

        // Create wrapper that bridges keyboard to IDemoControls
        auto wrapper = std::make_unique<input::DemoKeyboardInputProvider>(
            std::move(keyboard),
            std::move(provider),
            controls
        );

        if (wrapper->Initialize()) {
            return wrapper.release();
        }
        throw std::runtime_error("Failed to initialize demo keyboard input provider");
    }

    // Standard interactive mode
    if (config.interactive) {
        auto provider = std::make_unique<input::KeyboardInputProvider>(logger, config.targetLoad);
        if (provider->Initialize()) {
            return provider.release();
        }
        throw std::runtime_error("Failed to initialize input provider");
    }
    return nullptr;
}

presentation::IPresentation* createPresentation(const SimulationConfig& config) {
    presentation::PresentationConfig presConfig;
    // SimulationConfig is the source of truth; PresentationConfig receives copies for display purposes only
    // Note: interactive conceptually belongs to IInputProvider but is surfaced here for presentation
    presConfig.interactive = config.interactive;
    presConfig.duration = config.duration;

    auto pres = std::make_unique<presentation::ConsolePresentation>();
    if (pres->Initialize(presConfig)) {
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
    auto presetDiscovery = SimulatorFactory::discoverPresetPaths(presetDir);
    if (!presetDiscovery.presets.empty()) {
        std::vector<std::string> paths;
        for (const auto& preset : presetDiscovery.presets) {
            paths.push_back(preset.fullPath);
        }
        logger->info(LogMask::BRIDGE, "Presets: %zu found (P to cycle)", paths.size());
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
    config.duration = args.duration > 0.0 ? args.duration : (config.interactive ? 0.0 : config.duration);
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

    // Color the simulator label for CLI output
    std::string name = config.configPath.empty() ? "[DEFAULT]" : config.configPath;
    config.simulatorLabel = ANSIColors::CYAN + name + ANSIColors::RESET;

    // Factory instruction
    config.simulatorType = args.sineMode ? SimulatorType::SineWave : SimulatorType::PistonEngine;

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

    CommandLineArgs args;
    if (parseArguments(argc, argv, args)) {
        SimulationConfig config = CreateSimulationConfig(args);
        ShowConfigHeader(config, ISimulator::getVersion());

        auto inputProvider = createInputProvider(config, cliLogger.get(), args);
        auto presentation = createPresentation(config);

        ASSERT(inputProvider || !config.interactive, "Interactive mode requires an input provider");
        ASSERT(presentation, "A presentation provider must be created successfully");

        try {
            // Determine paths to run
            auto paths = resolveConfigPaths(args, cliLogger.get());

            // Create audio buffer once (client owns for session lifetime)
            AudioMode audioMode = config.syncPull ? AudioMode::SyncPull : AudioMode::Threaded;
            auto audioBuffer = IAudioBufferFactory::createBuffer(audioMode, cliLogger.get(), telemetry.get());

            // Wire keyboard provider to session for Q/Esc stop signalling
            auto* keyboardProvider = dynamic_cast<input::KeyboardInputProvider*>(inputProvider);

            // cycle through the available engine presets unless a specific one is configured
            // Each initSimulation() creates a new session, subsequent uses runs hot-swap on the same session
            std::unique_ptr<ISimulatorSession> session;
            result = EXIT_BUT_CONTINUE_NEXT;
            for (size_t presetIndex = 0; result == EXIT_BUT_CONTINUE_NEXT; presetIndex = (presetIndex + 1) % paths.size()) {
                const std::string& currentPath = paths[presetIndex];
                auto simulator = SimulatorFactory::createAndConfigure(config, currentPath, "", cliLogger.get(), telemetry.get());
                session = createSession(config, currentPath, std::move(simulator), audioBuffer.get(), std::move(session), inputProvider, presentation, telemetry.get(), telemetry.get(), cliLogger.get());

                // Expose session to signal handler and keyboard provider
                g_sessionForSignal = session.get();
                if (keyboardProvider) keyboardProvider->setSession(session.get());

                result = session->run();
            }//for

            g_sessionForSignal = nullptr;
            
            if (session) {
                session->close();
            }
        }
        catch (const std::exception& e) {
            cliLogger->error(LogMask::BRIDGE, "%s", e.what());
            result = 1;
        }

        if (inputProvider) delete inputProvider;
        delete presentation;
    }
    return result;
}
