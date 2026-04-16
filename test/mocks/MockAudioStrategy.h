// MockAudioStrategy.h - Mock implementation for testing IAudioStrategy

#ifndef MOCK_AUDIO_STRATEGY_H
#define MOCK_AUDIO_STRATEGY_H

#include "audio/strategies/IAudioStrategy.h"
#include "audio/state/Diagnostics.h"
#include <cstring>
#include <atomic>

class MockAudioStrategy : public IAudioStrategy {
public:
    const char* getName() const override { return "Mock"; }
    bool isEnabled() const override { return true; }
    bool isPlaying() const override { return playing_.load(); }
    bool shouldDrainDuringWarmup() const override { return false; }

    bool render(AudioBufferList* ioData, UInt32 numberFrames) override {
        if (ioData && ioData->mBuffers[0].mData) {
            float* data = static_cast<float*>(ioData->mBuffers[0].mData);
            size_t samples = numberFrames * 2;
            std::memset(data, 0, samples * sizeof(float));
        }
        return true;
    }

    bool AddFrames(float* buffer, int frameCount) override { return true; }
    void reset() override {}
    std::string getModeString() const override { return "Mock mode"; }

    bool initialize(const AudioStrategyConfig& config) override { return true; }
    void prepareBuffer() override {}
    bool startPlayback(EngineSimHandle handle, const EngineSimAPI* api) override {
        playing_.store(true);
        return true;
    }
    void stopPlayback(EngineSimHandle handle, const EngineSimAPI* api) override {
        playing_.store(false);
    }
    void resetBufferAfterWarmup() override {}
    void updateSimulation(EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) override {}
    void fillBufferFromEngine(EngineSimHandle handle, const EngineSimAPI& api, int frames) override {}
    Diagnostics::Snapshot getDiagnosticsSnapshot() const override { return Diagnostics::Snapshot(); }

private:
    std::atomic<bool> playing_{false};
};

#endif // MOCK_AUDIO_STRATEGY_H
