// ThreadedStrategy.h - Cursor-chasing audio strategy (NEW STATE MODEL)
// Implements IAudioStrategy using new StrategyContext state model
// SRP: Single responsibility - only implements threaded cursor-chasing rendering
// OCP: New strategies can be added without modifying existing code
// DIP: Depends on state abstractions, not concrete implementations

#ifndef THREADED_STRATEGY_H
#define THREADED_STRATEGY_H

#include <memory>
#include <string>
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>

#include "audio/strategies/IAudioStrategy.h"
#include "audio/state/StrategyContext.h"
#include "audio/common/CircularBuffer.h"
#include "ILogging.h"

/**
 * ThreadedStrategy - Cursor-chasing audio strategy
 *
 * This strategy implements cursor-chasing audio generation where:
 * - Audio is generated in a separate thread (typically main loop)
 * - Generated audio is written to a circular buffer
 * - Real-time audio callback reads from buffer at playback cursor position
 * - Maintains ~100ms lead between generation and playback to prevent underruns
 *
 * Implementation details:
 * - Uses StrategyContext for all state management
 * - CircularBuffer is owned by StrategyContext (shared with strategies)
 * - Hardware feedback maintains target lead of 100ms
 * - Wrap-around buffer handling is supported
 *
 * SRP: Only implements threaded cursor-chasing rendering
 * OCP: New strategies can be added without modifying this code
 * DIP: Depends on StrategyContext abstraction, not concrete state
 */
class ThreadedStrategy : public IAudioStrategy {
public:
    /**
     * Constructor with optional logger injection
     * @param logger Optional logger for diagnostics. If nullptr, uses default logger.
     */
    explicit ThreadedStrategy(ILogging* logger = nullptr);

    // ================================================================
    // IAudioStrategy Implementation
    // ================================================================

    const char* getName() const override;

    bool isEnabled() const override;

    /**
     * Render audio from circular buffer for real-time playback
     *
     * This method is called by the hardware audio callback.
     * It reads frames from the circular buffer at the read pointer position.
     *
     * @param context Strategy context containing all audio state
     * @param ioData Audio buffer list to fill with samples
     * @param numberFrames Number of frames requested by hardware
     * @return true if rendering succeeded, false otherwise
     *
     * Behavior:
     * - Reads from circular buffer using read pointer position
     * - Updates read pointer for cursor-chasing
     * - Handles wrap-around correctly
     * - Fills with silence if buffer underruns
     * - Updates buffer status and diagnostics
     */
    bool render(
        StrategyContext* context,
        AudioBufferList* ioData,
        UInt32 numberFrames
    ) override;

    /**
     * Add audio frames to the circular buffer
     *
     * This method is called by the audio generation thread.
     * It writes frames to the circular buffer at the write pointer position.
     *
     * @param context Strategy context containing all audio state
     * @param buffer Audio samples to write (stereo interleaved)
     * @param frameCount Number of frames to write
     * @return true if frames were added successfully, false otherwise
     *
     * Behavior:
     * - Writes to circular buffer using write pointer position
     * - Updates write pointer with wrap-around
     * - Handles buffer overflow gracefully
     * - Logs warnings if buffer cannot accept all frames
     */
    bool AddFrames(
        StrategyContext* context,
        float* buffer,
        int frameCount
    ) override;

    // === Lifecycle Methods ===

    /**
     * Initialize the strategy with context and configuration
     * @param context Strategy context containing all audio state
     * @param config Strategy configuration parameters
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize(StrategyContext* context, const AudioStrategyConfig& config) override;

    /**
     * Prepare audio buffer for playback (pre-fill circular buffer)
     * @param context Strategy context containing all audio state
     */
    void prepareBuffer(StrategyContext* context) override;

    /**
     * Start playback and the engine's audio thread
     * @param context Strategy context containing all audio state
     * @param handle Engine simulator handle
     * @param api Engine simulator API
     * @return true if playback started successfully, false otherwise
     */
    bool startPlayback(StrategyContext* context, EngineSimHandle handle, const EngineSimAPI* api) override;

    /**
     * Stop playback and the engine's audio thread
     * @param context Strategy context containing all audio state
     * @param handle Engine simulator handle
     * @param api Engine simulator API
     */
    void stopPlayback(StrategyContext* context, EngineSimHandle handle, const EngineSimAPI* api) override;

    /**
     * Reset buffer state after warmup
     * @param context Strategy context containing all audio state
     */
    void resetBufferAfterWarmup(StrategyContext* context) override;

    /**
     * Update simulation state
     * @param context Strategy context containing all audio state
     * @param handle Engine simulator handle
     * @param api Engine simulator API
     * @param deltaTimeMs Time since last update in milliseconds
     */
    void updateSimulation(StrategyContext* context, EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) override;

    // === Strategy-Specific Methods ===

    /**
     * Return whether to drain buffer during warmup phase
     *
     * Threaded strategy should drain buffer during warmup to eliminate
     * warmup audio latency that was pre-filled.
     *
     * @return true - should drain during warmup
     */
    bool shouldDrainDuringWarmup() const override;

    /**
     * Get diagnostic information
     *
     * @return Human-readable diagnostic string
     */
    std::string getDiagnostics() const override;

    /**
     * Get strategy-specific progress display string
     * @return String containing strategy-specific progress output (empty if none)
     *
     * ThreadedStrategy: No specific progress output (buffer level shown by main loop)
     */
    std::string getProgressDisplay() const override;

    /**
     * Configure strategy with simulation parameters
     *
     * @param config Simulation configuration
     */
    void configure(const AudioStrategyConfig& config) override;

    /**
     * Reset strategy state
     *
     * This method resets the circular buffer state and diagnostics.
     * Useful for cleanup or re-initialization scenarios.
     */
    void reset() override;

    /**
     * Get mode string for diagnostics
     *
     * @return Mode description string
     */
    std::string getModeString() const override;

private:
    // ================================================================
    // Private Members
    // ================================================================

    /**
     * Logger for diagnostics
     * May be nullptr if no logger is injected.
     */
    ILogging* logger_;

    /**
     * Set strategy context
     * Called by AudioPlayer to provide context for configure/reset
     * @param context Strategy context to use
     */
    void setContext(StrategyContext* context);

    // ================================================================
    // Private Helper Methods
    // ================================================================

    /**
     * Calculate available frames in circular buffer
     *
     * This method determines how many frames are available to be read
     * by comparing write and read pointers in the circular buffer.
     *
     * @param context Strategy context containing buffer state
     * @return Number of frames available to read
     */
    int getAvailableFrames(const StrategyContext* context) const;

    /**
     * Read frames from circular buffer at specific position
     *
     * This method reads frames from the circular buffer at the
     * read pointer position (cursor-chasing behavior).
     * It handles wrap-around correctly when reading spans buffer boundary.
     * Does NOT update the internal read pointer.
     *
     * @param context Strategy context containing buffer state
     * @param output Output buffer to write frames to
     * @param framesToRead Number of frames to read
     */
    void readFromCircularBuffer(
        const StrategyContext* context,
        float* output,
        int framesToRead
    ) const;

    /**
     * Update diagnostics with buffer status
     *
     * @param context Strategy context containing diagnostics
     * @param availableFrames Number of frames available in buffer
     * @param framesRequested Number of frames requested by hardware
     */
    void updateDiagnostics(
        StrategyContext* context,
        int availableFrames,
        int framesRequested
    );
};

#endif // THREADED_STRATEGY_H
