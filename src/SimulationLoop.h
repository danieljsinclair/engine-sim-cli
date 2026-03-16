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

// ============================================================================
// Unified Main Loop - Works for BOTH sine and engine modes
// Uses Strategy pattern for audio mode behavior
// ============================================================================

int runUnifiedAudioLoop(
    EngineSimHandle handle,
    const EngineSimAPI& api,
    IAudioSource& audioSource,
    const CommandLineArgs& args,
    AudioPlayer* audioPlayer,
    IAudioMode& audioMode);

// ============================================================================
// Main Simulation Entry Point - UNIFIED for both modes
// ============================================================================

int runSimulation(const CommandLineArgs& args, EngineSimAPI& engineAPI);

#endif // SIMULATION_LOOP_H
