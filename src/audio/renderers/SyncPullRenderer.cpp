// SyncPullRenderer.cpp - Sync-pull renderer implementation
// Renders audio synchronously on-demand from the engine simulator
#include <chrono>
#include <iomanip>

#include "audio/renderers/SyncPullRenderer.h"
#include "ConsoleColors.h"
#include "AudioPlayer.h"

namespace {
    void collectSyncPullTiming(AudioUnitContext* context, UInt32 numberFrames, int framesRead,
                               double callbackMs, double budgetMs) {
        if (!context) return;

        double headroom = budgetMs - callbackMs;

        // Store timing data for main simulation loop to read
        context->lastReqFrames.store(static_cast<int>(numberFrames));
        context->lastGotFrames.store(framesRead);
        context->lastRenderMs.store(callbackMs);
        context->lastHeadroomMs.store(headroom);
    }
}

bool SyncPullRenderer::render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) {
    AudioUnitContext* context = static_cast<AudioUnitContext*>(ctx);
    
    if (!context || !context->syncPullAudio) {
        return false;
    }
    
    auto callbackStart = std::chrono::high_resolution_clock::now();

    int totalFramesRead = 0;
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

        // Report actual frames written to THIS buffer
        buffer.mDataByteSize = framesRead * 2 * sizeof(float);
        totalFramesRead += framesRead;
    }
    
    auto callbackEnd = std::chrono::high_resolution_clock::now();
    double callbackMs = std::chrono::duration<double, std::milli>(callbackEnd - callbackStart).count();
    
    // Estimate budget: at 44100Hz, callback interval depends on buffer size
    double budgetMs = (numberFrames * 1000.0) / 44100.0;

    // Collect timing data for main simulation loop to display
    collectSyncPullTiming(context, numberFrames, totalFramesRead, callbackMs, budgetMs);

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
