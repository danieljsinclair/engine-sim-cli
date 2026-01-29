// Sine Wave Generator - Test tone generation for audio path verification
//
// This module provides a simple sine wave generator for testing the audio pipeline.
// It generates clean test tones to verify the audio chain is working correctly.

#ifndef SINE_WAVE_GENERATOR_H
#define SINE_WAVE_GENERATOR_H

#include <vector>

struct SineWaveConfig {
    double frequency;   // Frequency in Hz (e.g., 440.0 for A4)
    double duration;    // Duration in seconds
    double amplitude;   // Amplitude 0.0 to 1.0
    int sampleRate;     // Sample rate in Hz (e.g., 48000)
    int channels;       // Number of audio channels (1 = mono, 2 = stereo)

    SineWaveConfig()
        : frequency(440.0), duration(1.0), amplitude(0.5), sampleRate(48000), channels(2) {}
};

/**
 * Generate a sine wave test tone.
 *
 * @param buffer Output buffer that will be resized to hold the generated samples
 * @param config Configuration parameters for the sine wave
 *
 * The buffer contains interleaved samples: [L, R, L, R, ...] for stereo,
 * or [sample, sample, ...] for mono.
 *
 * Applies fade-in/fade-out to avoid clicks at the start/end.
 */
void generateSineWave(std::vector<float>& buffer, const SineWaveConfig& config);

#endif // SINE_WAVE_GENERATOR_H
