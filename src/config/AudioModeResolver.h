// AudioModeResolver.h - SRP extraction from CLIMain.cpp main() (S3776).
//
// Maps the resolved SimulationConfig onto the audio-buffer strategy. Pinned by
// test/unit/CLIMainS3776S1820Test.cpp Section B (SLIPLOCK_REFACTOR_EXPOSED).

#ifndef CLI_AUDIO_MODE_RESOLVER_H
#define CLI_AUDIO_MODE_RESOLVER_H

#include "strategy/IAudioBuffer.h"

class SimulationConfig;

// Deterministic beats sync-pull: --deterministic implies the headless null
// provider (no audio callback thread exists at all), sync-pull is the default
// streaming strategy, threaded is the opt-in cursor-chasing alternative.
AudioMode resolveAudioMode(const SimulationConfig& config);

#endif  // CLI_AUDIO_MODE_RESOLVER_H
