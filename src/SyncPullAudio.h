// SyncPullAudio.h - Sync-pull audio rendering
// Handles synchronous on-demand audio rendering for low latency

#ifndef SYNC_PULL_AUDIO_H
#define SYNC_PULL_AUDIO_H

#include <atomic>
#include <cstdint>

// Include engine types (needed for EngineSimHandle and EngineSimAPI)
#include "engine_sim_bridge.h"
#include "engine_sim_loader.h"

// ============================================================================
// SyncPullAudio - Handles sync-pull mode audio rendering
// Renders audio directly in callback for lowest latency
// ============================================================================

class SyncPullAudio {
public:
    SyncPullAudio();
    ~SyncPullAudio();

    // Initialize with engine handle and API
    bool initialize(EngineSimHandle handle, const EngineSimAPI* api, int sampleRate);

    // Cleanup resources
    void cleanup();

    // Render audio on-demand (called from audio callback)
    // Returns number of frames actually rendered
    int renderOnDemand(float* outputBuffer, int framesToRender);

    // Check if sync-pull is enabled
    bool isEnabled() const { return engineAPI_ != nullptr && engineHandle_ != nullptr; }

    // Get engine handle
    EngineSimHandle getEngineHandle() const { return engineHandle_; }

    // Get engine API
    const EngineSimAPI* getEngineAPI() const { return engineAPI_; }

    // Get sample rate
    int getSampleRate() const { return sampleRate_; }

private:
    EngineSimHandle engineHandle_;
    const EngineSimAPI* engineAPI_;
    int sampleRate_;
};

#endif // SYNC_PULL_AUDIO_H
