// AudioMode.h - Strategy pattern for audio mode behavior
// Refactored to eliminate enum, switch statements, and boolean flags
// Renderer is injected into context by each mode (OCP, SRP, DI)

#ifndef AUDIO_MODE_H
#define AUDIO_MODE_H

#include <memory>
#include <string>
#include <vector>

#include "engine_sim_bridge.h"
#include "engine_sim_loader.h"

class AudioPlayer;
class AudioUnitContext;
class IAudioSource;
class IAudioRenderer;

// ============================================================================
// IAudioMode Interface - Strategy Pattern
// Factory returns this directly - no enum, no switch, no boolean flags
// OCP: New modes can be added without modifying existing code
// SRP: Each mode encapsulates its own behavior
// DI: Each mode creates and injects its own IAudioRenderer into context
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
    
    // Factory method: Create the audio context for AudioPlayer
    // Each mode creates its own context with appropriate audio components
    // DI: Also creates and injects the appropriate IAudioRenderer into context
    virtual std::unique_ptr<AudioUnitContext> createContext(
        int sampleRate,
        EngineSimHandle engineHandle,
        const EngineSimAPI* engineAPI,
        bool silent
    ) = 0;
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
    
    std::unique_ptr<AudioUnitContext> createContext(
        int sampleRate,
        EngineSimHandle engineHandle,
        const EngineSimAPI* engineAPI,
        bool silent
    ) override;
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
    
    std::unique_ptr<AudioUnitContext> createContext(
        int sampleRate,
        EngineSimHandle engineHandle,
        const EngineSimAPI* engineAPI,
        bool silent
    ) override;
};

// ============================================================================
// Factory function - Returns IAudioMode directly based on API capabilities
// No enum, no switch - factory decides internally
// ============================================================================

std::unique_ptr<IAudioMode> createAudioModeFactory(const EngineSimAPI* engineAPI);

#endif // AUDIO_MODE_H
