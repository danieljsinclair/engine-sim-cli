// AudioPlayer.h - Audio playback facade
// Composes hardware, buffer, and strategy behind a clean interface
// DIP: All dependencies injected (strategy, hardware provider, logger)

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
 * AudioPlayer - Facade for audio playback subsystem
 *
 * Composes hardware provider, buffer, and strategy. Owns the CoreAudio
 * callback registration and wires it to the strategy's render() method.
 *
 * All dependencies are injected: strategy, hardware provider, logger.
 */
class AudioPlayer {
public:
    AudioPlayer(IAudioStrategy* strategy,
                std::unique_ptr<IAudioHardwareProvider> hardwareProvider,
                ILogging* logger = nullptr);
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool initialize(int sampleRate, EngineSimHandle handle, const EngineSimAPI* api);
    bool start();
    void stop();
    void setVolume(float volume);

    BufferContext* getContext();
    const BufferContext* getContext() const;

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

    bool isPlaying_ = false;
    int sampleRate_ = 0;

    void cleanup();
};

#endif // AUDIO_PLAYER_H
