// SilentRenderer.cpp - Silent renderer implementation
// Renders silence - used as fallback when no valid mode is configured

#include "audio/renderers/SilentRenderer.h"
#include "AudioPlayer.h"

#include <cstring>

bool SilentRenderer::render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) {
    (void)ctx;
    (void)numberFrames;
    
    for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
        AudioBuffer& buffer = ioData->mBuffers[i];
        float* data = static_cast<float*>(buffer.mData);
        std::memset(data, 0, buffer.mDataByteSize);
    }
    return true;
}

bool SilentRenderer::isEnabled() const {
    return true;
}

bool SilentRenderer::AddFrames(void* ctx, float* buffer, int frameCount) {
    // SilentRenderer discards all frames - return true to indicate success
    (void)ctx;
    (void)buffer;
    (void)frameCount;
    return true;
}
