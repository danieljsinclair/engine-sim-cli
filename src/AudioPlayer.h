// AudioPlayer.h - Audio playback class
// Refactored to use separate CircularBuffer and SyncPullAudio classes

#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include "engine_sim_bridge.h"
#include "engine_sim_loader.h"

// Include the new classes (needed for unique_ptr with complete type)
#include "CircularBuffer.h"
#include "SyncPullAudio.h"

// ============================================================================
// AudioUnit Context - stores engine simulator handle for rendering
// ============================================================================

struct AudioUnitContext {
    EngineSimHandle engineHandle;         // Engine simulator handle
    std::atomic<bool> isPlaying;          // Playback state

    // Cursor-chasing buffer (now uses CircularBuffer class)
    // Uses hardware feedback to maintain 100ms lead, preventing underruns
    std::unique_ptr<CircularBuffer> circularBuffer;  // Managed by CircularBuffer class
    
    std::atomic<int> writePointer;        // Write position in circular buffer
    std::atomic<int> readPointer;         // Read position (hardware playback cursor)
    std::atomic<int> underrunCount;       // Count of buffer underruns
    int bufferStatus;                     // Buffer status for diagnostics (0=normal, 1=warning, 2=critical)

    // Cursor-chasing state
    std::atomic<int64_t> totalFramesRead; // Total frames read by callback (for cursor tracking)
    int sampleRate;                       // Sample rate for calculations

    // Sync pull model: now uses SyncPullAudio class
    std::unique_ptr<SyncPullAudio> syncPullAudio;  // Managed by SyncPullAudio class

    // Silent mode: zero output after processing (mute at callback level)
    bool silent;

    AudioUnitContext() : engineHandle(nullptr), isPlaying(false),
                        writePointer(0), readPointer(0),
                        underrunCount(0), bufferStatus(0),
                        totalFramesRead(0), sampleRate(44100),
                        silent(false) {}
};

// ============================================================================
// AudioPlayer Class
// ============================================================================

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    bool initialize(int sr, bool syncPull = false, EngineSimHandle handle = nullptr, const EngineSimAPI* api = nullptr, bool silent = false);
    void cleanup();

    // Set the engine simulator handle for audio rendering
    void setEngineHandle(EngineSimHandle handle);

    // Check if sync-pull mode is enabled
    bool isSyncPullMode() const;

    // Start playback - begins real-time streaming
    bool start();

    // Stop playback
    void stop();

    // Play buffer (for compatibility with streaming mode)
    bool playBuffer(const float* data, int frames, int sampleRate);

    void waitForCompletion();

    // Add samples to circular buffer with true continuous writes
    void addToCircularBuffer(const float* samples, int frameCount);

    // Expose context for main loop access
    AudioUnitContext* getContext();

    // Get buffer diagnostics for monitoring synchronization issues
    void getBufferDiagnostics(int& writePtr, int& readPtr, int& available, int& status);

    // Reset buffer diagnostics
    void resetBufferDiagnostics();

    // Reset circular buffer to eliminate warmup audio latency
    void resetCircularBuffer();

    // Calculate how many samples to read based on cursor-chasing logic
    int calculateCursorChasingSamples(int defaultFrames);

private:
    AudioUnit audioUnit;
    AudioDeviceID deviceID;
    bool isPlaying;
    int sampleRate;
    AudioUnitContext* context;

    // Static callback for real-time audio rendering
    static OSStatus audioUnitCallback(
        void* refCon,
        AudioUnitRenderActionFlags* actionFlags,
        const AudioTimeStamp* timeStamp,
        UInt32 busNumber,
        UInt32 numberFrames,
        AudioBufferList* ioData
    );
};

#endif // AUDIO_PLAYER_H
