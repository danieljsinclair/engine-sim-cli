// AudioSource.cpp - Audio source implementation

#include "AudioSource.h"
#include "CLIconfig.h"
#include "ConsoleColors.h"
#include "AudioPlayer.h"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

// Template color function to avoid repetition
// use a lambfunction to capture the logic for determining color based on thresholds
std::string getDispositionColour(bool isGreen = false, bool isYellow = false, bool isRed = true) {
    if (isGreen) {
        return ANSIColors::GREEN;
    } else if (isYellow) {
        return ANSIColors::YELLOW;
    } else if (isRed) {
        return ANSIColors::RED;
    } else {
        return ANSIColors::RESET;
    }
}

// ============================================================================
// BaseAudioSource Implementation - DRY: common code for both modes
// ============================================================================

BaseAudioSource::BaseAudioSource(EngineSimHandle h, const EngineSimAPI& a)
    : handle_(h), api_(a) {
}

bool BaseAudioSource::generateAudio(std::vector<float>& buffer, int frames) {
    int totalRead = 0;
    api_.ReadAudioBuffer(handle_, buffer.data(), frames, &totalRead);
    return totalRead > 0;
}

void BaseAudioSource::updateStats(const EngineSimStats& stats) {
    (void)stats;
}

// ============================================================================
// EngineAudioSource Implementation
// ============================================================================

EngineAudioSource::EngineAudioSource(EngineSimHandle h, const EngineSimAPI& a)
    : BaseAudioSource(h, a) {
}

void EngineAudioSource::displayProgress(double currentTime, double duration, bool interactive, const EngineSimStats& stats, double throttle, int underrunCount, const AudioPlayer* audioPlayer) {
    int progress = static_cast<int>(currentTime * 100 / duration);
    std::ostringstream prefix;
    int rpm = static_cast<int>(stats.currentRPM);
    if (rpm < 10 && stats.currentRPM > 0) rpm = 0;
    prefix << "[" << std::setw(5) << rpm << " RPM] ";
    prefix << "[Throttle: " << std::setw(4) << static_cast<int>(throttle * 100) << "%] ";
    prefix << "[Underruns: " << underrunCount << "] ";
    prefix << ANSIColors::CYAN << "[Flow: " << std::fixed << std::showpos << std::setw(8) << std::setprecision(5) << stats.exhaustFlow << std::noshowpos << " m3/s]" << ANSIColors::RESET << " ";

    // Append SYNC-PULL timing data if available
    if (audioPlayer && audioPlayer->getContext()) {
        const AudioUnitContext* ctx = audioPlayer->getContext();

        // Check if pre-buffer was depleted
        if (ctx->preBufferDepleted.load()) {
            prefix << " [SYNC-PULL] Pre-buffer depleted - ";
        }

        // Get timing data
        int reqFrames = ctx->lastReqFrames.load();
        if (reqFrames > 0) {
            int gotFrames = ctx->lastGotFrames.load();
            double renderMs = ctx->lastRenderMs.load();
            double headroomMs = ctx->lastHeadroomMs.load();
            double timeBudgetPct = ctx->lastBudgetPct.load();
            double frameBudgetPct = ctx->lastFrameBudgetPct.load();
            double bufferTrendPct = ctx->lastBufferTrendPct.load();
            double callbackIntervalMs = ctx->lastCallbackIntervalMs.load();

            // Calculate callback rate and fps
            double callbackRateHz = (callbackIntervalMs > 0.0) ? (1000.0 / callbackIntervalMs) : 0.0;
            double neededFps = callbackRateHz * reqFrames;
            double generatingFps = (callbackIntervalMs > 0.0) ? (gotFrames * 1000.0 / callbackIntervalMs) : 0.0;

            // Color code based on metrics
            std::string reqGotColor = getDispositionColour(reqFrames == gotFrames);
            std::string budgetColor = getDispositionColour(timeBudgetPct < 80, timeBudgetPct < 100);
            std::string trendColor = getDispositionColour(bufferTrendPct >= 0, bufferTrendPct < 0, bufferTrendPct < -1.0);

            // Fixed-width formatting for column alignment
            prefix << "[SYNC-PULL] req=" << std::setw(3) << reqFrames
                    << " got=" << std::setw(3) << gotFrames
                    << " rendered=" << std::setw(5) << std::fixed << std::setprecision(1) << renderMs << "ms"
                    << " headroom=" << std::setw(5) << std::showpos << std::setprecision(1) << headroomMs << std::noshowpos << "ms"
                    << " (" << budgetColor << std::setw(3) << std::setprecision(0) << timeBudgetPct << "% of budget" << ANSIColors::RESET << ")"
                    << " callbacks=" << std::setw(6) << std::setprecision(0) << callbackRateHz << "Hz"
                    << " needed=" << std::setw(5) << std::setprecision(1) << neededFps / 1000.0 << "kfps"
                    << " generating=" << std::setw(5) << std::setprecision(1) << generatingFps / 1000.0 << "kfps"
                    << " trend=" << trendColor << std::showpos << std::setw(4) << std::setprecision(1) << bufferTrendPct << "%" << std::noshowpos << ANSIColors::RESET;
        }
    }

    DisplayHelper::outputProgress(interactive, prefix.str(), currentTime, duration, progress, stats, throttle, underrunCount);
}

