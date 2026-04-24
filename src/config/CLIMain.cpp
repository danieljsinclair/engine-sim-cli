// CLIMain.cpp - Main entry point implementation
// Uses IAudioBufferFactory directly (no adapter layer)
// Phase E: Creates BridgeSimulator (ISimulator) instead of raw EngineSimAPI

#include "CLIMain.h"

#include "CLIconfig.h"

#include "strategy/IAudioBuffer.h"
#include "telemetry/ITelemetryProvider.h"
#include "simulation/SimulationLoop.h"
#include "simulator/SimulatorFactory.h"
#include "simulator/EngineSimTypes.h"
#include "io/IInputProvider.h"
#include "input/KeyboardInputProvider.h"
#include "io/IPresentation.h"
#include "presentation/ConsolePresentation.h"
#include "common/ILogging.h"
#include "config/ANSIColors.h"

#include <iostream>
#include <csignal>
#include <memory>
#include <stdexcept>

// ============================================================================
// Signal Handler
// ============================================================================

void signalHandler(int signal) {
    (void)signal;
    g_running.store(false);
}

// ============================================================================
// Dependency Constructors - Create injectable providers
// ============================================================================

namespace {

input::IInputProvider* createInputProvider(const SimulationConfig& config, ILogging* logger) {
    if (config.interactive) {
        auto provider = std::make_unique<input::KeyboardInputProvider>(logger);
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

} // anonymous namespace

SimulationConfig CreateSimulationConfig(const CommandLineArgs& args) {
    SimulationConfig config;

    config.configPath = args.engineConfig;
    config.assetBasePath = "";

    // Resolve CLI args (0-sentinel pattern: use SimulationConfig default if arg is 0)
    config.interactive = args.interactive != config.interactive ? args.interactive : config.interactive;
    config.playAudio = args.playAudio != config.playAudio ? args.playAudio : config.playAudio;
    config.duration = args.duration > 0.0 ? args.duration : config.duration;
    config.volume = args.silent ? 0.0f : config.volume;
    config.syncPull = args.syncPull != config.syncPull ? args.syncPull : config.syncPull;
    config.targetRPM = args.targetRPM != config.targetRPM ? args.targetRPM : config.targetRPM;
    config.targetLoad = args.targetLoad != config.targetLoad ? args.targetLoad : config.targetLoad;
    config.useDefaultEngine = args.useDefaultEngine != config.useDefaultEngine ? args.useDefaultEngine : config.useDefaultEngine;

    config.preFillMs = (args.preFillMs > 0) ? args.preFillMs : config.preFillMs;
    if (!args.outputWav.empty()) config.outputWav = args.outputWav.c_str();

    // Create ISimulatorConfig - single source of truth for audio/simulation constants
    // Default ctor fills from EngineSimDefaults, then apply CLI overrides
    auto* engineConfig = new ISimulatorConfig();
    engineConfig->simulationFrequency = (args.simulationFrequency > 0) ? args.simulationFrequency : engineConfig->simulationFrequency;
    engineConfig->targetSynthesizerLatency = (args.synthLatency > 0.0) ? args.synthLatency : engineConfig->targetSynthesizerLatency;

    // Store the engine config in SimulationConfig (owned pointer)
    config.engineConfig = engineConfig;

    // Color the simulator label for CLI output
    std::string name = config.configPath.empty() ? "[DEFAULT]" : config.configPath;
    config.simulatorLabel = ANSIColors::CYAN + name + ANSIColors::RESET;

    return config;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    int result = 1;
    
    std::cout << "Engine Simulator CLI v2.0\n";
    std::cout << "========================\n\n";

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    CommandLineArgs args;
    if (parseArguments(argc, argv, args)) {
        SimulationConfig config = CreateSimulationConfig(args);
        ShowConfigHeader(config, ISimulator::getVersion());

        // Create shared telemetry (simulator and strategies write, presentation reads)
        auto cliLogger = std::make_unique<ConsoleLogger>();
        auto telemetry = std::make_unique<telemetry::InMemoryTelemetry>();

        // Create simulator via factory (composition root — wires mode-specific details)
        SimulatorType type = args.sineMode ? SimulatorType::SineWave : SimulatorType::PistonEngine;
        std::string scriptPath = args.useDefaultEngine ? "engine-sim-bridge/engine-sim/assets/main.mr" : std::string(args.engineConfig);
        std::string assetBasePath = "";

        std::unique_ptr<ISimulator> simulator = SimulatorFactory::create(
            type, scriptPath, assetBasePath, *config.engineConfig,
            cliLogger.get(), telemetry.get());

        // Create strategy via factory - pass telemetry so strategies push diagnostics
        AudioMode mode = config.syncPull ? AudioMode::SyncPull : AudioMode::Threaded;
        std::unique_ptr<IAudioBuffer> audioBuffer = IAudioBufferFactory::createBuffer(mode, cliLogger.get(), telemetry.get());
        input::IInputProvider* inputProvider = createInputProvider(config, cliLogger.get());
        presentation::IPresentation* presentation = createPresentation(config);

        // Run simulation with ISimulator (holy trinity: ISimulator -> IAudioBuffer -> IAudioHardwareProvider)
        try
        {
            result = runSimulation(config, *simulator, audioBuffer.get(), inputProvider, presentation, telemetry.get(), telemetry.get(), cliLogger.get());
        }
        catch (const std::exception& e) {
            std::cerr << "ERROR: " << e.what() << std::endl;
        }

        delete inputProvider;
        delete presentation;
    }
    return result;
}
