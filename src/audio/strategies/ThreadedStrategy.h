// ThreadedStrategy.h - Cursor-chasing audio strategy
// Implements IAudioStrategy using BufferContext state model
// SRP: Single responsibility - only implements threaded cursor-chasing rendering
// OCP: New strategies can be added without modifying existing code
// DIP: Depends on state abstractions, not concrete implementations

#ifndef THREADED_STRATEGY_H
#define THREADED_STRATEGY_H

#include <string>
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>

#include "audio/strategies/IAudioStrategy.h"
#include "audio/state/BufferContext.h"
#include "ILogging.h"

/**
 * ThreadedStrategy - Cursor-chasing audio strategy
 *
 * Audio is generated in the main loop, written to a circular buffer,
 * and read by the real-time audio callback at playback cursor position.
 * Maintains ~100ms lead between generation and playback.
 *
 * SRP: Only implements threaded cursor-chasing rendering
 * OCP: New strategies can be added without modifying this code
 * DIP: Depends on BufferContext abstraction, not concrete state
 */
class ThreadedStrategy : public IAudioStrategy {
public:
    explicit ThreadedStrategy(ILogging* logger = nullptr);

    // IAudioStrategy Implementation
    const char* getName() const override;
    bool isEnabled() const override;

    bool render(BufferContext* context, AudioBufferList* ioData, UInt32 numberFrames) override;
    bool AddFrames(BufferContext* context, float* buffer, int frameCount) override;

    // Lifecycle Methods
    bool initialize(BufferContext* context, const AudioStrategyConfig& config) override;
    void prepareBuffer(BufferContext* context) override;
    bool startPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) override;
    void stopPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) override;
    void resetBufferAfterWarmup(BufferContext* context) override;
    void updateSimulation(BufferContext* context, EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) override;

    // Strategy-Specific Methods
    bool shouldDrainDuringWarmup() const override;
    bool needsMainThreadAudioGeneration() const override;
    std::string getDiagnostics() const override;
    std::string getProgressDisplay() const override;
    void configure(const AudioStrategyConfig& config) override;
    void reset() override;
    std::string getModeString() const override;

private:
    ILogging* logger_;

    int getAvailableFrames(const BufferContext* context) const;
    void updateDiagnostics(BufferContext* context, int availableFrames, int framesRequested);
};

#endif // THREADED_STRATEGY_H
