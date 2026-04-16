// SyncPullStrategy.cpp - Lock-step audio strategy
// Implements synchronous audio generation where simulation advances with audio playback
// SRP: Single responsibility - only implements synchronous lock-step rendering
// OCP: New strategies can be added without modifying existing code
// DIP: Depends on abstractions, not concrete implementations

#include "audio/strategies/SyncPullStrategy.h"
#include "audio/strategies/IAudioStrategy.h"
#include "ILogging.h"
#include "config/ANSIColors.h"
#include <sstream>
#include <iomanip>

// ============================================================================
// SyncPullStrategy Implementation
// ============================================================================

SyncPullStrategy::SyncPullStrategy(ILogging* logger)
    : logger_(logger)
{
}

// ============================================================================
// IAudioStrategy Implementation
// ============================================================================

const char* SyncPullStrategy::getName() const {
    return "SyncPull";
}

bool SyncPullStrategy::isEnabled() const {
    return true;
}

bool SyncPullStrategy::isPlaying() const {
    return audioState_.isPlaying.load();
}

bool SyncPullStrategy::shouldDrainDuringWarmup() const {
    return false;
}

void SyncPullStrategy::fillBufferFromEngine(EngineSimHandle, const EngineSimAPI&, int) {
    // SyncPull generates audio on-demand in the render callback via RenderOnDemand.
}

// ============================================================================
// Lifecycle Method Implementations
// ============================================================================

bool SyncPullStrategy::initialize(const AudioStrategyConfig& config) {
    audioState_.sampleRate = config.sampleRate;
    audioState_.isPlaying = false;

    engineHandle_ = config.engineHandle;
    engineAPI_ = config.engineAPI;

    if (logger_) {
        logger_->info(LogMask::AUDIO,
                      "SyncPullStrategy initialized: sampleRate=%dHz, channels=%d",
                      config.sampleRate, config.channels);
    }

    return true;
}

void SyncPullStrategy::prepareBuffer() {
    if (logger_) {
        logger_->debug(LogMask::AUDIO, "SyncPullStrategy::prepareBuffer: No-op for sync-pull mode");
    }
}

bool SyncPullStrategy::startPlayback(EngineSimHandle handle, const EngineSimAPI* api) {
    audioState_.isPlaying.store(true);

    if (logger_) {
        logger_->info(LogMask::AUDIO, "SyncPullStrategy::startPlayback: On-demand rendering started");
    }

    return true;
}

void SyncPullStrategy::stopPlayback(EngineSimHandle handle, const EngineSimAPI* api) {
    audioState_.isPlaying.store(false);

    if (logger_) {
        logger_->info(LogMask::AUDIO, "SyncPullStrategy::stopPlayback: On-demand rendering stopped");
    }
}

void SyncPullStrategy::resetBufferAfterWarmup() {
    if (logger_) {
        logger_->debug(LogMask::AUDIO, "SyncPullStrategy::resetBufferAfterWarmup: No-op for sync-pull mode");
    }
}

void SyncPullStrategy::updateSimulation(EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) {
    // Sync-pull mode updates simulation during render callback
}

bool SyncPullStrategy::render(
    AudioBufferList* ioData,
    UInt32 numberFrames
) {
    if (!ioData) {
        return false;
    }

    if (!engineAPI_) {
        return false;
    }

    auto callbackStart = std::chrono::high_resolution_clock::now();

    int framesToGenerate = static_cast<int>(numberFrames);
    int framesRendered = 0;

    float* audioData = static_cast<float*>(ioData->mBuffers[0].mData);
    int remainingFrames = framesToGenerate;

    while (remainingFrames > 0 && framesRendered < framesToGenerate) {
        int32_t framesWritten = 0;
        EngineSimResult result = engineAPI_->RenderOnDemand(
            engineHandle_,
            audioData + (framesRendered * 2),
            remainingFrames,
            &framesWritten
        );

        if (result != 0) {
            if (logger_) {
                logger_->error(LogMask::AUDIO, "SyncPullStrategy::render: RenderOnDemand failed (result=%d)", result);
            }
            return false;
        }

        framesRendered += framesWritten;
        remainingFrames -= framesWritten;

        if (framesWritten == 0 && remainingFrames > 0) {
            if (logger_) {
                logger_->warning(LogMask::AUDIO, "SyncPullStrategy::render: RenderOnDemand returned 0 frames, breaking loop");
            }
            break;
        }
    }

    auto callbackEnd = std::chrono::high_resolution_clock::now();
    double renderMs = std::chrono::duration<double, std::milli>(callbackEnd - callbackStart).count();

    diagnostics_.recordRender(renderMs, framesRendered);

    return true;
}

bool SyncPullStrategy::AddFrames(
    float* buffer,
    int frameCount
) {
    if (logger_) {
        logger_->debug(LogMask::AUDIO, "SyncPullStrategy::AddFrames: No-op for sync-pull mode");
    }
    return true;
}

std::string SyncPullStrategy::getDiagnostics() const {
    std::string diagnostics = "SyncPullStrategy Diagnostics:\n";
    diagnostics += "- Mode: Lock-step (synchronous) mode\n";
    diagnostics += "- Audio generation: On-demand from simulator\n";
    diagnostics += "- Synchronization: Simulation advances with audio playback\n";
    return diagnostics;
}

std::string SyncPullStrategy::getProgressDisplay() const {
    auto snap = diagnostics_.getSnapshot();

    std::string budgetColor = ANSIColors::getDispositionColour(snap.lastBudgetPct < 80, snap.lastBudgetPct < 100);

    std::ostringstream output;
    output << "[SYNC-PULL] rendered=" << std::fixed << std::setprecision(1) << snap.lastRenderMs << "ms"
           << " headroom=" << std::showpos << std::setprecision(1) << snap.lastHeadroomMs << std::noshowpos << "ms"
           << " (" << budgetColor << std::setprecision(0) << static_cast<int>(snap.lastBudgetPct) << "% of budget" << ANSIColors::RESET << ")";

    return output.str();
}

void SyncPullStrategy::reset() {
    if (logger_) {
        logger_->debug(LogMask::AUDIO, "SyncPullStrategy reset: No-op for sync-pull mode");
    }
}

std::string SyncPullStrategy::getModeString() const {
    return "Sync-pull (lock-step) mode";
}
