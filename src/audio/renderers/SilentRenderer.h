// SilentRenderer.h - Silent renderer
// Renders silence - used when no valid mode is configured

#ifndef SILENT_RENDERER_H
#define SILENT_RENDERER_H

#include "audio/renderers/IAudioRenderer.h"

class SilentRenderer : public IAudioRenderer {
public:
    bool render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) override;
    bool isEnabled() const override;
    const char* getName() const override { return "SilentRenderer"; }
    bool AddFrames(void* ctx, float* buffer, int frameCount) override;
};

#endif // SILENT_RENDERER_H
