// ThreadedAudioMode.h - Threaded audio mode header
// Cursor-chasing mode with separate audio thread

#ifndef THREADED_AUDIO_MODE_H
#define THREADED_AUDIO_MODE_H

#include <memory>
#include <string>

#include "engine_sim_bridge.h"
#include "audio/modes/IAudioMode.h"

class AudioPlayer;
class AudioUnitContext;
class IAudioSource;
class CircularBuffer;

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
        const EngineSimAPI* engineAPI
    ) override;
};

#endif // THREADED_AUDIO_MODE_H
