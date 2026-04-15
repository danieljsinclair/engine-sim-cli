// IAudioStrategy.h - Audio strategy interface
// Strategy pattern - abstracts audio generation strategy behavior
// SRP: Single responsibility for audio generation strategy
// OCP: New strategies can be added without modifying core code
// DI: Strategy is injected via IAudioStrategyFactory

#ifndef IAUDIO_STRATEGY_H
#define IAUDIO_STRATEGY_H

#include <memory>
#include <string>

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>

#include "engine_sim_bridge.h"
#include "bridge/engine_sim_loader.h"
#include "ILogging.h"

// Forward declarations
struct BufferContext;

// Simulation configuration
struct AudioStrategyConfig {
    int sampleRate;
    int channels;
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
     * Render audio to the provided buffer list
     * Called by the real-time audio callback.
     */
    virtual bool render(
        BufferContext* context,
        AudioBufferList* ioData,
        UInt32 numberFrames
    ) = 0;

    /**
     * Add frames to the strategy's internal buffer
     * Called by the main simulation loop.
     */
    virtual bool AddFrames(BufferContext* context, float* buffer, int frameCount) = 0;

    // === Lifecycle Methods ===

    virtual bool initialize(BufferContext* context, const AudioStrategyConfig& config) = 0;
    virtual void prepareBuffer(BufferContext* context) = 0;

    virtual bool startPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) = 0;
    virtual void stopPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) = 0;
    virtual void resetBufferAfterWarmup(BufferContext* context) = 0;

    // === Strategy-Specific Methods ===

    virtual bool shouldDrainDuringWarmup() const = 0;
    virtual std::string getDiagnostics() const = 0;
    virtual std::string getProgressDisplay() const = 0;
    virtual void configure(const AudioStrategyConfig& config) = 0;
    virtual void reset() = 0;
    virtual std::string getModeString() const = 0;

    virtual void updateSimulation(BufferContext* context, EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) = 0;
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
