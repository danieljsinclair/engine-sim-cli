// CircularBufferRenderer.h - Circular buffer renderer
// Renders audio from a cursor-chasing circular buffer
// Uses hardware feedback to maintain 100ms lead, preventing underruns

#ifndef CIRCULAR_BUFFER_RENDERER_H
#define CIRCULAR_BUFFER_RENDERER_H

#include "audio/renderers/IAudioRenderer.h"

class CircularBufferRenderer : public IAudioRenderer {
public:
    bool render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) override;
    bool isEnabled() const override;
    const char* getName() const override { return "CircularBufferRenderer"; }
    bool AddFrames(void* ctx, float* buffer, int frameCount) override;
};

#endif // CIRCULAR_BUFFER_RENDERER_H
