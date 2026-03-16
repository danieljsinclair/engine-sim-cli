// SineGenerator.cpp - Sine wave audio generation implementation

#include "SineGenerator.h"

#include <cmath>

// ============================================================================
// SineGenerator Implementation
// ============================================================================

SineGenerator::SineGenerator()
    : sampleRate_(44100), phase_(0.0), initialized_(false) {
}

SineGenerator::~SineGenerator() {
    cleanup();
}

bool SineGenerator::initialize(int sampleRate) {
    cleanup();
    
    if (sampleRate <= 0) {
        return false;
    }
    
    sampleRate_ = sampleRate;
    phase_ = 0.0;
    initialized_ = true;
    
    return true;
}

void SineGenerator::cleanup() {
    sampleRate_ = 44100;
    phase_ = 0.0;
    initialized_ = false;
}

double SineGenerator::rpmToFrequency(double RPM) {
    // Mapping: 600 RPM = 100 Hz, 6000 RPM = 1000 Hz
    // Formula: frequency = (RPM / 600.0) * 100.0
    return (RPM / 600.0) * 100.0;
}

void SineGenerator::generate(float* output, int frameCount, double frequency, double amplitude) {
    if (!initialized_ || !output || frameCount <= 0 || frequency <= 0) {
        return;
    }

    const double twoPi = 2.0 * M_PI;
    const double phaseIncrement = twoPi * frequency / sampleRate_;

    for (int i = 0; i < frameCount; i++) {
        double sample = amplitude * std::sin(phase_);
        
        // Stereo: same sample on left and right
        output[i * 2] = static_cast<float>(sample);         // Left
        output[i * 2 + 1] = static_cast<float>(sample);     // Right
        
        // Advance phase
        phase_ += phaseIncrement;
        
        // Keep phase in [0, 2π) to avoid overflow
        if (phase_ >= twoPi) {
            phase_ -= twoPi;
        }
    }
}

void SineGenerator::generateRPMLinked(float* output, int frameCount, double RPM, double amplitude) {
    double frequency = rpmToFrequency(RPM);
    generate(output, frameCount, frequency, amplitude);
}

void SineGenerator::setPhase(double phase) {
    const double twoPi = 2.0 * M_PI;
    phase_ = std::fmod(phase, twoPi);
    if (phase_ < 0) {
        phase_ += twoPi;
    }
}
