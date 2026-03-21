// AudioSource.cpp - Audio source implementation
// DRY: BaseAudioSource contains common generateAudio() using SineGenerator

#include "AudioSource.h"
#include "CLIconfig.h"
#include "ConsoleColors.h"
#include "AudioPlayer.h"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

// ============================================================================
// BaseAudioSource Implementation - DRY: common code for both modes
// ============================================================================

BaseAudioSource::BaseAudioSource(EngineSimHandle h, const EngineSimAPI& a, bool useSine)
    : handle_(h), api_(a), currentRPM_(0.0), useSineGenerator_(useSine) {
    // Initialize SineGenerator with sample rate
    if (useSineGenerator_) {
        sineGenerator_.initialize(AudioLoopConfig::SAMPLE_RATE);
    }
}

bool BaseAudioSource::generateAudio(std::vector<float>& buffer, int frames) {
    // DRY: Single implementation for both sync-pull and threaded modes
    // Uses SineGenerator when in sine mode, otherwise reads from engine buffer
    
    if (useSineGenerator_ && sineGenerator_.isInitialized()) {
        // Use SineGenerator for sine wave generation - DRY between modes
        // Frequency is derived from RPM: 600 RPM = 100 Hz, 6000 RPM = 1000 Hz
        sineGenerator_.generateRPMLinked(buffer.data(), frames, currentRPM_, 0.5);
        return true;
    }
    
    // Fallback: read from engine audio buffer
    int totalRead = 0;
    api_.ReadAudioBuffer(handle_, buffer.data(), frames, &totalRead);
    return totalRead > 0;
}

void BaseAudioSource::updateStats(const EngineSimStats& stats) {
    // Update RPM for sine generation - needed for threaded mode
    currentRPM_ = stats.currentRPM;
}

// ============================================================================
// SineAudioSource Implementation
// ============================================================================

SineAudioSource::SineAudioSource(EngineSimHandle h, const EngineSimAPI& a)
    : BaseAudioSource(h, a, true) {  // true = use SineGenerator
}

void SineAudioSource::displayProgress(double currentTime, double duration, bool interactive, const EngineSimStats& stats, double throttle, int underrunCount) {
    // Update RPM for sine generation
    setCurrentRPM(stats.currentRPM);
    
    int progress = static_cast<int>(currentTime * 100 / duration);
    double frequency = (stats.currentRPM / 600.0) * 100.0;
    
    std::ostringstream prefix;
    prefix << ANSIColors::CYAN << "[Frequency: " << static_cast<int>(frequency) << " Hz] " << ANSIColors::RESET;
    int rpm = static_cast<int>(stats.currentRPM);
    if (rpm < 10 && stats.currentRPM > 0) rpm = 0;
    prefix << "[" << std::setw(5) << rpm << " RPM] ";
    prefix << "[Throttle: " << std::setw(4) << static_cast<int>(throttle * 100) << "%] ";
    prefix << "[Underruns: " << underrunCount << "] ";
    
    DisplayHelper::outputProgress(interactive, prefix.str(), currentTime, duration, progress, stats, throttle, underrunCount);
}

// ============================================================================
// EngineAudioSource Implementation
// ============================================================================

EngineAudioSource::EngineAudioSource(EngineSimHandle h, const EngineSimAPI& a)
    : BaseAudioSource(h, a, false) {  // false = use engine API
}

void EngineAudioSource::displayProgress(double currentTime, double duration, bool interactive, const EngineSimStats& stats, double throttle, int underrunCount) {
    int progress = static_cast<int>(currentTime * 100 / duration);
    std::ostringstream prefix;
    int rpm = static_cast<int>(stats.currentRPM);
    if (rpm < 10 && stats.currentRPM > 0) rpm = 0;
    prefix << "[" << std::setw(5) << rpm << " RPM] ";
    prefix << "[Throttle: " << std::setw(4) << static_cast<int>(throttle * 100) << "%] ";
    prefix << "[Underruns: " << underrunCount << "] ";
    prefix << "[Flow: " << std::fixed << std::showpos << std::setw(8) << std::setprecision(5) << stats.exhaustFlow << std::noshowpos << " m3/s] ";
    
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
        auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
            now - absoluteStartTime).count();
        auto targetUs = static_cast<long long>(iterationCount * AudioLoopConfig::UPDATE_INTERVAL * 1000000);
        auto sleepUs = targetUs - elapsedUs;

        if (sleepUs > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
        }
    }
}
