// Sine Wave Generator - Test tone generation for audio path verification

#include "sine_wave_generator.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void generateSineWave(std::vector<float>& buffer, const SineWaveConfig& config) {
    // Calculate total number of samples (frames * channels)
    int numFrames = static_cast<int>(config.duration * config.sampleRate);
    int numSamples = numFrames * config.channels;
    buffer.resize(numSamples);

    const double twoPi = 2.0 * M_PI;

    // Generate interleaved samples
    for (int frame = 0; frame < numFrames; ++frame) {
        double t = static_cast<double>(frame) / config.sampleRate;
        float sample = static_cast<float>(std::sin(twoPi * config.frequency * t) * config.amplitude);

        // Write to all channels (mono -> stereo expansion)
        for (int ch = 0; ch < config.channels; ++ch) {
            buffer[frame * config.channels + ch] = sample;
        }
    }

    // Apply fade-in/fade-out to avoid clicks (10ms fade)
    int fadeFrames = config.sampleRate / 100;
    if (fadeFrames > numFrames) {
        fadeFrames = numFrames;
    }

    // Fade in
    for (int frame = 0; frame < fadeFrames; ++frame) {
        float gain = static_cast<float>(frame) / fadeFrames;
        for (int ch = 0; ch < config.channels; ++ch) {
            buffer[frame * config.channels + ch] *= gain;
        }
    }

    // Fade out
    for (int frame = 0; frame < fadeFrames; ++frame) {
        float gain = static_cast<float>(frame) / fadeFrames;
        int outFrame = numFrames - 1 - frame;
        for (int ch = 0; ch < config.channels; ++ch) {
            buffer[outFrame * config.channels + ch] *= gain;
        }
    }
}