// ============================================================================
// Shared Buffer Operations - DRY: same for both modes
// ============================================================================

namespace BufferOps {
    void preFillCircularBuffer(AudioPlayer* player) {
        if (!player) return;

        std::cout << ANSIColors::colorPreFill("Pre-filling audio buffer...") << "\n";
        std::vector<float> silence(AudioLoopConfig::FRAMES_PER_UPDATE * 2, 0.0f);

        for (int i = 0; i < AudioLoopConfig::PRE_FILL_ITERATIONS; i++) {
            player->addToCircularBuffer(silence.data(), AudioLoopConfig::FRAMES_PER_UPDATE);
        }

        std::cout << ANSIColors::colorPreFill("Buffer pre-filled:") << " " << (AudioLoopConfig::PRE_FILL_ITERATIONS * AudioLoopConfig::FRAMES_PER_UPDATE)
                  << " frames (" << (AudioLoopConfig::PRE_FILL_ITERATIONS / 60.0) << "s)\n";
    }

    void resetAndRePrefillBuffer(AudioPlayer* player) {
        if (!player) return;

        player->resetCircularBuffer();
        std::cout << ANSIColors::colorPreFill("Circular buffer reset after warmup") << "\n";

        // Re-pre-fill only if configured (0 iterations = no re-pre-fill)
        if (AudioLoopConfig::RE_PRE_FILL_ITERATIONS > 0) {
            std::vector<float> silence(AudioLoopConfig::FRAMES_PER_UPDATE * 2, 0.0f);
            for (int i = 0; i < AudioLoopConfig::RE_PRE_FILL_ITERATIONS; i++) {
                player->addToCircularBuffer(silence.data(), AudioLoopConfig::FRAMES_PER_UPDATE);
            }
            std::cout << ANSIColors::colorPreFill("Re-pre-filled:") << " " << (AudioLoopConfig::RE_PRE_FILL_ITERATIONS * AudioLoopConfig::FRAMES_PER_UPDATE)
                      << " frames (" << (AudioLoopConfig::RE_PRE_FILL_ITERATIONS / 60.0) << "s)\n";
        }
    }
}

// ============================================================================
// Shared warmup phase logic
// ============================================================================

namespace WarmupOps {
    void runWarmup(EngineSimHandle handle, const EngineSimAPI& api, AudioPlayer* audioPlayer, bool playAudio) {
        std::cout << ANSIColors::colorPreFill("Priming synthesizer pipeline (" + std::to_string(AudioLoopConfig::WARMUP_ITERATIONS) + " iterations)...") << "\n";

        double smoothedThrottle = 0.6;
        double currentTime = 0.0;

        for (int i = 0; i < AudioLoopConfig::WARMUP_ITERATIONS; i++) {
            EngineSimStats stats = {};
            api.GetStats(handle, &stats);

            api.SetThrottle(handle, smoothedThrottle);
            api.Update(handle, AudioLoopConfig::UPDATE_INTERVAL);

            currentTime += AudioLoopConfig::UPDATE_INTERVAL;

            std::cout << "  Priming: " << static_cast<int>(stats.currentRPM) << " RPM\n";

            // Drain audio if in play mode - DISCARD to prevent crackles
            // Warmup audio contains starter motor noise and transients
            // The buffer was pre-filled with silence, callback reads from that during warmup
            if (playAudio && audioPlayer) {
                std::vector<float> discardBuffer(AudioLoopConfig::FRAMES_PER_UPDATE * 2);
                int warmupRead = 0;

                for (int retry = 0; retry <= 3 && warmupRead < AudioLoopConfig::FRAMES_PER_UPDATE; retry++) {
                    int readThisTime = 0;
                    api.RenderOnDemand(handle,
                        discardBuffer.data() + warmupRead * 2,
                        AudioLoopConfig::FRAMES_PER_UPDATE - warmupRead,
                        &readThisTime);

                    if (readThisTime > 0) warmupRead += readThisTime;

                    if (warmupRead < AudioLoopConfig::FRAMES_PER_UPDATE && retry < 3) {
                        // No sleep - blocking prevents keyboard input
                    }
                }
                // DISCARD warmup audio - do NOT send to circular buffer
                // Buffer was pre-initialized with 100ms offset, so callback reads silence during warmup
            }
        }
    }
}

// ============================================================================
// Shared timing control
// ============================================================================

namespace TimingOps {
    LoopTimer::LoopTimer() : absoluteStartTime(std::chrono::steady_clock::now()), iterationCount(0) {}

    void LoopTimer::sleepToMaintain60Hz() {
        iterationCount++;
        auto now = std::chrono::steady_clock::now();
        auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(now - absoluteStartTime).count();
        auto targetUs = static_cast<long long>(iterationCount * AudioLoopConfig::UPDATE_INTERVAL * 1000000);
        auto sleepUs = targetUs - elapsedUs;

        if (sleepUs > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
        }
    }
}
