// SyncPullStrategy.h - Lock-step audio strategy (NEW STATE MODEL)
// Implements synchronous audio generation where simulation advances with audio playback
// SRP: Single responsibility - only implements synchronous lock-step rendering
// OCP: New strategies can be added without modifying existing code
// DIP: Depends on state abstractions, not concrete implementations

#ifndef SYNC_PULL_STRATEGY_H
#define SYNC_PULL_STRATEGY_H

#include <memory>
#include <string>
#include <atomic>

#include "audio/strategies/IAudioStrategy.h"
#include "audio/state/StrategyContext.h"
#include "ILogging.h"

// Forward declaration to avoid circular dependency
struct AudioUnitContext;

/**
 * SyncPullStrategy - Lock-step audio generation strategy
 *
 * This strategy implements lock-step audio generation where:
 * - Engine simulation advances in sync with audio playback
 * - Audio is rendered on-demand synchronously from engine simulator
 *
 * Implementation details:
 * - Calls engine API directly for audio generation
 * - Handles synchronous rendering to audio buffer
 * - Measures real timing metrics for progress display
 * - No separate audio thread is needed
 *
 * SRP: Only implements synchronous lock-step rendering
 * OCP: New strategies can be added without modifying this code
 * DIP: Depends on StrategyContext abstraction, not concrete state
 */
class SyncPullStrategy : public IAudioStrategy {
public:
    explicit SyncPullStrategy(ILogging* logger = nullptr);

    const char* getName() const override;
    bool isEnabled() const override;
    bool shouldDrainDuringWarmup() const override;

    bool render(
        StrategyContext* context,
        AudioBufferList* ioData,
        UInt32 numberFrames
    ) override;

    bool AddFrames(
        StrategyContext* context,
        float* buffer,
        int frameCount
    ) override;

    std::string getDiagnostics() const override;
    std::string getProgressDisplay() const override;

    void configure(const ::AudioStrategyConfig& config) override;
    void reset() override;
    std::string getModeString() const override;

    void setAudioUnitContext(struct AudioUnitContext* audioUnitContext);

private:
    ILogging* logger_;
    struct AudioUnitContext* audioUnitContext_;

    // Timing state for real measurements
    mutable std::atomic<double> lastRenderMs_{0.0};
    mutable std::atomic<double> lastHeadroomMs_{0.0};
    mutable std::atomic<double> lastBudgetPct_{0.0};
    mutable std::atomic<double> lastFrameBudgetPct_{100.0};
    mutable std::atomic<double> lastBufferTrendPct_{0.0};
    mutable std::atomic<double> lastCallbackIntervalMs_{250.0};
};

#endif // SYNC_PULL_STRATEGY_H
