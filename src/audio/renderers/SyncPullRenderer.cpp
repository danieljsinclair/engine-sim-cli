// SyncPullRenderer.cpp - Sync-pull renderer implementation
// Renders audio synchronously on-demand from the engine simulator
#include <chrono>
#include <iomanip>

#include "audio/renderers/SyncPullRenderer.h"
#include "ConsoleColors.h"
#include "AudioPlayer.h"

namespace {
    // Constants for 16ms hardware buffer budget tracking
    constexpr double BUFFER_PERIOD_MS = 16.0;
    constexpr int FRAMES_PER_BUFFER(int sampleRate) {
        return static_cast<int>(sampleRate * BUFFER_PERIOD_MS / 1000.0);
    }
    // For 44.1kHz: 44100 * 0.016 = 705.6 frames per 16ms

    void collectSyncPullTiming(AudioUnitContext* context, UInt32 numberFrames, int framesRead,
                               double callbackMs) {
        if (!context) return;

        // Get current time
        auto now = std::chrono::high_resolution_clock::now();
        double nowMs = std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();

        // Check if we need to start a new 16ms window
        double windowStart = context->windowStartTimeMs.load();
        if (windowStart == 0.0 || (nowMs - windowStart >= BUFFER_PERIOD_MS)) {
            // Start new window
            context->windowStartTimeMs.store(nowMs);
            context->framesServedInWindow.store(0);
            windowStart = nowMs;
        }

        // Add frames served in this window (atomic add)
        int totalFramesInWindow = context->framesServedInWindow.fetch_add(framesRead) + framesRead;

        // Calculate metrics
        // 1. Time budget: how much of the 16ms period this callback took
        double timeBudgetPct = (callbackMs * 100.0) / BUFFER_PERIOD_MS;
        double headroomMs = BUFFER_PERIOD_MS - callbackMs;

        // 2. Frame budget: how many frames we've served in the current 16ms window
        int maxFramesForBuffer = FRAMES_PER_BUFFER(context->sampleRate);
        double frameBudgetPct = (static_cast<double>(totalFramesInWindow) * 100.0) / maxFramesForBuffer;

        // Store timing data for main simulation loop to read
        context->lastReqFrames.store(static_cast<int>(numberFrames));
        context->lastGotFrames.store(framesRead);
        context->lastRenderMs.store(callbackMs);
        context->lastHeadroomMs.store(headroomMs);
        context->lastBudgetPct.store(timeBudgetPct);  // Store time budget for display
        context->lastFrameBudgetPct.store(frameBudgetPct);  // Store frame budget separately
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

    // Collect timing data for main simulation loop to display
    // Tracks budget across the entire 16ms hardware buffer period
    collectSyncPullTiming(context, numberFrames, totalFramesRead, callbackMs);

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
