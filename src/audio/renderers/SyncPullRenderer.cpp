// SyncPullRenderer.cpp - Sync-pull renderer implementation
// Renders audio synchronously on-demand from the engine simulator
#include <iomanip>
#include <chrono>

#include "audio/renderers/SyncPullRenderer.h"
#include "ConsoleColors.h"
#include "AudioPlayer.h"

namespace {
    void logSyncPullTiming(UInt32 numberFrames, AudioBufferList* ioData, double callbackMs, double budgetMs, double latency) {
        double budgetPct = callbackMs / budgetMs * 100;

        std::string budgetColor;
        if (budgetPct < 80) {
            budgetColor = ANSIColors::GREEN;
        } else if (budgetPct <= 100) {
            budgetColor = ANSIColors::YELLOW;
        } else {
            budgetColor = ANSIColors::RED;
        }

        std::string headroomColor;
        if (latency < -1.0) {
            headroomColor = ANSIColors::GREEN;
        } else if (latency <= 0.0) {
            headroomColor = ANSIColors::YELLOW;
        } else {
            headroomColor = ANSIColors::RED;
        }

        static int cbCount = 0;
        if (++cbCount % 100 == 0 || headroomColor == ANSIColors::RED || budgetColor == ANSIColors::RED) {  // Log every 100 callbacks or on critical latency

            std::cout << "[SYNC-PULL] req=" << numberFrames << " got=" << (ioData->mBuffers[0].mDataByteSize / (2 * sizeof(float)))
                    << " render=" << std::fixed << std::setprecision(1) << callbackMs << "ms"
                    << " headroom=" << headroomColor << std::showpos << std::setprecision(1) << latency << std::noshowpos << "ms" << ANSIColors::RESET
                    << " (" << budgetColor << std::setprecision(0) << budgetPct << "%" << ANSIColors::RESET << " of budget)\n";
        }
    }
}

bool SyncPullRenderer::render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) {
    AudioUnitContext* context = static_cast<AudioUnitContext*>(ctx);
    
    if (!context || !context->syncPullAudio) {
        return false;
    }
    
    auto callbackStart = std::chrono::high_resolution_clock::now();
    
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
    }
    
    auto callbackEnd = std::chrono::high_resolution_clock::now();
    double callbackMs = std::chrono::duration<double, std::milli>(callbackEnd - callbackStart).count();
    
    // Estimate budget: at 44100Hz, callback interval depends on buffer size
    // Typical is 1024 frames at 44100Hz = ~23.2ms
    // Latency: lead time - how much buffer margin we have before the audio callback deadline.
    // Positive = we finished with time to spare, Negative = we exceeded the budget (audio glitch risk)
    double budgetMs = (numberFrames * 1000.0) / 44100.0;
    double latency = callbackMs - budgetMs;

    logSyncPullTiming(numberFrames, ioData, callbackMs, budgetMs, latency);
    
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
