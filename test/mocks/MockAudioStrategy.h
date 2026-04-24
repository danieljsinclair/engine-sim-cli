// MockAudioStrategy.h - Mock implementation for testing IAudioBuffer
// Phase E: Updated to use ISimulator* instead of EngineSimHandle/EngineSimAPI

#ifndef MOCK_AUDIO_STRATEGY_H
#define MOCK_AUDIO_STRATEGY_H

#include "strategy/IAudioBuffer.h"
#include <cstring>
#include <atomic>

class MockAudioStrategy : public IAudioBuffer {
public:
    const char* getName() const override { return "Mock"; }
    bool isEnabled() const override { return true; }
    bool isPlaying() const override { return playing_.load(); }
    bool shouldDrainDuringWarmup() const override { return false; }

    bool render(AudioBufferView& buffer) override {
        float* dst = buffer.asFloat();
        if (dst) {
            size_t samples = static_cast<size_t>(buffer.frameCount) * buffer.channelCount;
            std::memset(dst, 0, samples * sizeof(float));
        }
        return true;
    }

    bool AddFrames(float* buffer, int frameCount) override { return true; }
    void reset() override {}
    std::string getModeString() const override { return "Mock mode"; }

    bool initialize(const AudioBufferConfig& config) override { return true; }
    void prepareBuffer() override {}
    bool startPlayback(ISimulator* simulator) override {
        playing_.store(true);
        return true;
    }
    void stopPlayback(ISimulator* simulator) override {
        playing_.store(false);
    }
    void resetBufferAfterWarmup() override {}
    void updateSimulation(ISimulator* simulator, double deltaTimeMs) override {}
    void fillBufferFromEngine(ISimulator* simulator, int frames) override {}

private:
    std::atomic<bool> playing_{false};
};

#endif // MOCK_AUDIO_STRATEGY_H
