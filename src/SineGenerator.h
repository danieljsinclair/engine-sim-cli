// SineGenerator.h - Sine wave audio generation
// ONE place for sine generation used by both audio modes

#ifndef SINE_GENERATOR_H
#define SINE_GENERATOR_H

#include <cstddef>
#include <cstdint>

// ============================================================================
// SineGenerator - Generates sine wave audio samples
// Used by both sync-pull and threaded modes for consistent sine generation
// ============================================================================

class SineGenerator {
public:
    SineGenerator();
    ~SineGenerator();

    // Initialize with sample rate
    bool initialize(int sampleRate);

    // Cleanup resources
    void cleanup();

    // Generate sine wave samples (stereo interleaved)
    // frequency: frequency in Hz
    // output: stereo interleaved output buffer
    // frameCount: number of frames to generate
    // amplitude: volume (0.0 to 1.0, default 0.5)
    void generate(float* output, int frameCount, double frequency, double amplitude = 0.5);

    // Generate sine wave with RPM-linked frequency
    // RPM: current engine RPM
    // output: stereo interleaved output buffer
    // frameCount: number of frames to generate
    // amplitude: volume (0.0 to 1.0, default 0.5)
    // Maps RPM to frequency: 600 RPM = 100 Hz, 6000 RPM = 1000 Hz
    void generateRPMLinked(float* output, int frameCount, double RPM, double amplitude = 0.5);

    // Get sample rate
    int getSampleRate() const { return sampleRate_; }

    // Check if initialized
    bool isInitialized() const { return initialized_; }

    // Set phase offset (for continuous sine across buffer boundaries)
    void setPhase(double phase);
    double getPhase() const { return phase_; }

private:
    int sampleRate_;
    double phase_;           // Current phase for continuous sine
    bool initialized_;

    // Calculate frequency from RPM (600 RPM = 100 Hz, 6000 RPM = 1000 Hz)
    static double rpmToFrequency(double RPM);
};

#endif // SINE_GENERATOR_H
