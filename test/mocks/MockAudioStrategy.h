// MockAudioStrategy.h - Mock implementation for testing IAudioStrategy

#ifndef MOCK_AUDIO_STRATEGY_H
#define MOCK_AUDIO_STRATEGY_H

#include "audio/strategies/IAudioStrategy.h"
#include "audio/state/BufferContext.h"
#include <cstring>

class MockAudioStrategy : public IAudioStrategy {
public:
    const char* getName() const override { return "Mock"; }
    bool isEnabled() const override { return true; }
    bool shouldDrainDuringWarmup() const override { return false; }

    bool render(BufferContext* context, AudioBufferList* ioData, UInt32 numberFrames) override {
        if (ioData && ioData->mBuffers[0].mData) {
            float* data = static_cast<float*>(ioData->mBuffers[0].mData);
            size_t samples = numberFrames * 2;
            std::memset(data, 0, samples * sizeof(float));
        }
        return true;
    }

    bool AddFrames(BufferContext* context, float* buffer, int frameCount) override { return true; }
    std::string getDiagnostics() const override { return "Mock strategy diagnostics"; }
    std::string getProgressDisplay() const override { return "[MOCK] progress display"; }
    void reset() override {}
    std::string getModeString() const override { return "Mock mode"; }

    bool initialize(BufferContext* context, const AudioStrategyConfig& config) override { return true; }
    void prepareBuffer(BufferContext* context) override {}
    bool startPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) override { return true; }
    void stopPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) override {}
    void resetBufferAfterWarmup(BufferContext* context) override {}
    void updateSimulation(BufferContext* context, EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) override {}
};

#endif // MOCK_AUDIO_STRATEGY_H
