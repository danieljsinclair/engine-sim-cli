// CLIMain.cpp - Main entry point implementation
// Uses IAudioBufferFactory directly (no adapter layer)
// Phase E: Creates BridgeSimulator (ISimulator) instead of raw EngineSimAPI

#include "CLIMain.h"

#include "CLIconfig.h"

#include "strategy/IAudioBuffer.h"
#include "telemetry/ITelemetryProvider.h"
#include "simulation/SimulationLoop.h"
#include "simulator/SimulatorFactory.h"
#include "simulator/BridgeSimulator.h"
#include "simulator/EngineSimTypes.h"
#include "io/IInputProvider.h"
#include "input/KeyboardInputProvider.h"
#include "input/KeyboardInput.h"
#include "io/IPresentation.h"
#include "presentation/ConsolePresentation.h"
#include "common/ILogging.h"
#include "config/ANSIColors.h"

// Bridge headers for connect-demo mode
#include "input/DemoInputProvider.h"
#include "input/KeyboardDemoThrottleSource.h"
#include "twin/IceVehicleProfile.h"

#include "engine-sim/include/simulator.h"
#include "engine-sim/include/units.h"

#include <iostream>
#include <csignal>
#include <memory>
#include <stdexcept>

// Adapter for real KeyboardInput to bridge's IKeyboardInput interface
namespace {
class RealKeyboardInputAdapter : public input::IKeyboardInput {
public:
    explicit RealKeyboardInputAdapter(::KeyboardInput& realKeyboard) : realKeyboard_(realKeyboard) {}
    int getKey() override { return realKeyboard_.getKey(); }

private:
    ::KeyboardInput& realKeyboard_;
};
}

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

input::IInputProvider* createInputProvider(const SimulationConfig& config, ILogging* logger, const CommandLineArgs& args) {
    // Connect-demo mode: VirtualICE twin with automatic gearbox
    if (args.connectDemo) {
        static ::KeyboardInput realKeyboard; // Static for termios persistence (global namespace)
        static RealKeyboardInputAdapter adapter(realKeyboard); // Adapter to bridge's interface
        auto throttle = std::make_unique<input::KeyboardDemoThrottleSource>(adapter);
        auto provider = std::make_unique<input::DemoInputProvider>(
            std::move(throttle),
            twin::IceVehicleProfile::zf8hp45()
        );
        if (provider->Initialize()) {
            return provider.release();
        }
        throw std::runtime_error("Failed to initialize demo input provider");
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

// Configure dyno in load torque mode if --load is specified.
// hold=false + rotationSpeed=0 = brake-only (velocity-dependent damping).
// m_maxTorque is the load knob — the engine must work against this torque.
// Returns true if dyno was configured, false otherwise.
bool configureLoadTorque(ISimulator* simulator, double loadFraction) {
    if (loadFraction <= 0) return false;

    auto* bridgeSim = dynamic_cast<BridgeSimulator*>(simulator);
    if (!bridgeSim) return false;

    Simulator* rawSim = bridgeSim->getInternalSimulator();
    if (!rawSim) return false;

    rawSim->m_dyno.m_enabled = true;
    rawSim->m_dyno.m_hold = false;       // Brake-only: resists but doesn't drive
    const double radPerRpm = 3.14159265358979323846 / 30.0;
    rawSim->m_dyno.m_rotationSpeed = 700.0 * radPerRpm; // Idle RPM: no braking below idle
    rawSim->m_dyno.m_maxTorque = units::torque(EngineSimDefaults::DYNO_MAX_TORQUE_FT_LBS, units::ft_lb) * loadFraction;

    std::cout << "  Load: " << static_cast<int>(loadFraction * 100)
              << "% (" << static_cast<int>(loadFraction * EngineSimDefaults::DYNO_MAX_TORQUE_FT_LBS) << " ft*lbs max)" << std::endl;
    return true;
}

} // anonymous namespace

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
    config.useDefaultEngine = args.useDefaultEngine != config.useDefaultEngine ? args.useDefaultEngine : config.useDefaultEngine;
    config.preFillMs = (args.preFillMs > 0) ? args.preFillMs : config.preFillMs;

    if (!args.outputWav.empty()) config.outputWav = args.outputWav.c_str();

    // Apply CLI overrides on top of EngineSimDefaults (from ISimulatorConfig inline initializers)
    config.engineConfig.simulationFrequency = (args.simulationFrequency > 0) ? args.simulationFrequency : config.engineConfig.simulationFrequency;
    config.engineConfig.targetSynthesizerLatency = (args.synthLatency > 0.0) ? args.synthLatency : config.engineConfig.targetSynthesizerLatency;

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

        std::unique_ptr<ISimulator> simulator;
        std::unique_ptr<IAudioBuffer> audioBuffer;
        input::IInputProvider* inputProvider = nullptr;
        presentation::IPresentation* presentation = nullptr;

        try
        {
            simulator = SimulatorFactory::create(
                type, scriptPath, assetBasePath, config.engineConfig,
                cliLogger.get(), telemetry.get());

            // Configure dyno load torque if specified (--load flag)
            configureLoadTorque(simulator.get(), config.targetLoad);

            // Create strategy via factory - pass telemetry so strategies push diagnostics
            AudioMode mode = config.syncPull ? AudioMode::SyncPull : AudioMode::Threaded;
            audioBuffer = IAudioBufferFactory::createBuffer(mode, cliLogger.get(), telemetry.get());
            inputProvider = createInputProvider(config, cliLogger.get(), args);
            presentation = createPresentation(config);

            // Run simulation with ISimulator (holy trinity: ISimulator -> IAudioBuffer -> IAudioHardwareProvider)
            result = runSimulation(config, *simulator, audioBuffer.get(), inputProvider, presentation, telemetry.get(), telemetry.get(), cliLogger.get());
        }
        catch (const std::exception& e) {
            std::cerr << "ERROR: " << e.what() << std::endl;
            result = 1;
        }

        delete inputProvider;
        delete presentation;
    }
    return result;
}
