// AudioMode.h - Strategy pattern for audio mode behavior
// Refactored to eliminate syncPull branching and comply with OCP/SRP

#ifndef AUDIO_MODE_H
#define AUDIO_MODE_H

#include <memory>
#include <string>
#include <vector>

#include "engine_sim_bridge.h"
#include "engine_sim_loader.h"

class AudioPlayer;
class IAudioSource;

// ============================================================================
// IAudioMode Interface - Strategy Pattern
// Encapsulates mode-specific behavior for audio processing
// ============================================================================

class IAudioMode {
public:
    virtual ~IAudioMode() = default;
    
    // Get mode name for diagnostics
    virtual std::string getModeName() const = 0;
    
    // Update simulation - different strategies for each mode
    virtual void updateSimulation(EngineSimHandle handle, const EngineSimAPI& api, 
                                   AudioPlayer* audioPlayer) = 0;
    
    // Generate audio - threaded mode generates audio in main loop
    virtual void generateAudio(IAudioSource& audioSource, AudioPlayer* audioPlayer) = 0;
    
    // Get mode string for diagnostics display
    virtual std::string getModeString() const = 0;
    
    // Start audio thread if needed (threaded mode only)
    virtual bool startAudioThread(EngineSimHandle handle, const EngineSimAPI& api,
                                   AudioPlayer* audioPlayer) = 0;
    
    // Prepare audio buffer for playback
    virtual void prepareBuffer(AudioPlayer* audioPlayer) = 0;
    
    // Reset buffer after warmup (threaded mode only)
    virtual void resetBufferAfterWarmup(AudioPlayer* audioPlayer) = 0;
    
    // Start playback
    virtual void startPlayback(AudioPlayer* audioPlayer) = 0;
    
    // Check if drain is needed during warmup
    virtual bool shouldDrainDuringWarmup() const = 0;
};

// ============================================================================
// SyncPullAudioMode - Sync-pull (direct) audio mode
// Engine simulation advances in lock-step with audio playback
// ============================================================================

class SyncPullAudioMode : public IAudioMode {
public:
    std::string getModeName() const override;
    
    void updateSimulation(EngineSimHandle handle, const EngineSimAPI& api,
                          AudioPlayer* audioPlayer) override;
    
    void generateAudio(IAudioSource& audioSource, AudioPlayer* audioPlayer) override;
    
    std::string getModeString() const override;
    
    bool startAudioThread(EngineSimHandle handle, const EngineSimAPI& api,
                          AudioPlayer* audioPlayer) override;
    
    void prepareBuffer(AudioPlayer* audioPlayer) override;
    
    void resetBufferAfterWarmup(AudioPlayer* audioPlayer) override;
    
    void startPlayback(AudioPlayer* audioPlayer) override;
    
    bool shouldDrainDuringWarmup() const override;
};

// ============================================================================
// ThreadedAudioMode - Threaded/cursor-chasing audio mode
// Separate audio thread with cursor-chasing buffer management
// ============================================================================

class ThreadedAudioMode : public IAudioMode {
public:
    std::string getModeName() const override;
    
    void updateSimulation(EngineSimHandle handle, const EngineSimAPI& api,
                          AudioPlayer* audioPlayer) override;
    
    void generateAudio(IAudioSource& audioSource, AudioPlayer* audioPlayer) override;
    
    std::string getModeString() const override;
    
    bool startAudioThread(EngineSimHandle handle, const EngineSimAPI& api,
                          AudioPlayer* audioPlayer) override;
    
    void prepareBuffer(AudioPlayer* audioPlayer) override;
    
    void resetBufferAfterWarmup(AudioPlayer* audioPlayer) override;
    
    void startPlayback(AudioPlayer* audioPlayer) override;
    
    bool shouldDrainDuringWarmup() const override;
};

// ============================================================================
// Factory function to create audio mode based on syncPull flag
// ============================================================================

std::unique_ptr<IAudioMode> createAudioMode(bool syncPull);

#endif // AUDIO_MODE_H
