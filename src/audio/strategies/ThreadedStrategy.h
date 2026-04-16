// ThreadedStrategy.h - Cursor-chasing audio strategy
// SRP: Single responsibility - only implements threaded cursor-chasing rendering
// OCP: New strategies can be added without modifying existing code
// DIP: Depends on abstractions, not concrete implementations
// Owns its own state: AudioState, Diagnostics, CircularBuffer

#ifndef THREADED_STRATEGY_H
#define THREADED_STRATEGY_H

#include <string>
#include <memory>
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>

#include "audio/strategies/IAudioStrategy.h"
#include "audio/common/CircularBuffer.h"
#include "audio/state/AudioState.h"
#include "audio/state/Diagnostics.h"
#include "ITelemetryProvider.h"
#include "ILogging.h"

/**
 * ThreadedStrategy - Cursor-chasing audio strategy
 *
 * Audio is generated in the main loop, written to an internal circular buffer,
 * and read by the real-time audio callback at playback cursor position.
 * Maintains ~100ms lead between generation and playback.
 *
 * Owns its own state: AudioState, Diagnostics, CircularBuffer.
 */
class ThreadedStrategy : public IAudioStrategy {
public:
    explicit ThreadedStrategy(ILogging* logger = nullptr,
                              telemetry::ITelemetryWriter* telemetry = nullptr);

    // IAudioStrategy Implementation
    const char* getName() const override;
    bool isEnabled() const override;
    bool isPlaying() const override;

    bool render(AudioBufferList* ioData, UInt32 numberFrames) override;
    bool AddFrames(float* buffer, int frameCount) override;

    // Lifecycle Methods
    bool initialize(const AudioStrategyConfig& config) override;
    void prepareBuffer() override;
    bool startPlayback(EngineSimHandle handle, const EngineSimAPI* api) override;
    void stopPlayback(EngineSimHandle handle, const EngineSimAPI* api) override;
    void resetBufferAfterWarmup() override;
    void updateSimulation(EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) override;

    // Strategy-Specific Methods
    bool shouldDrainDuringWarmup() const override;
    void fillBufferFromEngine(EngineSimHandle handle, const EngineSimAPI& api, int defaultFramesPerUpdate) override;
    void reset() override;
    std::string getModeString() const override;

    // Returns render timing diagnostics for presentation
    const Diagnostics& diagnostics() const { return diagnostics_; }
    Diagnostics::Snapshot getDiagnosticsSnapshot() const override { return diagnostics_.getSnapshot(); }

private:
    // Logger: always non-null (defaults to ConsoleLogger if not injected)
    std::unique_ptr<ILogging> defaultLogger_;
    ILogging* logger_;

    // Telemetry: always non-null (defaults to NullTelemetryWriter if not injected)
    std::unique_ptr<telemetry::ITelemetryWriter> defaultTelemetry_;
    telemetry::ITelemetryWriter* telemetry_;

    // Owned state (previously in BufferContext)
    AudioState audioState_;
    Diagnostics diagnostics_;
    CircularBuffer circularBuffer_;

    // Internal underrun tracking
    int underrunCount_ = 0;

    int getAvailableFrames() const;
    void updateDiagnostics(int availableFrames, int framesRequested);
    void publishAudioDiagnostics(int underrunCount, double bufferHealthPct);
};

#endif // THREADED_STRATEGY_H
