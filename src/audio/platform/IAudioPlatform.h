// IAudioPlatform.h - Platform-agnostic audio output interface
//
// Each platform (macOS, iOS, ESP32) implements this interface.
// The platform layer handles all audio playback specifics.

#ifndef AUDIO_PLATFORM_IAUDIO_PLATFORM_H
#define AUDIO_PLATFORM_IAUDIO_PLATFORM_H

#include "audio/common/IAudioSource.h"

namespace audio {
namespace platform {

// IAudioSource is now included, no forward declaration needed

/**
 * Platform-agnostic audio output interface.
 * Each platform (macOS, iOS, ESP32) implements this interface.
 * The platform layer handles all audio playback specifics.
 */
class IAudioPlatform {
public:
    virtual ~IAudioPlatform() = default;

    /**
     * Initialize the audio platform.
     * @param sampleRate Sample rate (e.g., 48000)
     * @param framesPerBuffer Buffer size for audio callback (e.g., 256, 512)
     * @return true if successful
     */
    virtual bool initialize(int sampleRate, int framesPerBuffer) = 0;

    /**
     * Start audio playback.
     * @return true if successful
     */
    virtual bool start() = 0;

    /**
     * Stop audio playback.
     */
    virtual void stop() = 0;

    /**
     * Set output volume.
     * @param volume Volume level (0.0 to 1.0)
     */
    virtual void setVolume(float volume) = 0;

    /**
     * Set the audio source for this platform.
     * The platform will call back to the source during audio rendering.
     * @param source Audio source implementation (caller retains ownership)
     */
    virtual void setAudioSource(IAudioSource* source) = 0;

    /**
     * Cleanup and release resources.
     */
    virtual void cleanup() = 0;

    /**
     * Get platform name for diagnostics.
     */
    virtual const char* getPlatformName() const = 0;
};

} // namespace platform
} // namespace audio

#endif // AUDIO_PLATFORM_IAUDIO_PLATFORM_H
