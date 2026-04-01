// SyncPullAudio.h - Sync-pull audio rendering
// Handles synchronous on-demand audio rendering for low latency

#ifndef SYNC_PULL_AUDIO_H
#define SYNC_PULL_AUDIO_H

#include <atomic>
#include <vector>
#include <cstdint>

// Include engine types (needed for EngineSimHandle and EngineSimAPI)
#include "engine_sim_bridge.h"
#include "bridge/engine_sim_loader.h"

// Forward declarations
struct AudioUnitContext;

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

    // Set context pointer for tracking pre-buffer depletion
    void setContext(AudioUnitContext* context) { context_ = context; }

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
    AudioUnitContext* context_;  // For tracking pre-buffer depletion
    std::vector<float> preBuffer_;  // Pre-buffered audio for crackle prevention
    size_t preBufferReadPos_ = 0;   // Read position in pre-buffer

public:
    // Pre-fill buffer with audio before playback starts
    void preFillBuffer(int targetMs = 100);  // Default 100ms

};

#endif // SYNC_PULL_AUDIO_H
