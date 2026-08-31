// AudioModeResolver.cpp - extracted from CLIMain.cpp main() (the flattened
// if/else-if/else audio-strategy selection).

#include "config/AudioModeResolver.h"

#include "simulation/SimulationLoop.h"  // SimulationConfig

AudioMode resolveAudioMode(const SimulationConfig& config) {
    if (config.deterministic) {
        return AudioMode::Deterministic;
    }
    if (config.syncPull) {
        return AudioMode::SyncPull;
    }
    return AudioMode::Threaded;
}
