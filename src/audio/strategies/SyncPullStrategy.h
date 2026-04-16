// SyncPullStrategy.h - Lock-step audio strategy
// Implements synchronous audio generation where simulation advances with audio playback
// SRP: Single responsibility - only implements synchronous lock-step rendering
// OCP: New strategies can be added without modifying existing code
// DIP: Depends on abstractions (ISimulator), not concrete implementations
// Owns its own state: AudioState, Diagnostics

#ifndef SYNC_PULL_STRATEGY_H
#define SYNC_PULL_STRATEGY_H

#include <string>
#include <atomic>
#include <memory>

#include "audio/strategies/IAudioStrategy.h"
#include "audio/state/AudioState.h"
#include "audio/state/Diagnostics.h"
#include "ILogging.h"

class ISimulator;

/**
 * SyncPullStrategy - Lock-step audio generation strategy
 *
 * Engine simulation advances in sync with audio playback.
 * Audio is rendered on-demand synchronously from the simulator via ISimulator.
 * No separate audio thread is needed.
 *
 * Owns its own state: AudioState, Diagnostics.
 */
class SyncPullStrategy : public IAudioStrategy {
public:
    explicit SyncPullStrategy(ILogging* logger = nullptr);

    const char* getName() const override;
    bool isEnabled() const override;
    bool isPlaying() const override;
    bool shouldDrainDuringWarmup() const override;
    void fillBufferFromEngine(ISimulator* simulator, int defaultFramesPerUpdate) override;

    bool render(AudioBufferList* ioData, UInt32 numberFrames) override;
    bool AddFrames(float* buffer, int frameCount) override;

    // Lifecycle Methods
    bool initialize(const AudioStrategyConfig& config) override;
    void prepareBuffer() override;
    bool startPlayback(ISimulator* simulator) override;
    void stopPlayback(ISimulator* simulator) override;
    void resetBufferAfterWarmup() override;
    void updateSimulation(ISimulator* simulator, double deltaTimeMs) override;

    void reset() override;
    std::string getModeString() const override;

    // Returns render timing diagnostics for presentation
    const Diagnostics& diagnostics() const { return diagnostics_; }
    Diagnostics::Snapshot getDiagnosticsSnapshot() const override { return diagnostics_.getSnapshot(); }

private:
    // Logger: always non-null (defaults to ConsoleLogger if not injected)
    std::unique_ptr<ILogging> defaultLogger_;
    ILogging* logger_;

    // Owned state
    AudioState audioState_;
    Diagnostics diagnostics_;

    // Simulator reference (set during startPlayback)
    ISimulator* simulator_ = nullptr;
};

#endif // SYNC_PULL_STRATEGY_H
