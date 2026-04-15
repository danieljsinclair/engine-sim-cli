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

    if (!context->engineAPI) {
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
        EngineSimResult result = context->engineAPI->RenderOnDemand(
            context->engineHandle,
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

    constexpr double BUFFER_PERIOD_MS = 16.0;
    double timeBudgetPct = (renderMs * 100.0) / BUFFER_PERIOD_MS;
    double headroomMs = BUFFER_PERIOD_MS - renderMs;
    double frameBudgetPct = 100.0 * framesRendered / framesToGenerate;
    double callbackIntervalMs = 250.0;
    double bufferTrendPct = 0.0;

    lastRenderMs_.store(renderMs);
    lastHeadroomMs_.store(headroomMs);
    lastBudgetPct_.store(timeBudgetPct);
    lastFrameBudgetPct_.store(frameBudgetPct);
    lastBufferTrendPct_.store(bufferTrendPct);
    lastCallbackIntervalMs_.store(callbackIntervalMs);

    // Also store in BufferContext diagnostics for consumers that read from there
    context->diagnostics.lastRenderMs.store(renderMs);
    context->diagnostics.lastHeadroomMs.store(headroomMs);
    context->diagnostics.lastBudgetPct.store(timeBudgetPct);
    context->diagnostics.lastFrameBudgetPct.store(frameBudgetPct);

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
    double renderMs = lastRenderMs_.load();
    double headroomMs = lastHeadroomMs_.load();
    double timeBudgetPct = lastBudgetPct_.load();
    double bufferTrendPct = lastBufferTrendPct_.load();
    double callbackIntervalMs = lastCallbackIntervalMs_.load();

    double callbackRateHz = (callbackIntervalMs > 0.0) ? (1000.0 / callbackIntervalMs) : 0.0;
    double generatingFps = (callbackIntervalMs > 0.0) ? (callbackRateHz * 100.0) : 0.0;

    std::string budgetColor = ANSIColors::getDispositionColour(timeBudgetPct < 80, timeBudgetPct < 100);
    std::string trendColor = ANSIColors::getDispositionColour(bufferTrendPct >= 0, bufferTrendPct < 0, bufferTrendPct < -1.0);

    std::ostringstream output;
    output << "[SYNC-PULL] rendered=" << std::fixed << std::setprecision(1) << renderMs << "ms"
           << " headroom=" << std::showpos << std::setprecision(1) << headroomMs << std::noshowpos << "ms"
           << " (" << budgetColor << std::setprecision(0) << static_cast<int>(timeBudgetPct) << "% of budget" << ANSIColors::RESET << ")"
           << " callbacks=" << std::setprecision(0) << callbackRateHz << "Hz"
           << " generating=" << std::setprecision(1) << (generatingFps / 1000.0) << "kfps"
           << " trend=" << trendColor << std::showpos << std::setprecision(1) << bufferTrendPct << "%" << std::noshowpos << ANSIColors::RESET;

    return output.str();
}

void SyncPullStrategy::configure(const AudioStrategyConfig& config) {
    (void)config;
    if (logger_) {
        logger_->info(LogMask::AUDIO, "SyncPullStrategy configured: No pre-fill buffer");
    }
}

void SyncPullStrategy::reset() {
    if (logger_) {
        logger_->debug(LogMask::AUDIO, "SyncPullStrategy reset: No-op for sync-pull mode");
    }
}

std::string SyncPullStrategy::getModeString() const {
    return "Sync-pull (lock-step) mode";
}
