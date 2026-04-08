// SyncPullStrategy.cpp - Lock-step audio strategy (NEW STATE MODEL)
// Implements synchronous audio generation where simulation advances with audio playback
// SRP: Single responsibility - only implements synchronous lock-step rendering
// OCP: New strategies can be added without modifying existing code
// DIP: Depends on state abstractions, not concrete implementations

#include "audio/strategies/SyncPullStrategy.h"
#include "audio/strategies/IAudioStrategy.h"
#include "audio/state/StrategyContext.h"
#include "AudioPlayer.h"  // For AudioUnitContext forward declaration
#include "ILogging.h"
#include "config/ANSIColors.h"
#include <sstream>
#include <iomanip>

// ============================================================================
// SyncPullStrategy Implementation
// ============================================================================

SyncPullStrategy::SyncPullStrategy(ILogging* logger)
    : logger_(logger)
    , audioUnitContext_(nullptr)
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

bool SyncPullStrategy::render(
    StrategyContext* context,
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

    // Measure render time for real timing metrics
    auto callbackStart = std::chrono::high_resolution_clock::now();

    // For sync-pull mode, we render audio on-demand from engine simulator
    // Use the engine API to render audio directly
    int framesToGenerate = static_cast<int>(numberFrames);
    int framesRendered = 0;

    // Loop until we get all requested frames
    // RenderOnDemand may return fewer frames than requested if the synthesizer
    // doesn't have enough audio buffered, so we need to keep calling it
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

        if (result != 0) {  // ESIM_SUCCESS = 0
            if (logger_) {
                logger_->error(LogMask::AUDIO, "SyncPullStrategy::render: RenderOnDemand failed (result=%d)", result);
            }
            return false;
        }

        framesRendered += framesWritten;
        remainingFrames -= framesWritten;

        // Safety: prevent infinite loop if we're not making progress
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

    // Constants for timing calculations
    constexpr double BUFFER_PERIOD_MS = 16.0;  // 16ms hardware buffer period

    // Calculate timing metrics
    double timeBudgetPct = (renderMs * 100.0) / BUFFER_PERIOD_MS;
    double headroomMs = BUFFER_PERIOD_MS - renderMs;
    double frameBudgetPct = 100.0 * framesRendered / framesToGenerate;

    // For callback interval, use a reasonable estimate (4Hz = 250ms)
    // In a real implementation, we'd track the actual callback intervals
    double callbackIntervalMs = 250.0;
    double bufferTrendPct = 0.0;  // No buffer trend for sync-pull mode

    // Store timing metrics for progress display
    lastRenderMs_.store(renderMs);
    lastHeadroomMs_.store(headroomMs);
    lastBudgetPct_.store(timeBudgetPct);
    lastFrameBudgetPct_.store(frameBudgetPct);
    lastBufferTrendPct_.store(bufferTrendPct);
    lastCallbackIntervalMs_.store(callbackIntervalMs);

    // Update AudioUnitContext metrics for legacy compatibility
    // These are read by AudioSource::displayProgress() for SYNC-PULL output
    if (audioUnitContext_) {
        audioUnitContext_->lastReqFrames.store(framesToGenerate);
        audioUnitContext_->lastGotFrames.store(framesRendered);
        audioUnitContext_->lastRenderMs.store(renderMs);
        audioUnitContext_->lastHeadroomMs.store(headroomMs);
        audioUnitContext_->lastBudgetPct.store(timeBudgetPct);
        audioUnitContext_->lastFrameBudgetPct.store(frameBudgetPct);
        audioUnitContext_->lastBufferTrendPct.store(bufferTrendPct);
        audioUnitContext_->lastCallbackIntervalMs.store(callbackIntervalMs);
    }

    return true;
}

bool SyncPullStrategy::AddFrames(
    StrategyContext* context,
    float* buffer,
    int frameCount
) {
    // For sync-pull mode, AddFrames is a no-op since audio is
    // generated on-demand by the engine simulator.
    (void)context;  // Suppress unused parameter warning
    (void)buffer;  // Suppress unused parameter warning
    (void)frameCount;  // Suppress unused parameter warning

    if (logger_) {
        logger_->debug(LogMask::AUDIO, "SyncPullStrategy::AddFrames: Called but no-op for sync-pull mode");
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
    // Get real timing metrics
    double renderMs = lastRenderMs_.load();
    double headroomMs = lastHeadroomMs_.load();
    double timeBudgetPct = lastBudgetPct_.load();
    double frameBudgetPct = lastFrameBudgetPct_.load();
    double bufferTrendPct = lastBufferTrendPct_.load();
    double callbackIntervalMs = lastCallbackIntervalMs_.load();

    // Calculate callback rate and fps
    double callbackRateHz = (callbackIntervalMs > 0.0) ? (1000.0 / callbackIntervalMs) : 0.0;
    double generatingFps = (callbackIntervalMs > 0.0) ? (callbackRateHz * 100.0) : 0.0;  // frames per second

    // Color code based on metrics
    std::string budgetColor = ANSIColors::getDispositionColour(timeBudgetPct < 80, timeBudgetPct < 100);
    std::string trendColor = ANSIColors::getDispositionColour(bufferTrendPct >= 0, bufferTrendPct < 0, bufferTrendPct < -1.0);

    // Build progress display string
    std::ostringstream output;
    output << "[SYNC-PULL] rendered=" << std::fixed << std::setprecision(1) << renderMs << "ms"
           << " headroom=" << std::showpos << std::setprecision(1) << headroomMs << std::noshowpos << "ms"
           << " (" << budgetColor << std::setprecision(0) << static_cast<int>(timeBudgetPct) << "% of budget" << ANSIColors::RESET << ")"
           << " callbacks=" << std::setprecision(0) << callbackRateHz << "Hz"
           << " generating=" << std::setprecision(1) << (generatingFps / 1000.0) << "kfps"
           << " trend=" << trendColor << std::showpos << std::setprecision(1) << bufferTrendPct << "%" << std::noshowpos << ANSIColors::RESET;

    return output.str();
}

void SyncPullStrategy::configure(const ::AudioStrategyConfig& config) {
    // Sync pull strategy doesn't need pre-fill configuration
    // The buffer is filled on-demand from the simulator
    (void)config;  // Suppress unused parameter warning
    if (logger_) {
        logger_->info(LogMask::AUDIO, "SyncPullStrategy configured: No pre-fill buffer");
    }
}

void SyncPullStrategy::reset() {
    // Sync pull strategy doesn't have much state to reset
    if (logger_) {
        logger_->debug(LogMask::AUDIO, "SyncPullStrategy reset: No-op for sync-pull mode");
    }
}

std::string SyncPullStrategy::getModeString() const {
    return "Sync-pull (lock-step) mode";
}

void SyncPullStrategy::setAudioUnitContext(struct AudioUnitContext* audioUnitContext) {
    audioUnitContext_ = audioUnitContext;
}
