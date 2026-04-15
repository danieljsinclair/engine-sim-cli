// BufferContext.h - Composed context for audio strategies
// SRP: Single responsibility - composes audio state components
// OCP: New strategies can add state without modifying this composer
// DIP: High-level modules depend on this abstraction

#ifndef BUFFER_CONTEXT_H
#define BUFFER_CONTEXT_H

#include "audio/state/AudioState.h"
#include "audio/state/Diagnostics.h"
#include "audio/common/CircularBuffer.h"

/**
 * BufferContext - Composed context for audio strategies
 *
 * Contains shared playback state used by both strategies:
 * - AudioState: Core playback state (playing, sampleRate)
 * - Diagnostics: Performance and timing metrics
 * - CircularBuffer: Audio data buffer (owned externally, e.g. by AudioPlayer)
 *
 * Buffer tracking (pointers, underruns) is managed directly by CircularBuffer.
 * Engine-specific state (handle, API) is owned by the strategies that need it.
 */
struct BufferContext {
    AudioState audioState;

    Diagnostics diagnostics;

    /**
     * Circular buffer for audio data storage.
     * Non-owning pointer; ownership belongs to the AudioPlayer.
     */
    CircularBuffer* circularBuffer;

    BufferContext()
        : circularBuffer(nullptr)
    {}

    void reset() {
        audioState.reset();
        diagnostics.reset();
    }
};

#endif // BUFFER_CONTEXT_H
