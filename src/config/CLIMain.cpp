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
#include "io/IPresentation.h"
#include "presentation/ConsolePresentation.h"
#include "common/ILogging.h"
#include "config/ANSIColors.h"

#include <csignal>
#include <memory>
#include <stdexcept>
#include <vector>

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

constexpr const char* DEFAULT_PRESET_DIR = "engine-sim-bridge/preset/";

input::IInputProvider* createInputProvider(const SimulationConfig& config, ILogging* logger) {
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
    config.duration = args.duration > 0.0 ? args.duration : config.duration;
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

        auto inputProvider = createInputProvider(config, cliLogger.get());
        auto presentation = createPresentation(config);

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
            for (size_t presetIndex = 0; result == EXIT_BUT_CONTINUE_NEXT; presetIndex = (presetIndex + 1) % paths.size()) {
                const std::string& currentPath = paths[presetIndex];
                auto simulator = SimulatorFactory::createAndConfigure(config, currentPath, "", cliLogger.get(), telemetry.get());
                session = initSimulation(config, currentPath, std::move(simulator), audioBuffer.get(), std::move(session), inputProvider, presentation, telemetry.get(), telemetry.get(), cliLogger.get());
                result = session->run();
            }//for
            
            if (session) {
                session->close();
            }
        }
        catch (const std::exception& e) {
            cliLogger->error(LogMask::BRIDGE, "%s", e.what());
            result = 1;
        }

        delete inputProvider;
        delete presentation;
    }
    return result;
}
