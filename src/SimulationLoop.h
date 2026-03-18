// SimulationLoop.h - Simulation loop functions
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#ifndef SIMULATION_LOOP_H
#define SIMULATION_LOOP_H

#include "AudioConfig.h"
#include "engine_sim_bridge.h"
#include "engine_sim_loader.h"

#include <memory>

class AudioPlayer;
class IAudioSource;
class IAudioMode;
class KeyboardInput;

// ============================================================================
// SimulationConfig - Minimal config for simulation (OCP compliance)
// Only contains what runSimulation needs, not CLI args
// ============================================================================

struct SimulationConfig {
    double duration = 3.0;
    bool interactive = false;
    bool playAudio = false;
    bool silent = false;
    bool sineMode = false;  // Added for runUnifiedAudioLoop
    std::unique_ptr<IAudioMode> audioMode;  // Injected - OCP compliance
};

// ============================================================================
// Unified Main Loop - Works for BOTH sine and engine modes
// Uses Strategy pattern for audio mode behavior
// ============================================================================

int runUnifiedAudioLoop(
    EngineSimHandle handle,
    const EngineSimAPI& api,
    IAudioSource& audioSource,
    const SimulationConfig& config,
    AudioPlayer* audioPlayer,
    IAudioMode& audioMode,
    KeyboardInput* keyboardInput);

// ============================================================================
// Engine Loader Result - Wraps loaded engine for dependency injection
// ============================================================================

struct EngineLoaderResult {
    EngineSimHandle handle = nullptr;
    std::string configPath;
    std::string assetBasePath;
    bool success = false;
};

// Load engine script - single function to resolve config path and load (Feedback #3)
EngineLoaderResult loadEngineScript(const CommandLineArgs& args);

// ============================================================================
// Main Simulation Entry Point - UNIFIED for both modes
// Dependencies injected: audioPlayer, audioMode, keyboardInput (Feedback #2, #4)
// ============================================================================

int runSimulation(
    const CommandLineArgs& args,
    EngineSimAPI& engineAPI,
    AudioPlayer* audioPlayer,
    IAudioMode* audioMode,
    KeyboardInput* keyboardInput
);

#endif // SIMULATION_LOOP_H
