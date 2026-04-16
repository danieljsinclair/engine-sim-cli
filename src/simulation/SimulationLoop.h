// SimulationLoop.h - Simulation loop functions
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance
// Phase E: Takes ISimulator* instead of EngineSimHandle/EngineSimAPI&

#ifndef SIMULATION_LOOP_H
#define SIMULATION_LOOP_H

#include "simulation/EngineConfig.h"

class IAudioStrategy;
class ISimulator;

// Forward declarations for injectable interfaces
namespace input { class IInputProvider; }
namespace presentation { class IPresentation; }
class ILogging;
namespace telemetry { class ITelemetryWriter; class ITelemetryReader; }

// ============================================================================
// SimulationConfig - Simulation parameters only (no infrastructure deps)
// ============================================================================

class SimulationConfig {
public:
    SimulationConfig();

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

    telemetry::ITelemetryWriter* telemetryWriter = nullptr;
    telemetry::ITelemetryReader* telemetryReader = nullptr;
};

// ============================================================================
// Main simulation entry point
// Dependencies injected: simulator, strategy, inputProvider, presentation, logger
// Throws std::runtime_error on initialization failure (fail-fast).
// ============================================================================

int runUnifiedAudioLoop(
    ISimulator& simulator,
    const SimulationConfig& config,
    IAudioStrategy& audioStrategy,
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation,
    telemetry::ITelemetryWriter* telemetryWriter,
    telemetry::ITelemetryReader* telemetryReader,
    ILogging* logger);

int runSimulation(
    const SimulationConfig& config,
    ISimulator& simulator,
    IAudioStrategy* audioStrategy,
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation,
    ILogging* logger
);

#endif // SIMULATION_LOOP_H