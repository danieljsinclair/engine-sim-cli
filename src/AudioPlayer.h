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
#include "engine_sim_loader.h"

#include "CircularBuffer.h"
#include "SyncPullAudio.h"

// Forward declarations
class IAudioMode;

// ============================================================================
// Strategy Pattern: IAudioRenderer Interface
// Abstracts the rendering strategy (sync-pull vs cursor-chasing)
// OCP: New modes can be added without modifying existing code
// SRP: Each renderer encapsulates its own rendering logic
// DI: Renderer is injected via constructor - AudioPlayer doesn't create it
// ============================================================================

class IAudioRenderer {
public:
    virtual ~IAudioRenderer() = default;
    
    // Render audio to the buffer list - returns true if rendering succeeded
    virtual bool render(
        void* context,
        AudioBufferList* ioData,
        UInt32 numberFrames
    ) = 0;
    
    // Check if this renderer is active/enabled
    virtual bool isEnabled() const = 0;
    
    // Get the name of this renderer for diagnostics
    virtual const char* getName() const = 0;
    
    // Add frames to the renderer's buffer (for playBuffer compatibility)
    // Returns true if frames were added successfully
    virtual bool AddFrames(void* context, float* buffer, int frameCount) = 0;
};

// ============================================================================
// Concrete Renderer: SyncPullRenderer
// Renders audio on-demand synchronously from the engine simulator
// Used when the caller controls timing and requests frames as needed
// ============================================================================

class SyncPullRenderer : public IAudioRenderer {
public:
    bool render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) override;
    bool isEnabled() const override;
    const char* getName() const override { return "SyncPullRenderer"; }
    bool AddFrames(void* ctx, float* buffer, int frameCount) override;
};

// ============================================================================
// Concrete Renderer: CircularBufferRenderer  
// Renders audio from a cursor-chasing circular buffer
// Uses hardware feedback to maintain 100ms lead, preventing underruns
// ============================================================================

class CircularBufferRenderer : public IAudioRenderer {
public:
    bool render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) override;
    bool isEnabled() const override;
    const char* getName() const override { return "CircularBufferRenderer"; }
    bool AddFrames(void* ctx, float* buffer, int frameCount) override;
};

// ============================================================================
// Null Renderer: Renders silence
// Used when no valid mode is configured
// ============================================================================

class SilentRenderer : public IAudioRenderer {
public:
    bool render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) override;
    bool isEnabled() const override;
    const char* getName() const override { return "SilentRenderer"; }
    bool AddFrames(void* ctx, float* buffer, int frameCount) override;
};

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

    // Silent mode: zero output after processing (mute at callback level)
    bool silent;

    AudioUnitContext() : engineHandle(nullptr), isPlaying(false),
                        audioRenderer(nullptr),
                        writePointer(0), readPointer(0),
                        underrunCount(0), bufferStatus(0),
                        totalFramesRead(0), sampleRate(44100),
                        silent(false) {}
    
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
    // Constructor with injected IAudioRenderer (DI pattern)
    // Caller owns the renderer - AudioPlayer just uses it
    explicit AudioPlayer(IAudioRenderer* renderer);
    ~AudioPlayer();

    // Initialize using IAudioMode (DI pattern)
    // IAudioMode creates the appropriate AudioUnitContext
    bool initialize(IAudioMode& audioMode, int sr, EngineSimHandle handle, 
                    const EngineSimAPI* api, bool silent);
    
    void cleanup();

    // Set the engine simulator handle for audio rendering
    void setEngineHandle(EngineSimHandle handle);

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

    // Injected renderer - not owned by AudioPlayer (DI)
    IAudioRenderer* renderer;

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
