// BufferContext.h - Composed context for audio strategies
// SRP: Single responsibility - composes audio state components
// OCP: New strategies can add state without modifying this composer
// DIP: High-level modules depend on this abstraction

#ifndef BUFFER_CONTEXT_H
#define BUFFER_CONTEXT_H

#include "audio/state/AudioState.h"
#include "audio/state/BufferState.h"
#include "audio/state/Diagnostics.h"
#include "audio/common/CircularBuffer.h"
#include "engine_sim_bridge.h"
#include "bridge/engine_sim_loader.h"

// Forward declarations
class IAudioStrategy;

/**
 * BufferContext - Composed context for audio strategies
 *
 * Replaces the monolithic AudioUnitContext god struct by composing
 * focused state structs with clear separation of concerns.
 *
 * Composition:
 * - AudioState: Core playback state (playing, sampleRate)
 * - BufferState: Circular buffer management (pointers, counters)
 * - Diagnostics: Performance and timing metrics
 * - CircularBuffer: Actual audio data buffer
 * - Strategy Reference: Current rendering strategy
 * - Engine handles: Simulator connection for on-demand rendering
 *
 * SRP: Only composes and manages state components, doesn't contain logic
 * DIP: Strategies depend on this composed context, not individual components
 */
struct BufferContext {
    AudioState audioState;

    BufferState bufferState;

    Diagnostics diagnostics;

    /**
     * Circular buffer for audio data storage.
     * Non-owning pointer; ownership belongs to the AudioPlayer.
     */
    CircularBuffer* circularBuffer;

    /**
     * Current rendering strategy.
     * nullptr until set by AudioPlayer.
     */
    IAudioStrategy* strategy;

    /**
     * Engine simulator handle.
     * Required for sync-pull mode to generate audio.
     */
    EngineSimHandle engineHandle;

    /**
     * Engine simulator API.
     * Required for sync-pull mode to call RenderOnDemand.
     */
    const EngineSimAPI* engineAPI;

    BufferContext()
        : circularBuffer(nullptr)
        , strategy(nullptr)
        , engineHandle(nullptr)
        , engineAPI(nullptr)
    {}

    void reset() {
        audioState.reset();
        bufferState.reset();
        diagnostics.reset();
    }

    void resetAudioAndBuffer() {
        audioState.reset();
        bufferState.reset();
    }
};

#endif // BUFFER_CONTEXT_H
