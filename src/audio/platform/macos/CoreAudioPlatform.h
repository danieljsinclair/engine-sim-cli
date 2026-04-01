// CoreAudioPlatform.h - macOS audio platform using CoreAudio AudioUnit
// Implements IAudioPlatform interface for macOS

#ifndef AUDIO_PLATFORM_MACOS_CORE_AUDIO_PLATFORM_H
#define AUDIO_PLATFORM_MACOS_CORE_AUDIO_PLATFORM_H

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include "audio/platform/IAudioPlatform.h"
#include "audio/common/IAudioSource.h"

namespace audio {
namespace platform {
namespace macos {

/**
 * macOS audio platform using CoreAudio AudioUnit.
 * Wraps the CoreAudio-specific audio output functionality.
 */
class CoreAudioPlatform : public IAudioPlatform {
public:
    CoreAudioPlatform();
    ~CoreAudioPlatform() override;

    // IAudioPlatform interface implementation
    bool initialize(int sampleRate, int framesPerBuffer) override;
    bool start() override;
    void stop() override;
    void setVolume(float volume) override;
    void setAudioSource(IAudioSource* source) override;
    void cleanup() override;
    const char* getPlatformName() const override { return "CoreAudio"; }

private:
    AudioUnit audioUnit_;
    AudioDeviceID deviceID_;
    IAudioSource* audioSource_;
    int sampleRate_;
    int framesPerBuffer_;
    bool isPlaying_;
    bool isInitialized_;

    // Static audio callback for CoreAudio
    static OSStatus audioCallback(
        void* refCon,
        AudioUnitRenderActionFlags* actionFlags,
        const AudioTimeStamp* timeStamp,
        UInt32 busNumber,
        UInt32 numberFrames,
        AudioBufferList* ioData
    );

    bool setupAudioUnit();
    OSStatus renderAudio(AudioBufferList* ioData, UInt32 numberFrames);
};

} // namespace macos
} // namespace platform
} // namespace audio

#endif // AUDIO_PLATFORM_MACOS_CORE_AUDIO_PLATFORM_H
