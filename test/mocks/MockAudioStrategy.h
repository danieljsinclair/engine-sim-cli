// MockAudioStrategy.h - Mock implementation for testing IAudioStrategy
// Used in unit tests to avoid instantiating abstract classes

#ifndef MOCK_AUDIO_STRATEGY_H
#define MOCK_AUDIO_STRATEGY_H

#include "audio/strategies/IAudioStrategy.h"
#include "audio/state/StrategyContext.h"
#include <cstring>

/**
 * MockAudioStrategy - Concrete implementation for testing
 *
 * This provides a minimal implementation of IAudioStrategy for unit testing
 */
class MockAudioStrategy : public IAudioStrategy {
public:
    const char* getName() const override {
        return "Mock";
    }

    bool isEnabled() const override {
        return true;
    }

    bool shouldDrainDuringWarmup() const override {
        return false;
    }

    bool render(StrategyContext* context, AudioBufferList* ioData, UInt32 numberFrames) override {
        // Fill with silence for testing
        if (ioData && ioData->mBuffers[0].mData) {
            float* data = static_cast<float*>(ioData->mBuffers[0].mData);
            size_t samples = numberFrames * 2; // stereo
            std::memset(data, 0, samples * sizeof(float));
        }
        return true;
    }

    bool AddFrames(StrategyContext* context, float* buffer, int frameCount) override {
        return true;
    }

    std::string getDiagnostics() const override {
        return "Mock strategy diagnostics";
    }

    std::string getProgressDisplay() const override {
        return "[MOCK] progress display";
    }

    void configure(const AudioStrategyConfig& config) override {
        // No-op for testing
    }

    void reset() override {
        // No-op for testing
    }

    std::string getModeString() const override {
        return "Mock mode";
    }
};

#endif // MOCK_AUDIO_STRATEGY_H