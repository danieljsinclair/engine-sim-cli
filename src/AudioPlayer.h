// AudioPlayer.h - Audio playback engine
// SRP: Orchestrates audio playback using injected strategy and hardware provider
// OCP: New strategies and hardware providers can be added without modification
// DIP: Depends on IAudioStrategy and IAudioHardwareProvider abstractions

#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include "audio/strategies/IAudioStrategy.h"
#include "audio/state/BufferContext.h"
#include "audio/common/CircularBuffer.h"
#include "audio/hardware/IAudioHardwareProvider.h"
#include "ILogging.h"

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <memory>

/**
 * AudioPlayer - Orchestrates audio playback
 *
 * Composes:
 * - BufferContext: Owns the shared audio state
 * - IAudioStrategy: Injected rendering strategy (Threaded/SyncPull)
 * - IAudioHardwareProvider: Platform-specific audio output (CoreAudio)
 * - CircularBuffer: Owned buffer for audio data
 *
 * AudioPlayer is responsible for:
 * - Wiring the strategy, context, and hardware together
 * - Managing the CoreAudio callback registration
 * - Providing buffer manipulation methods for the simulation loop
 *
 * AudioPlayer is NOT responsible for:
 * - Knowing how audio is generated (that's IAudioStrategy)
 * - Knowing platform details (that's IAudioHardwareProvider)
 * - Managing simulation state (that's SimulationLoop)
 */
class AudioPlayer {
public:
    /**
     * Construct AudioPlayer with injected strategy and optional logger.
     * @param strategy The audio rendering strategy (not owned by AudioPlayer)
     * @param logger Optional logger; creates default if nullptr
     */
    AudioPlayer(IAudioStrategy* strategy, ILogging* logger = nullptr);
    ~AudioPlayer();

    // Non-copyable, non-movable
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    /**
     * Initialize audio player with format and engine simulator handles.
     * Sets up hardware provider, context, and strategy.
     */
    bool initialize(int sampleRate, EngineSimHandle handle, const EngineSimAPI* api);

    /** Start audio playback via hardware provider */
    bool start();

    /** Stop audio playback via hardware provider */
    void stop();

    /** Set output volume (0.0 to 1.0) */
    void setVolume(float volume);

    /** Add audio frames to the circular buffer via strategy */
    void addToCircularBuffer(const float* samples, int frameCount);

    /** Reset the circular buffer and pre-fill with silence */
    void resetCircularBuffer();

    /** Calculate cursor-chasing frame count for adaptive buffer management */
    int calculateCursorChasingSamples(int defaultFrames);

    /** Set the engine handle on the context */
    void setEngineHandle(EngineSimHandle handle);

    // === Accessors ===

    BufferContext* getContext();
    const BufferContext* getContext() const;

    bool getIsPlaying() const { return isPlaying_; }
    IAudioStrategy* getStrategy() { return strategy_; }

    // === Diagnostics ===
    void getBufferDiagnostics(int& writePtr, int& readPtr, int& available, int& status);
    void resetBufferDiagnostics();
    void waitForCompletion();

private:
    // Owned state
    BufferContext context_;
    CircularBuffer circularBuffer_;
    std::unique_ptr<IAudioHardwareProvider> hardwareProvider_;

    // Injected dependencies (not owned)
    IAudioStrategy* strategy_;

    // Logger (owned if created internally)
    std::unique_ptr<ILogging> defaultLogger_;
    ILogging* logger_;

    // Playback state
    bool isPlaying_;
    int sampleRate_;

    /** CoreAudio static callback - bridges to AudioPlayer instance */
    static OSStatus audioUnitCallback(
        void* refCon,
        AudioUnitRenderActionFlags* actionFlags,
        const AudioTimeStamp* timeStamp,
        UInt32 busNumber,
        UInt32 numberFrames,
        AudioBufferList* ioData
    );

    void cleanup();
};

#endif // AUDIO_PLAYER_H
