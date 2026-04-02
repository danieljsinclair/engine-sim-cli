// AudioPlayer.h - Audio playback class
// Refactored to use injected IAudioRenderer strategy (SOLID: DI, OCP, SRP)

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
#include "bridge/engine_sim_loader.h"
#include "ILogging.h"

#include "audio/common/CircularBuffer.h"
#include "SyncPullAudio.h"
#include "audio/renderers/IAudioRenderer.h"
#include "audio/renderers/SyncPullRenderer.h"
#include "audio/renderers/ThreadedRenderer.h"

// Forward declarations
class IAudioMode;

// ============================================================================
// AudioUnit Context - stores engine simulator handle for rendering
// DI: IAudioRenderer is injected by AudioPlayer
// ============================================================================

struct AudioUnitContext {
    EngineSimHandle engineHandle;         // Engine simulator handle
    std::atomic<bool> isPlaying;         // Playback state

    // Strategy pattern: injected rendering mode
    // OCP: New renderers added without changing this struct
    IAudioRenderer* audioRenderer;
    
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

    // Sync-pull timing diagnostics (collected in audio callback, read by main loop)
    std::atomic<int> lastReqFrames;       // Last frame count requested by callback
    std::atomic<int> lastGotFrames;       // Last frame count actually rendered
    std::atomic<double> lastRenderMs;     // Last render time in milliseconds
    std::atomic<double> lastHeadroomMs;   // Last headroom (16ms - renderTime) in milliseconds
    std::atomic<double> lastBudgetPct;    // Last time budget percentage used (renderTime / 16ms)
    std::atomic<double> lastFrameBudgetPct; // Last frame budget percentage (framesInWindow / framesNeeded)
    std::atomic<double> lastBufferTrendPct; // Buffer trend: positive = filling, negative = depleting
    std::atomic<double> lastCallbackIntervalMs; // Time since last callback (for rate calculation)
    std::atomic<bool> preBufferDepleted;  // Whether pre-buffer was depleted

    // 16ms budget tracking (SRP: tracks budget state, not presentation)
    std::atomic<double> windowStartTimeMs; // Start time of current 16ms window (0 = not started)
    std::atomic<int> framesServedInWindow;  // Cumulative frames in current window

    // Real-time performance tracking (1-second rolling window)
    std::atomic<double> perfWindowStartTimeMs;  // Start of current 1-second performance window
    std::atomic<int> framesRequestedInWindow;   // Frames CoreAudio requested in current window
    std::atomic<int> framesGeneratedInWindow;   // Frames we actually generated in current window
    std::atomic<double> lastCallbackTimeMs;     // Timestamp of last callback (for interval calculation)

    // Master volume
    float volume;

    AudioUnitContext() : engineHandle(nullptr), isPlaying(false),
                        audioRenderer(nullptr),
                        writePointer(0), readPointer(0),
                        underrunCount(0), bufferStatus(0),
                        totalFramesRead(0), sampleRate(44100),
                        lastReqFrames(0), lastGotFrames(0),
                        lastRenderMs(0.0), lastHeadroomMs(0.0),
                        lastBudgetPct(0.0), lastFrameBudgetPct(0.0),
                        lastBufferTrendPct(0.0), lastCallbackIntervalMs(0.0),
                        preBufferDepleted(false),
                        windowStartTimeMs(0.0),
                        framesServedInWindow(0),
                        perfWindowStartTimeMs(0.0),
                        framesRequestedInWindow(0),
                        framesGeneratedInWindow(0),
                        lastCallbackTimeMs(0.0),
                        volume(1.0f) {}
    
    // Helper to set the rendering mode (Strategy injection point)
    void setRenderer(IAudioRenderer* renderer) { audioRenderer = renderer; }
};

// ============================================================================
// AudioPlayer Class - Uses injected IAudioRenderer (DI pattern)
// OCP: Uses Strategy pattern for rendering - no boolean flags
// SRP: Rendering logic is in IAudioRenderer, not AudioPlayer
// DI: Renderer is injected via constructor, not owned
// ============================================================================

class AudioPlayer {
public:
    // Constructor with injected IAudioRenderer and logger (DI pattern)
    // Caller owns the renderer - AudioPlayer just uses it
    // Logger defaults to ConsoleLogger if null
    AudioPlayer(IAudioRenderer* renderer, ILogging* logger = nullptr);
    ~AudioPlayer();

    // Initialize using IAudioMode (DI pattern)
    // IAudioMode creates the appropriate AudioUnitContext
    bool initialize(IAudioMode& audioMode, int sr, EngineSimHandle handle, 
                    const EngineSimAPI* api);
    
    void cleanup();

    // Set the engine simulator handle for audio rendering
    void setEngineHandle(EngineSimHandle handle);

    // Start playback - begins real-time streaming
    bool start();

    // Stop playback
    void stop();

    // Set output volume (0.0 to 1.0)
    void setVolume(float volume);

    // Play buffer (for compatibility with streaming mode)
    bool playBuffer(const float* data, int frames, int sampleRate);

    void waitForCompletion();

    // Add samples to circular buffer with true continuous writes
    void addToCircularBuffer(const float* samples, int frameCount);

    // Expose context for main loop access
    AudioUnitContext* getContext();
    const AudioUnitContext* getContext() const;

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

    // Injected renderer - not owned by AudioPlayer (DI)
    IAudioRenderer* renderer;

    // Logging: owns ConsoleLogger by default, or uses injected logger
    std::unique_ptr<ConsoleLogger> defaultLogger_;
    ILogging* logger_;  // Non-null, points to defaultLogger_ or injected logger

    // Static callback for real-time audio rendering
    static OSStatus audioUnitCallback(
        void* refCon,
        AudioUnitRenderActionFlags* actionFlags,
        const AudioTimeStamp* timeStamp,
        UInt32 busNumber,
        UInt32 numberFrames,
        AudioBufferList* ioData
    );

    bool setupAudioUnit();
};

#endif // AUDIO_PLAYER_H
