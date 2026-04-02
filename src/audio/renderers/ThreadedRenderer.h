// ThreadedRenderer.h - Threaded/cursor-chasing renderer
// Renders audio from a cursor-chasing circular buffer
// Uses hardware feedback to maintain 100ms lead, preventing underruns

#ifndef THREADED_RENDERER_H
#define THREADED_RENDERER_H

#include "audio/renderers/IAudioRenderer.h"

class ThreadedRenderer : public IAudioRenderer {
public:
    bool render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) override;
    bool isEnabled() const override;
    const char* getName() const override { return "ThreadedRenderer"; }
    bool AddFrames(void* ctx, float* buffer, int frameCount) override;
};

#endif // THREADED_RENDERER_H
