// IAudioStrategy.h - Audio strategy interface
// Strategy pattern - abstracts audio generation strategy behavior
// SRP: Single responsibility for audio generation strategy
// OCP: New strategies can be added without modifying core code
// DI: Strategy is injected via IAudioStrategyFactory

#ifndef IAUDIO_STRATEGY_H
#define IAUDIO_STRATEGY_H

#include <memory>
#include <string>
#include <atomic>

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>

#include "engine_sim_bridge.h"
#include "bridge/engine_sim_loader.h"
#include "audio/state/Diagnostics.h"
#include "ILogging.h"

// Simulation configuration
struct AudioStrategyConfig {
    int sampleRate;
    int channels;
    EngineSimHandle engineHandle = nullptr;
    const EngineSimAPI* engineAPI = nullptr;
};

// Audio mode enumeration
enum class AudioMode {
    Threaded,    // Cursor-chasing mode with separate audio thread
    SyncPull     // Lock-step mode where simulation advances with audio playback
};

/**
 * IAudioStrategy - Interface for audio generation strategies
 *
 * Each strategy implements a different approach to generating audio:
 * - ThreadedStrategy: Cursor-chasing mode with separate audio thread
 * - SyncPullStrategy: Lock-step mode where simulation advances with audio playback
 *
 * SRP: Strategy has single responsibility for audio generation approach
 * OCP: New strategies extend this interface without modification
 */
class IAudioStrategy {
public:
    virtual ~IAudioStrategy() = default;

    // === Core Strategy Methods ===

    virtual const char* getName() const = 0;
    virtual bool isEnabled() const = 0;

    /**
     * Check if the strategy is currently playing audio.
     * Thread-safe: can be called from any thread (e.g. audio callback).
     */
    virtual bool isPlaying() const = 0;

    /**
     * Render audio to the provided buffer list.
     * Called by the real-time audio callback.
     */
    virtual bool render(
        AudioBufferList* ioData,
        UInt32 numberFrames
    ) = 0;

    /**
     * Add frames to the strategy's internal buffer.
     * Called by the main simulation loop.
     */
    virtual bool AddFrames(float* buffer, int frameCount) = 0;

    // === Lifecycle Methods ===

    virtual bool initialize(const AudioStrategyConfig& config) = 0;
    virtual void prepareBuffer() = 0;

    virtual bool startPlayback(EngineSimHandle handle, const EngineSimAPI* api) = 0;
    virtual void stopPlayback(EngineSimHandle handle, const EngineSimAPI* api) = 0;
    virtual void resetBufferAfterWarmup() = 0;

    // === Strategy-Specific Methods ===

    virtual bool shouldDrainDuringWarmup() const = 0;

    /**
     * Fill internal buffer with audio from the engine.
     * Called by the main simulation loop each iteration.
     * ThreadedStrategy: reads from engine via ReadAudioBuffer, applies cursor chasing.
     * SyncPullStrategy: no-op (audio is generated on-demand in render callback).
     */
    virtual void fillBufferFromEngine(EngineSimHandle handle, const EngineSimAPI& api, int defaultFramesPerUpdate) = 0;

    virtual std::string getDiagnostics() const = 0;
    virtual std::string getProgressDisplay() const = 0;
    virtual void reset() = 0;
    virtual std::string getModeString() const = 0;

    virtual void updateSimulation(EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) = 0;

    // Temporary: returns render timing diagnostics snapshot (Phase C moves to telemetry-only)
    virtual Diagnostics::Snapshot getDiagnosticsSnapshot() const = 0;
};

/**
 * Factory for creating audio strategies.
 * OCP: New strategies can be added by extending the factory.
 */
class IAudioStrategyFactory {
public:
    static std::unique_ptr<IAudioStrategy> createStrategy(
        AudioMode mode,
        ILogging* logger = nullptr
    );
};

#endif // IAUDIO_STRATEGY_H
