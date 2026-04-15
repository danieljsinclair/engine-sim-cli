// AudioSource.cpp - Audio source implementation

#include "AudioSource.h"
#include "config/CLIconfig.h"
#include "config/ANSIColors.h"
#include "AudioPlayer.h"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

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

void outputProgress(bool interactive, const std::string& prefix,
    double currentTime, double duration, int progress,
    const EngineSimStats& stats, double throttle, int underrunCount) {
    (void)currentTime;
    (void)duration;
    if (interactive) {
        std::cout << prefix << "\n" << std::flush;
    } else {
        static int lastProgress = 0;
        if (progress != lastProgress && progress % 10 == 0) {
            // Include prefix (which may contain SYNC-PULL timing data) in non-interactive mode
            std::cout << prefix << "  Progress: " << progress << "% | RPM: " << static_cast<int>(stats.currentRPM)
                        << " | Throttle: " << static_cast<int>(throttle * 100) << "%"
                        << " | Underruns: " << underrunCount << "\r" << std::flush;
            lastProgress = progress;
        }
    }
}

void EngineAudioSource::displayProgress(double currentTime, double duration, bool interactive, const EngineSimStats& stats, double throttle, int underrunCount, const AudioPlayer* audioPlayer) {
    int progress = static_cast<int>(currentTime * 100 / duration);
    std::ostringstream prefix;
    int rpm = static_cast<int>(stats.currentRPM);
    if (rpm < 10 && stats.currentRPM > 0) rpm = 0;
    prefix << "[" << std::setw(5) << rpm << " RPM] ";
    prefix << "[Throttle: " << std::setw(4) << static_cast<int>(throttle * 100) << "%] ";
    prefix << "[Underruns: " << underrunCount << "] ";
    prefix << ANSIColors::INFO << "[Flow: " << std::fixed << std::showpos << std::setw(8) << std::setprecision(5) << stats.exhaustFlow << std::noshowpos << " m3/s]" << ANSIColors::RESET << " ";

    // Append SYNC-PULL timing data if available
    if (audioPlayer && audioPlayer->getContext()) {
        const BufferContext* ctx = audioPlayer->getContext();

        // Read diagnostics from BufferContext (populated by SyncPullStrategy::render)
        double renderMs = ctx->diagnostics.lastRenderMs.load();
        double headroomMs = ctx->diagnostics.lastHeadroomMs.load();
        double timeBudgetPct = ctx->diagnostics.lastBudgetPct.load();

        if (renderMs > 0.0) {
            double bufferTrendPct = 0.0;
            double callbackIntervalMs = 250.0;

            double callbackRateHz = (callbackIntervalMs > 0.0) ? (1000.0 / callbackIntervalMs) : 0.0;

            std::string budgetColor = ANSIColors::getDispositionColour(timeBudgetPct < 80, timeBudgetPct < 100);
            std::string trendColor = ANSIColors::getDispositionColour(bufferTrendPct >= 0, bufferTrendPct < 0, bufferTrendPct < -1.0);

            prefix << "[SYNC-PULL] rendered=" << std::setw(5) << std::fixed << std::setprecision(1) << renderMs << "ms"
                    << " headroom=" << std::setw(5) << std::showpos << std::setprecision(1) << headroomMs << std::noshowpos << "ms"
                    << " (" << budgetColor << std::setw(3) << std::setprecision(0) << timeBudgetPct << "% of budget" << ANSIColors::RESET << ")"
                    << " callbacks=" << std::setw(6) << std::setprecision(0) << callbackRateHz << "Hz"
                    << " trend=" << trendColor << std::showpos << std::setw(4) << std::setprecision(1) << bufferTrendPct << "%" << std::noshowpos << ANSIColors::RESET;
        }
    }

    outputProgress(interactive, prefix.str(), currentTime, duration, progress, stats, throttle, underrunCount);
}

// ============================================================================
// Shared Buffer Operations - DRY: same for both modes
// ============================================================================

namespace BufferOps {
    void preFillCircularBuffer(AudioPlayer* player) {
        if (!player) return;

        std::vector<float> silence(AudioLoopConfig::FRAMES_PER_UPDATE * 2, 0.0f);

        for (int i = 0; i < AudioLoopConfig::PRE_FILL_ITERATIONS; i++) {
            player->addToCircularBuffer(silence.data(), AudioLoopConfig::FRAMES_PER_UPDATE);
        }
    }

    void resetAndRePrefillBuffer(AudioPlayer* player) {
        if (!player) return;

        player->resetCircularBuffer();

        // Re-pre-fill only if configured (0 iterations = no re-pre-fill)
        if (AudioLoopConfig::RE_PRE_FILL_ITERATIONS > 0) {
            std::vector<float> silence(AudioLoopConfig::FRAMES_PER_UPDATE * 2, 0.0f);
            for (int i = 0; i < AudioLoopConfig::RE_PRE_FILL_ITERATIONS; i++) {
                player->addToCircularBuffer(silence.data(), AudioLoopConfig::FRAMES_PER_UPDATE);
            }
        }
    }
}

// ============================================================================
// Shared warmup phase logic
// ============================================================================

namespace WarmupOps {
    void runWarmup(EngineSimHandle handle, const EngineSimAPI& api, AudioPlayer* audioPlayer, bool playAudio) {
        double smoothedThrottle = 0.6;
        double currentTime = 0.0;

        for (int i = 0; i < AudioLoopConfig::WARMUP_ITERATIONS; i++) {
            EngineSimStats stats = {};
            api.GetStats(handle, &stats);

            api.SetThrottle(handle, smoothedThrottle);
            api.Update(handle, AudioLoopConfig::UPDATE_INTERVAL);

            currentTime += AudioLoopConfig::UPDATE_INTERVAL;

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
