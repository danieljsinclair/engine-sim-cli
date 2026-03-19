// IAudioMode.h - Audio mode interface
// Strategy pattern - abstracts audio mode behavior (sync-pull vs threaded)
// OCP: New modes can be added without modifying existing code
// SRP: Each mode encapsulates its own behavior
// DI: Each mode creates and injects its own IAudioRenderer into context

#ifndef IAUDIO_MODE_H
#define IAUDIO_MODE_H

#include <memory>
#include <string>
#include <vector>

#include "engine_sim_bridge.h"
#include "engine_sim_loader.h"

class AudioPlayer;
class AudioUnitContext;
class IAudioSource;
class IAudioRenderer;


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
        const EngineSimAPI* engineAPI
    ) = 0;
};

std::unique_ptr<IAudioMode> createAudioModeFactory(const EngineSimAPI* engineAPI, bool preferSyncPull = true);

#endif // IAUDIO_MODE_H
