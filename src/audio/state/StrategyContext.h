// StrategyContext.h - Composed context for audio strategies
// SRP: Single responsibility - composes audio state components
// OCP: New strategies can add state without modifying this composer
// DIP: High-level modules depend on this abstraction

#ifndef STRATEGY_CONTEXT_H
#define STRATEGY_CONTEXT_H

#include "audio/state/AudioState.h"
#include "audio/state/BufferState.h"
#include "audio/state/Diagnostics.h"
#include "audio/common/CircularBuffer.h"
#include "engine_sim_bridge.h"
#include "bridge/engine_sim_loader.h"

#include <memory>

// Forward declarations
class IAudioStrategy;

/**
 * StrategyContext - Composed context for audio strategies
 *
 * This struct replaces the massive SRP violation of AudioUnitContext
 * by composing focused state structs with clear separation of concerns.
 *
 * Composition:
 * - AudioState: Core playback state (playing, sampleRate)
 * - BufferState: Circular buffer management (pointers, counters)
 * - Diagnostics: Performance and timing metrics
 * - CircularBuffer: Actual audio data buffer
 * - Strategy Reference: Current rendering strategy
 *
 * Responsibilities:
 * - Provide unified context for IAudioStrategy implementations
 * - Maintain clear separation between audio, buffer, and diagnostic concerns
 * - Enable dependency injection of state components
 * - Support thread-safe operations where needed
 *
 * SRP: Only composes and manages state components, doesn't contain logic
 * DIP: Strategies depend on this composed context, not individual components
 */
struct StrategyContext {
    /**
     * Audio playback state
     */
    AudioState audioState;

    /**
     * Buffer state management
     */
    BufferState bufferState;

    /**
     * Performance and timing diagnostics
     */
    Diagnostics diagnostics;

    /**
     * Circular buffer for audio data storage
     * Shared between strategies for cursor-chasing mode
     * NOTE: This is a non-owning pointer; ownership is by AudioUnitContext
     */
    CircularBuffer* circularBuffer;

    /**
     * Current rendering strategy
     * Used by strategies to know which mode they're operating in
     * - nullptr: No strategy set yet
     */
    IAudioStrategy* strategy;

    /**
     * Engine simulator handle
     * - Required for sync-pull mode to generate audio
     * - Shared across strategies
     */
    EngineSimHandle engineHandle;

    /**
     * Engine simulator API
     * - Required for sync-pull mode to call RenderOnDemand
     * - Shared across strategies
     */
    const EngineSimAPI* engineAPI;

    /**
     * Initialize with default values
     */
    StrategyContext()
        : circularBuffer(nullptr)
        , strategy(nullptr)
        , engineHandle(nullptr)
        , engineAPI(nullptr)
    {}

    /**
     * Reset all state components
     * Useful for cleanup or re-initialization scenarios
     */
    void reset() {
        audioState.reset();
        bufferState.reset();
        diagnostics.reset();
    }

    /**
     * Reset only audio and buffer state
     * Useful when keeping diagnostics across runs
     */
    void resetAudioAndBuffer() {
        audioState.reset();
        bufferState.reset();
    }
};

#endif // STRATEGY_CONTEXT_H
