// SimulationLoop.h - Simulation loop functions
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#ifndef SIMULATION_LOOP_H
#define SIMULATION_LOOP_H

#include "config/CLIconfig.h"
#include "engine_sim_bridge.h"
#include "simulation/EngineConfig.h"
#include "bridge/engine_sim_loader.h"

#include <memory>

class AudioPlayer;
class IAudioSource;
class IAudioMode;

// Forward declarations for injectable interfaces
namespace input { class IInputProvider; }
namespace presentation { class IPresentation; }
class ILogging;
namespace telemetry { class ITelemetryWriter; }

// ============================================================================
// SimulationConfig - Minimal config for simulation (OCP compliance)
// Only contains what runSimulation needs, not CLI args
// ============================================================================

struct SimulationConfig {
    // From CLI args - extracted for DI
    std::string configPath;           // Engine config path
    std::string assetBasePath;        // Asset base path
    double duration = 3.0;
    bool interactive = false;
    bool playAudio = false;
    float volume = 1.0f;
    bool sineMode = false;             // Generate sine wave test tone
    bool syncPull = true;              // Use sync pull audio mode
    double targetRPM = 0.0;
    double targetLoad = -1.0;
    bool useDefaultEngine = false;
    const char* outputWav = nullptr;
    int simulationFrequency = 10000;  // Physics Hz - lower for faster sync-pull
    int preFillMs = 50;  // Pre-fill buffer ms for sync-pull mode

    std::unique_ptr<IAudioMode> audioMode;  // Injected - OCP compliance
    ILogging* logger = nullptr;  // Injected - DI compliance
    telemetry::ITelemetryWriter* telemetryWriter = nullptr;  // Injected - DI compliance
};

// ============================================================================
// Unified Main Loop - Works for BOTH sine and engine modes
// Uses Strategy pattern for audio mode behavior
// Injects IInputProvider and IPresentation for SRP compliance
// ============================================================================

int runUnifiedAudioLoop(
    EngineSimHandle handle,
    const EngineSimAPI& api,
    IAudioSource& audioSource,
    const SimulationConfig& config,
    AudioPlayer* audioPlayer,
    IAudioMode& audioMode,
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation,
    telemetry::ITelemetryWriter* telemetryWriter);

// ============================================================================
// Engine Loader Result - Wraps loaded engine for dependency injection
// ============================================================================

struct EngineLoaderResult {
    EngineSimHandle handle = nullptr;
    std::string configPath;
    std::string assetBasePath;
    bool success = false;
};


// ============================================================================
// Main Simulation Entry Point - UNIFIED for both modes
// Dependencies injected: audioPlayer, audioMode, inputProvider, presentation
// ============================================================================

int runSimulation(
    const SimulationConfig& config,
    EngineSimAPI& engineAPI,
    IAudioMode* audioMode,
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation
);

#endif // SIMULATION_LOOP_H
