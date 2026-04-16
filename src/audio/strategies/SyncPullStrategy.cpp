// SyncPullStrategy.cpp - Lock-step audio strategy
// Implements synchronous audio generation where simulation advances with audio playback
// SRP: Single responsibility - only implements synchronous lock-step rendering
// OCP: New strategies can be added without modifying existing code
// DIP: Depends on abstractions (ISimulator), not concrete implementations

#include "audio/strategies/SyncPullStrategy.h"
#include "audio/strategies/IAudioStrategy.h"
#include "simulation/ISimulator.h"
#include "ILogging.h"
#include "Verification.h"

// ============================================================================
// SyncPullStrategy Implementation
// ============================================================================

SyncPullStrategy::SyncPullStrategy(ILogging* logger)
    : defaultLogger_(logger ? nullptr : new ConsoleLogger())
    , logger_(logger ? logger : defaultLogger_.get())
{
    ASSERT(logger_, "SyncPullStrategy: logger must not be null");
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

void SyncPullStrategy::fillBufferFromEngine(ISimulator*, int) {
    // SyncPull generates audio on-demand in the render callback via ISimulator.
}

// ============================================================================
// Lifecycle Method Implementations
// ============================================================================

bool SyncPullStrategy::initialize(const AudioStrategyConfig& config) {
    ASSERT(logger_, "SyncPullStrategy::initialize: logger must not be null");

    audioState_.sampleRate = config.sampleRate;
    audioState_.isPlaying = false;

    logger_->info(LogMask::AUDIO,
                  "SyncPullStrategy initialized: sampleRate=%dHz, channels=%d",
                  config.sampleRate, config.channels);

    return true;
}

void SyncPullStrategy::prepareBuffer() {
    logger_->debug(LogMask::AUDIO, "SyncPullStrategy::prepareBuffer: No-op for sync-pull mode");
}

bool SyncPullStrategy::startPlayback(ISimulator* simulator) {
    simulator_ = simulator;
    audioState_.isPlaying.store(true);

    logger_->info(LogMask::AUDIO, "SyncPullStrategy::startPlayback: On-demand rendering started");

    return true;
}

void SyncPullStrategy::stopPlayback(ISimulator* simulator) {
    simulator_ = nullptr;
    audioState_.isPlaying.store(false);

    logger_->info(LogMask::AUDIO, "SyncPullStrategy::stopPlayback: On-demand rendering stopped");
}

void SyncPullStrategy::resetBufferAfterWarmup() {
    logger_->debug(LogMask::AUDIO, "SyncPullStrategy::resetBufferAfterWarmup: No-op for sync-pull mode");
}

void SyncPullStrategy::updateSimulation(ISimulator* simulator, double deltaTimeMs) {
    // Sync-pull mode updates simulation during render callback
}

bool SyncPullStrategy::render(
    AudioBufferList* ioData,
    UInt32 numberFrames
) {
    if (!ioData) {
        return false;
    }

    if (!simulator_) {
        return false;
    }

    auto callbackStart = std::chrono::high_resolution_clock::now();

    int framesToGenerate = static_cast<int>(numberFrames);
    int framesRendered = 0;

    float* audioData = static_cast<float*>(ioData->mBuffers[0].mData);
    int remainingFrames = framesToGenerate;

    while (remainingFrames > 0 && framesRendered < framesToGenerate) {
        int32_t framesWritten = 0;
        bool result = simulator_->renderOnDemand(
            audioData + (framesRendered * 2),
            remainingFrames,
            &framesWritten
        );

        if (!result) {
            logger_->error(LogMask::AUDIO, "SyncPullStrategy::render: renderOnDemand failed");
            return false;
        }

        framesRendered += framesWritten;
        remainingFrames -= framesWritten;

        if (framesWritten == 0 && remainingFrames > 0) {
            logger_->warning(LogMask::AUDIO, "SyncPullStrategy::render: renderOnDemand returned 0 frames, breaking loop");
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
    logger_->debug(LogMask::AUDIO, "SyncPullStrategy::AddFrames: No-op for sync-pull mode");
    return true;
}

void SyncPullStrategy::reset() {
    logger_->debug(LogMask::AUDIO, "SyncPullStrategy reset: No-op for sync-pull mode");
}

std::string SyncPullStrategy::getModeString() const {
    return "Sync-pull (lock-step) mode";
}
