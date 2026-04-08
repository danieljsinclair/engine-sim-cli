// IAudioStrategy.h - Audio strategy interface (Option B: IAudioStrategy + IAudioHardwareProvider)
// Strategy pattern - abstracts audio generation strategy behavior
// SRP: Single responsibility for audio generation strategy
// OCP: New strategies can be added without modifying core code
// DI: Strategy is injected via AudioStrategyFactory

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
class AudioPlayer;
class StrategyContext;
class IAudioSource;

// Simulation configuration (minimal for current implementation)
struct AudioStrategyConfig {  // Renamed from SimulationConfig to avoid conflict
    int sampleRate;
    int channels;
    // Additional config fields can be added as needed
};

// Audio mode enumeration
enum class AudioMode {
    Threaded,    // Cursor-chasing mode with separate audio thread
    SyncPull     // Lock-step mode where simulation advances with audio playback
};

/**
 * IAudioStrategy - Interface for audio generation strategies
 *
 * This interface defines the contract for audio generation strategies.
 * Each strategy implements a different approach to generating audio:
 * - ThreadedStrategy: Cursor-chasing mode with separate audio thread
 * - SyncPullStrategy: Lock-step mode where simulation advances with audio playback
 *
 * Responsibilities:
 * - Generate audio based on strategy-specific logic
 * - Handle rendering to audio buffer
 * - Strategy-specific initialization and configuration
 * - Provide strategy diagnostics
 */
class IAudioStrategy {
public:
    virtual ~IAudioStrategy() = default;

    // === Core Strategy Methods ===

    /**
     * Get the name of this strategy for diagnostics
     * @return Strategy name (e.g., "ThreadedStrategy", "SyncPullStrategy")
     */
    virtual const char* getName() const = 0;

    /**
     * Check if this strategy is enabled/active
     * @return true if strategy is enabled, false otherwise
     */
    virtual bool isEnabled() const = 0;

    /**
     * Render audio to the provided buffer list
     * @param context Strategy context containing all audio state
     * @param ioData Audio buffer list to render into
     * @param numberFrames Number of frames to render
     * @return true if rendering succeeded, false otherwise
     */
    virtual bool render(
        StrategyContext* context,
        AudioBufferList* ioData,
        UInt32 numberFrames
    ) = 0;

    /**
     * Add frames to the strategy's internal buffer
     * @param context Strategy context containing all audio state
     * @param buffer Audio data to add
     * @param frameCount Number of frames in the buffer
     * @return true if frames were added successfully, false otherwise
     */
    virtual bool AddFrames(StrategyContext* context, float* buffer, int frameCount) = 0;

    // === Strategy-Specific Methods ===

    /**
     * Check if this strategy needs to drain during warmup
     * @return true if strategy should drain during warmup, false otherwise
     *
     * ThreadedStrategy: true (needs to drain pre-filled buffer)
     * SyncPullStrategy: false (no pre-fill buffer)
     */
    virtual bool shouldDrainDuringWarmup() const = 0;

    /**
     * Get strategy-specific diagnostic information
     * @return String containing strategy diagnostics
     */
    virtual std::string getDiagnostics() const = 0;

    /**
     * Get strategy-specific progress display string
     * @return String containing strategy-specific progress output (empty if none)
     *
     * Each strategy can provide its own specialized progress output:
     * - ThreadedStrategy: Buffer level, cursor position, underruns
     * - SyncPullStrategy: Render timing, headroom, budget, callbacks
     */
    virtual std::string getProgressDisplay() const = 0;

    /**
     * Configure the strategy with simulation parameters
     * @param config Simulation configuration
     */
    virtual void configure(const AudioStrategyConfig& config) = 0;

    /**
     * Reset strategy state when simulation restarts
     */
    virtual void reset() = 0;

    /**
     * Get strategy-specific mode string for display
     * @return Mode string for user display
     */
    virtual std::string getModeString() const = 0;
};

/**
 * Factory for creating audio strategies
 *
 * This factory creates the appropriate strategy based on the requested AudioMode.
 * New strategies can be added by extending the factory switch statement.
 */
class IAudioStrategyFactory {
public:
    /**
     * Create an audio strategy for the specified mode
     * @param mode Audio mode to create strategy for
     * @param logger Optional logger for diagnostics
     * @return Unique pointer to the created strategy, or nullptr if mode is unknown
     */
    static std::unique_ptr<IAudioStrategy> createStrategy(
        AudioMode mode,
        ILogging* logger = nullptr
    );
};

#endif // IAUDIO_STRATEGY_H