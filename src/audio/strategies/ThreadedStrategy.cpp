// ThreadedStrategy.cpp - Cursor-chasing audio strategy (NEW STATE MODEL)
// Implements IAudioStrategy using new StrategyContext state model
// SRP: Single responsibility - only implements threaded cursor-chasing rendering
// OCP: New strategies can be added without modifying existing code
// DIP: Depends on state abstractions, not concrete implementations

#include "audio/strategies/ThreadedStrategy.h"
#include "ILogging.h"
#include "config/ANSIColors.h"

#include <cstring>
#include <algorithm>
#include <chrono>

using namespace std::chrono;

// ============================================================================
// ThreadedStrategy Implementation
// ============================================================================

ThreadedStrategy::ThreadedStrategy(ILogging* logger)
    : logger_(logger) {
    // Note: logger may be nullptr - handle with null checks
}

// ============================================================================
// IAudioStrategy Implementation
// ============================================================================

const char* ThreadedStrategy::getName() const {
    return "Threaded";
}

bool ThreadedStrategy::isEnabled() const {
    return true;
}

bool ThreadedStrategy::shouldDrainDuringWarmup() const {
    return true;
}

bool ThreadedStrategy::render(
    StrategyContext* context,
    AudioBufferList* ioData,
    UInt32 numberFrames
) {
    if (!context || !ioData) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "ThreadedStrategy::render: Invalid context or buffer");
        }
        return false;
    }

    // Check if circular buffer is initialized
    if (!context->circularBuffer || !context->circularBuffer->isInitialized()) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "ThreadedStrategy::render: Circular buffer not initialized");
        }
        return false;
    }

    // Check if we have enough frames available
    int availableFrames = getAvailableFrames(context);
    int framesToRead = std::min(static_cast<int>(numberFrames), availableFrames);

    if (framesToRead <= 0) {
        // No frames available - output silence
        if (logger_) {
            logger_->warning(LogMask::AUDIO, "ThreadedStrategy::render: No frames available, outputting silence");
        }
        std::memset(ioData->mBuffers[0].mData, 0, ioData->mBuffers[0].mDataByteSize);
        return true;
    }

    // Read frames from circular buffer (managed internally by CircularBuffer)
    size_t framesRead = context->circularBuffer->read(static_cast<float*>(ioData->mBuffers[0].mData), framesToRead);

    // Calculate target lead (100ms at current sample rate)
    int targetLead = static_cast<int>(context->audioState.sampleRate * 0.1);

    // Update diagnostics
    updateDiagnostics(context, availableFrames, numberFrames);

    return true;
}

bool ThreadedStrategy::AddFrames(
    StrategyContext* context,
    float* buffer,
    int frameCount
) {
    if (!context || !buffer || frameCount <= 0) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "ThreadedStrategy::AddFrames: Invalid parameters");
        }
        return false;
    }

    if (!context->circularBuffer || !context->circularBuffer->isInitialized()) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "ThreadedStrategy::AddFrames: Circular buffer not initialized");
        }
        return false;
    }

    // Write frames to circular buffer (managed internally by CircularBuffer)
    size_t framesWritten = context->circularBuffer->write(buffer, frameCount);

    if (framesWritten != static_cast<size_t>(frameCount)) {
        if (logger_) {
            logger_->warning(LogMask::AUDIO, "ThreadedStrategy::AddFrames: Only wrote %zu/%d frames",
                          framesWritten, frameCount);
        }
    }

    return true;
}

std::string ThreadedStrategy::getDiagnostics() const {
    std::string diagnostics = "ThreadedStrategy Diagnostics:\n";
    diagnostics += "- Mode: Threaded audio generation\n";
    diagnostics += "- Buffer management: Circular buffer (internal pointer management)\n";
    diagnostics += "- Buffer fill level: See context.bufferState.fillLevel\n";
    diagnostics += "- Current read/write pointers: Managed by CircularBuffer internally\n";
    return diagnostics;
}

std::string ThreadedStrategy::getProgressDisplay() const {
    // Threaded mode doesn't provide specific progress display
    // The main loop shows buffer level and other metrics
    return "";
}

void ThreadedStrategy::configure(const ::AudioStrategyConfig& config) {
    // Configure buffer capacity for cursor-chasing mode
    // Note: Using 2-second buffer at 48kHz for 100ms target lead
    // This method is called by AudioPlayer with context already set

    (void)config;  // Suppress unused parameter warning
    constexpr int sampleRate = 48000;  // Configured sample rate
    constexpr int bufferCapacity = sampleRate * 2;  // 2 seconds at 48kHz

    if (logger_) {
        logger_->info(LogMask::AUDIO,
                      "ThreadedStrategy configured: bufferCapacity=%d frames (%.2f seconds at 48kHz)",
                      bufferCapacity, bufferCapacity / static_cast<double>(sampleRate));
    }
}

void ThreadedStrategy::reset() {
    // Reset is handled by AudioPlayer by resetting the context directly
    // This method is a no-op in the strategy implementation
    if (logger_) {
        logger_->debug(LogMask::AUDIO, "ThreadedStrategy::reset: Reset handled by AudioPlayer");
    }
}

std::string ThreadedStrategy::getModeString() const {
    return "Threaded mode";
}

// ============================================================================
// Private Helper Methods
// ============================================================================

int ThreadedStrategy::getAvailableFrames(const StrategyContext* context) const {
    if (!context || !context->circularBuffer) {
        return 0;
    }

    // Use CircularBuffer's internal available() method
    return static_cast<int>(context->circularBuffer->available());
}

void ThreadedStrategy::updateDiagnostics(
    StrategyContext* context,
    int availableFrames,
    int requestedFrames
) {
    // Update buffer status
    if (availableFrames < requestedFrames) {
        context->bufferState.underrunCount.fetch_add(1);
    } else {
        context->bufferState.underrunCount.store(0);
    }

    // Update buffer fill level
    context->bufferState.fillLevel = static_cast<int>(context->circularBuffer->available());
}
