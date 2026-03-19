// SyncPullRenderer.h - Sync-pull renderer
// Renders audio on-demand synchronously from the engine simulator
// Used when the caller controls timing and requests frames as needed

#ifndef SYNC_PULL_RENDERER_H
#define SYNC_PULL_RENDERER_H

#include "audio/renderers/IAudioRenderer.h"

class SyncPullRenderer : public IAudioRenderer {
public:
    bool render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) override;
    bool isEnabled() const override;
    const char* getName() const override { return "SyncPullRenderer"; }
    bool AddFrames(void* ctx, float* buffer, int frameCount) override;
};

#endif // SYNC_PULL_RENDERER_H
