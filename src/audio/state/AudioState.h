// AudioState.h - Core audio playback state
// SRP: Single responsibility - manages only playback state
// OCP: New states can be added without modifying existing code
// DIP: High-level modules depend on this abstraction

#ifndef AUDIO_STATE_H
#define AUDIO_STATE_H

#include <atomic>

/**
 * AudioState - Core audio playback state
 *
 * Responsibilities:
 * - Track if audio is currently playing
 * - Maintain sample rate for timing calculations
 * - Thread-safe state management
 *
 * SRP: Only manages playback state, not buffer or diagnostics
 */
struct AudioState {
    /**
     * Initialize with default values
     */
    AudioState()
        : isPlaying(false)
        , sampleRate(44100)
    {}

    /**
     * Current playback state
     * - true: Audio is actively playing
     * - false: Audio is stopped or paused
     */
    std::atomic<bool> isPlaying;

    /**
     * Sample rate in Hz (e.g., 44100, 48000)
     * Used for timing calculations throughout the audio pipeline
     */
    int sampleRate;

    /**
     * Reset state to initial values
     * Useful for cleanup or re-initialization scenarios
     */
    void reset() {
        isPlaying.store(false);
        sampleRate = 44100;
    }
};

#endif // AUDIO_STATE_H
