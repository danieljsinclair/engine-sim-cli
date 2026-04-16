// SyncPullStrategy.cpp - Lock-step audio strategy
// Implements synchronous audio generation where simulation advances with audio playback
// SRP: Single responsibility - only implements synchronous lock-step rendering
// OCP: New strategies can be added without modifying existing code
// DIP: Depends on state abstractions, not concrete implementations

#include "audio/strategies/SyncPullStrategy.h"
#include "audio/strategies/IAudioStrategy.h"
#include "audio/state/BufferContext.h"
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

bool SyncPullStrategy::shouldDrainDuringWarmup() const {
    return false;
}

void SyncPullStrategy::fillBufferFromEngine(BufferContext*, EngineSimHandle, const EngineSimAPI&, int) {
    // SyncPull generates audio on-demand in the render callback via RenderOnDemand.
    // No main-thread audio generation needed.
}

// ============================================================================
// Lifecycle Method Implementations
// ============================================================================

bool SyncPullStrategy::initialize(BufferContext* context, const AudioStrategyConfig& config) {
    if (!context) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "SyncPullStrategy::initialize: Invalid context");
        }
        return false;
    }

    context->audioState.sampleRate = config.sampleRate;
    context->audioState.isPlaying = false;

    // Store engine connection from config
    engineHandle_ = config.engineHandle;
    engineAPI_ = config.engineAPI;

    if (logger_) {
        logger_->info(LogMask::AUDIO,
                      "SyncPullStrategy initialized: sampleRate=%dHz, channels=%d",
                      config.sampleRate, config.channels);
    }

    return true;
}

void SyncPullStrategy::prepareBuffer(BufferContext* context) {
    (void)context;
    if (logger_) {
        logger_->debug(LogMask::AUDIO, "SyncPullStrategy::prepareBuffer: No-op for sync-pull mode");
    }
}

bool SyncPullStrategy::startPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) {
    if (!context) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "SyncPullStrategy::startPlayback: Invalid context");
        }
        return false;
    }

    context->audioState.isPlaying.store(true);

    if (logger_) {
        logger_->info(LogMask::AUDIO, "SyncPullStrategy::startPlayback: On-demand rendering started");
    }

    return true;
}

void SyncPullStrategy::stopPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) {
    if (!context) {
        if (logger_) {
            logger_->warning(LogMask::AUDIO, "SyncPullStrategy::stopPlayback: Invalid context");
        }
        return;
    }

    context->audioState.isPlaying.store(false);

    if (logger_) {
        logger_->info(LogMask::AUDIO, "SyncPullStrategy::stopPlayback: On-demand rendering stopped");
    }

    (void)handle;
    (void)api;
}

void SyncPullStrategy::resetBufferAfterWarmup(BufferContext* context) {
    (void)context;
    if (logger_) {
        logger_->debug(LogMask::AUDIO, "SyncPullStrategy::resetBufferAfterWarmup: No-op for sync-pull mode");
    }
}

void SyncPullStrategy::updateSimulation(BufferContext* context, EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) {
    (void)context;
    (void)handle;
    (void)api;
    (void)deltaTimeMs;
    // Sync-pull mode updates simulation during render callback
}

bool SyncPullStrategy::render(
    BufferContext* context,
    AudioBufferList* ioData,
    UInt32 numberFrames
) {
    if (!context || !ioData) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "SyncPullStrategy::render: Invalid context or buffer");
        }
        return false;
    }

    if (!engineAPI_) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "SyncPullStrategy::render: Engine API not initialized");
        }
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

    // Calculate real timing metrics
    auto callbackEnd = std::chrono::high_resolution_clock::now();
    double renderMs = std::chrono::duration<double, std::milli>(callbackEnd - callbackStart).count();

    // Store in both strategy-local diagnostics and context diagnostics
    diagnostics_.recordRender(renderMs, framesRendered);
    context->diagnostics.recordRender(renderMs, framesRendered);

    return true;
}

bool SyncPullStrategy::AddFrames(
    BufferContext* context,
    float* buffer,
    int frameCount
) {
    (void)context;
    (void)buffer;
    (void)frameCount;

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
    diagnostics += "- Buffer management: No circular buffer used\n";
    diagnostics += "- Pre-fill: No pre-fill buffer needed\n";
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
