// SyncPullStrategy.h - Lock-step audio strategy
// Implements synchronous audio generation where simulation advances with audio playback
// SRP: Single responsibility - only implements synchronous lock-step rendering
// OCP: New strategies can be added without modifying existing code
// DIP: Depends on state abstractions, not concrete implementations

#ifndef SYNC_PULL_STRATEGY_H
#define SYNC_PULL_STRATEGY_H

#include <string>
#include <atomic>

#include "audio/strategies/IAudioStrategy.h"
#include "audio/state/BufferContext.h"
#include "ILogging.h"

/**
 * SyncPullStrategy - Lock-step audio generation strategy
 *
 * Engine simulation advances in sync with audio playback.
 * Audio is rendered on-demand synchronously from engine simulator.
 * No separate audio thread is needed.
 *
 * SRP: Only implements synchronous lock-step rendering
 * OCP: New strategies can be added without modifying this code
 * DIP: Depends on BufferContext abstraction, not concrete state
 */
class SyncPullStrategy : public IAudioStrategy {
public:
    explicit SyncPullStrategy(ILogging* logger = nullptr);

    const char* getName() const override;
    bool isEnabled() const override;
    bool shouldDrainDuringWarmup() const override;

    bool render(BufferContext* context, AudioBufferList* ioData, UInt32 numberFrames) override;
    bool AddFrames(BufferContext* context, float* buffer, int frameCount) override;

    // Lifecycle Methods
    bool initialize(BufferContext* context, const AudioStrategyConfig& config) override;
    void prepareBuffer(BufferContext* context) override;
    bool startPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) override;
    void stopPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) override;
    void resetBufferAfterWarmup(BufferContext* context) override;
    void updateSimulation(BufferContext* context, EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) override;

    std::string getDiagnostics() const override;
    std::string getProgressDisplay() const override;

    void configure(const AudioStrategyConfig& config) override;
    void reset() override;
    std::string getModeString() const override;

private:
    ILogging* logger_;

    // Timing state for real measurements
    mutable std::atomic<double> lastRenderMs_{0.0};
    mutable std::atomic<double> lastHeadroomMs_{0.0};
    mutable std::atomic<double> lastBudgetPct_{0.0};
    mutable std::atomic<double> lastFrameBudgetPct_{100.0};
    mutable std::atomic<double> lastBufferTrendPct_{0.0};
    mutable std::atomic<double> lastCallbackIntervalMs_{250.0};
};

#endif // SYNC_PULL_STRATEGY_H
