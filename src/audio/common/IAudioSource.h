// IAudioSource.h - Audio source interface for modular audio architecture
//
// Implementations provide audio data to the platform layer.

#ifndef AUDIO_COMMON_IAUDIO_SOURCE_H
#define AUDIO_COMMON_IAUDIO_SOURCE_H

namespace audio {

/**
 * Audio source interface.
 * Implementations provide audio data to the platform layer.
 */
class IAudioSource {
public:
    virtual ~IAudioSource() = default;

    /**
     * Generate audio samples.
     * Called by the audio platform during playback.
     *
     * @param buffer Output buffer (interleaved stereo float)
     * @param frames Number of frames to generate
     * @return Actual number of frames generated
     *
     * CRITICAL: This function MUST be allocation-free and real-time safe.
     * Called from audio thread - no blocking operations, no logging.
     */
    virtual int generateAudio(float* buffer, int frames) = 0;

    /**
     * Update audio source state (called from main thread).
     * @param deltaTime Time step in seconds
     */
    virtual void update(double deltaTime) = 0;
};

} // namespace audio

#endif // AUDIO_COMMON_IAUDIO_SOURCE_H
