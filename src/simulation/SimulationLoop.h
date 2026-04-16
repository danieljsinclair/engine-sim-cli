// SimulationLoop.h - Simulation loop functions
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#ifndef SIMULATION_LOOP_H
#define SIMULATION_LOOP_H

#include "engine_sim_bridge.h"
#include "simulation/EngineConfig.h"
#include "bridge/engine_sim_loader.h"

class AudioPlayer;
class IAudioStrategy;

// Forward declarations for injectable interfaces
namespace input { class IInputProvider; }
namespace presentation { class IPresentation; }
class ILogging;
namespace telemetry { class ITelemetryWriter; class ITelemetryReader; }

// ============================================================================
// SimulationConfig - Minimal config for simulation (OCP compliance)
// ============================================================================

class SimulationConfig {
public:
    explicit SimulationConfig(ILogging* logger = nullptr);

    SimulationConfig(const SimulationConfig&) = delete;
    SimulationConfig& operator=(const SimulationConfig&) = delete;

    SimulationConfig(SimulationConfig&&) = default;
    SimulationConfig& operator=(SimulationConfig&&) = default;

    // Public members
    std::string configPath;
    std::string assetBasePath;
    double duration = 3.0;
    bool interactive = false;
    bool playAudio = false;
    float volume = 1.0f;
    bool sineMode = false;
    bool syncPull = true;
    double targetRPM = 0.0;
    double targetLoad = -1.0;
    bool useDefaultEngine = false;
    const char* outputWav = nullptr;
    int simulationFrequency = 10000;
    int preFillMs = 50;

    ILogging* logger;
    telemetry::ITelemetryWriter* telemetryWriter = nullptr;
    telemetry::ITelemetryReader* telemetryReader = nullptr;

private:
    static ConsoleLogger& defaultLogger();
};

// ============================================================================
// Main simulation entry point
// Dependencies injected: strategy, inputProvider, presentation
// ============================================================================

int runUnifiedAudioLoop(
    EngineSimHandle handle,
    const EngineSimAPI& api,
    const SimulationConfig& config,
    AudioPlayer* audioPlayer,
    IAudioStrategy& audioStrategy,
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation,
    telemetry::ITelemetryWriter* telemetryWriter,
    telemetry::ITelemetryReader* telemetryReader);

struct EngineLoaderResult {
    EngineSimHandle handle = nullptr;
    std::string configPath;
    std::string assetBasePath;
    bool success = false;
};

int runSimulation(
    const SimulationConfig& config,
    EngineSimAPI& engineAPI,
    IAudioStrategy* audioStrategy,
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation
);

#endif // SIMULATION_LOOP_H
