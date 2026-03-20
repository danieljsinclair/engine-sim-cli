// SyncPullRenderer.cpp - Sync-pull renderer implementation
// Renders audio synchronously on-demand from the engine simulator

#include "audio/renderers/SyncPullRenderer.h"
#include <iomanip>
#include <chrono>
#include "AudioPlayer.h"

bool SyncPullRenderer::render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) {
    AudioUnitContext* context = static_cast<AudioUnitContext*>(ctx);
    
    if (!context || !context->syncPullAudio) {
        return false;
    }
    
    for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
        AudioBuffer& buffer = ioData->mBuffers[i];
        float* data = static_cast<float*>(buffer.mData);

        // Clamp frames to buffer capacity
        UInt32 framesToRender = numberFrames;
        if (framesToRender * 2 * sizeof(float) > buffer.mDataByteSize) {
            framesToRender = buffer.mDataByteSize / (2 * sizeof(float));
        }

        // Request frames from the engine simulator synchronously (with timing)
        auto start = std::chrono::high_resolution_clock::now();
        int framesRead = context->syncPullAudio->renderOnDemand(data, static_cast<int>(framesToRender));
        auto end = std::chrono::high_resolution_clock::now();
        double renderMs = std::chrono::duration<double, std::milli>(end - start).count();

        // Debug: show timing every ~2 seconds
        static int cbCount = 0; if (++cbCount % 100 == 0) {
            std::cout << "[SYNC-PULL] req=" << framesToRender << " got=" << framesRead 
                      << " time=" << std::fixed << std::setprecision(1) << renderMs << "ms\n";
        }

        // Report actual frames written to THIS buffer
        buffer.mDataByteSize = framesRead * 2 * sizeof(float);
    }
    
    return true;
}

bool SyncPullRenderer::isEnabled() const {
    // Will be checked via context in actual use
    return true;
}

bool SyncPullRenderer::AddFrames(void* ctx, float* buffer, int frameCount) {
    // Sync-pull renders on-demand in the callback, not via AddFrames
    // This method is a no-op - return true to indicate success
    (void)ctx;
    (void)buffer;
    (void)frameCount;
    return true;
}
