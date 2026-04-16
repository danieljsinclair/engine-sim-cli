// ThreadedStrategy.cpp - Cursor-chasing audio strategy (NEW STATE MODEL)
// Implements IAudioStrategy using BufferContext state model
// SRP: Single responsibility - only implements threaded cursor-chasing rendering
// OCP: New strategies can be added without modifying existing code
// DIP: Depends on state abstractions, not concrete implementations

#include "audio/strategies/ThreadedStrategy.h"
#include "ILogging.h"

#include <cstring>
#include <algorithm>
#include <chrono>

using namespace std::chrono;

// ============================================================================
// ThreadedStrategy Implementation
// ============================================================================

ThreadedStrategy::ThreadedStrategy(ILogging* logger, telemetry::ITelemetryWriter* telemetry)
    : logger_(logger), telemetry_(telemetry) {
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

void ThreadedStrategy::fillBufferFromEngine(BufferContext* context, EngineSimHandle handle, const EngineSimAPI& api, int defaultFramesPerUpdate) {
    if (!context || !context->circularBuffer || !handle) return;

    // Cursor chasing: adjust how many frames to read based on buffer fill level
    size_t available = context->circularBuffer->available();
    int bufferSize = static_cast<int>(context->circularBuffer->capacity());
    int sampleRate = context->audioState.sampleRate;
    int targetLead = static_cast<int>(sampleRate * 0.1);  // 100ms lead

    int framesToWrite;
    if (static_cast<int>(available) < targetLead) {
        framesToWrite = defaultFramesPerUpdate + (targetLead - static_cast<int>(available));
    } else if (static_cast<int>(available) > targetLead * 2) {
        framesToWrite = std::max(defaultFramesPerUpdate - (static_cast<int>(available) - targetLead), 0);
    } else {
        framesToWrite = defaultFramesPerUpdate;
    }

    constexpr int MAX_FRAMES_PER_READ = 4096;
    framesToWrite = std::min(framesToWrite, MAX_FRAMES_PER_READ);
    framesToWrite = std::min(framesToWrite, bufferSize);

    std::vector<float> buffer(framesToWrite * 2);
    int totalRead = 0;
    api.ReadAudioBuffer(handle, buffer.data(), framesToWrite, &totalRead);

    if (totalRead > 0) {
        AddFrames(context, buffer.data(), totalRead);
    }
}

// ============================================================================
// Lifecycle Method Implementations
// ============================================================================

bool ThreadedStrategy::initialize(BufferContext* context, const AudioStrategyConfig& config) {
    if (!context) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "ThreadedStrategy::initialize: Invalid context");
        }
        return false;
    }

    // Initialize circular buffer with appropriate capacity
    // Using 2-second buffer at configured sample rate for 100ms target lead
    int bufferCapacity = config.sampleRate * 2;
    if (!context->circularBuffer) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "ThreadedStrategy::initialize: Circular buffer not provided in context");
        }
        return false;
    }

    if (!context->circularBuffer->initialize(bufferCapacity)) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "ThreadedStrategy::initialize: Failed to initialize circular buffer");
        }
        return false;
    }

    // Initialize audio state
    context->audioState.sampleRate = config.sampleRate;
    context->audioState.isPlaying = false;

    // Reset circular buffer state
    context->circularBuffer->reset();

    if (logger_) {
        logger_->info(LogMask::AUDIO,
                      "ThreadedStrategy initialized: bufferCapacity=%d frames (%.2f seconds)",
                      bufferCapacity, bufferCapacity / static_cast<double>(config.sampleRate));
    }

    return true;
}

void ThreadedStrategy::prepareBuffer(BufferContext* context) {
    if (!context || !context->circularBuffer) {
        if (logger_) {
            logger_->warning(LogMask::AUDIO, "ThreadedStrategy::prepareBuffer: Invalid context or buffer");
        }
        return;
    }

    // Pre-fill circular buffer with silence for smooth playback start
    // Fill ~100ms of silence (about 4800 frames at 48kHz)
    int preFillFrames = static_cast<int>(context->audioState.sampleRate * 0.1);
    int capacity = static_cast<int>(context->circularBuffer->capacity());
    preFillFrames = std::min(preFillFrames, capacity);

    // Stereo audio = 2 channels per frame
    std::vector<float> silence(preFillFrames * 2);
    std::fill(silence.begin(), silence.end(), 0.0f);

    size_t framesWritten = context->circularBuffer->write(silence.data(), preFillFrames);
    (void)framesWritten;

    if (logger_) {
        logger_->debug(LogMask::AUDIO, "ThreadedStrategy::prepareBuffer: Pre-filled %d frames with silence", static_cast<int>(framesWritten));
    }
}

bool ThreadedStrategy::startPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) {
    if (!context) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "ThreadedStrategy::startPlayback: Invalid context");
        }
        return false;
    }

    // Start the engine's audio thread for threaded mode
    if (!api || !handle) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "ThreadedStrategy::startPlayback: Invalid engine API or handle");
        }
        return false;
    }

    EngineSimResult result = api->StartAudioThread(handle);
    if (result != ESIM_SUCCESS) {
        if (logger_) {
            logger_->error(LogMask::AUDIO, "ThreadedStrategy::startPlayback: Failed to start audio thread (result=%d)", result);
        }
        return false;
    }

    // Mark playback as started
    context->audioState.isPlaying.store(true);

    if (logger_) {
        logger_->info(LogMask::AUDIO, "ThreadedStrategy::startPlayback: Audio thread started");
    }

    return true;
}

void ThreadedStrategy::stopPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) {
    if (!context) {
        if (logger_) {
            logger_->warning(LogMask::AUDIO, "ThreadedStrategy::stopPlayback: Invalid context");
        }
        return;
    }

    // Stop the engine's audio thread
    if (api && handle) {
        // Note: EngineSim API doesn't have a StopAudioThread method
        // The thread will naturally stop when the simulation ends
        if (logger_) {
            logger_->debug(LogMask::AUDIO, "ThreadedStrategy::stopPlayback: Audio thread will stop with simulation");
        }
    }

    // Mark playback as stopped
    context->audioState.isPlaying.store(false);

    if (logger_) {
        logger_->info(LogMask::AUDIO, "ThreadedStrategy::stopPlayback: Playback stopped");
    }
}

void ThreadedStrategy::resetBufferAfterWarmup(BufferContext* context) {
    if (!context || !context->circularBuffer) {
        if (logger_) {
            logger_->warning(LogMask::AUDIO, "ThreadedStrategy::resetBufferAfterWarmup: Invalid context or buffer");
        }
        return;
    }

    // Reset circular buffer to eliminate warmup latency
    context->circularBuffer->reset();

    if (logger_) {
        logger_->info(LogMask::AUDIO, "ThreadedStrategy::resetBufferAfterWarmup: Buffer reset complete");
    }
}

void ThreadedStrategy::updateSimulation(BufferContext* context, EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) {
    if (!context) {
        if (logger_) {
            logger_->warning(LogMask::AUDIO, "ThreadedStrategy::updateSimulation: Invalid context");
        }
        return;
    }

    // Threaded mode updates simulation in main loop
    if (handle && context->audioState.isPlaying.load()) {
        // Convert deltaTime from milliseconds to seconds (bridge expects seconds, <= 1.0)
        double deltaTimeSeconds = deltaTimeMs / 1000.0;
        EngineSimResult result = api.Update(handle, deltaTimeSeconds);

        if (result != ESIM_SUCCESS && logger_) {
            logger_->warning(LogMask::AUDIO, "ThreadedStrategy::updateSimulation: Engine update failed (result=%d)", result);
        }
    }
}

bool ThreadedStrategy::render(
    BufferContext* context,
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
        // Update diagnostics to increment underrun count
        updateDiagnostics(context, 0, static_cast<int>(numberFrames));
        return true;
    }

    // Read frames from circular buffer using regular read() with physical pointer
    size_t framesRead = context->circularBuffer->read(static_cast<float*>(ioData->mBuffers[0].mData), framesToRead);

    // Calculate target lead (100ms at current sample rate)
    int targetLead = static_cast<int>(context->audioState.sampleRate * 0.1);

    // Update diagnostics with buffer status
    updateDiagnostics(context, availableFrames, numberFrames);

    return true;
}

bool ThreadedStrategy::AddFrames(
    BufferContext* context,
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

    // Write frames to circular buffer using regular write() with physical pointer
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
    diagnostics += "- Buffer management: Circular buffer\n";
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

int ThreadedStrategy::getAvailableFrames(const BufferContext* context) const {
    if (!context || !context->circularBuffer) {
        return 0;
    }

    // Use CircularBuffer's internal available() method
    return static_cast<int>(context->circularBuffer->available());
}

void ThreadedStrategy::updateDiagnostics(
    BufferContext* context,
    int availableFrames,
    int requestedFrames
) {
    // Update buffer underrun tracking (internal state, not on CircularBuffer)
    if (availableFrames < requestedFrames) {
        underrunCount_++;
    } else {
        underrunCount_ = 0;
    }

    // Calculate buffer health percentage
    double bufferHealthPct = 0.0;
    if (context && context->circularBuffer && context->circularBuffer->capacity() > 0) {
        bufferHealthPct = (static_cast<double>(availableFrames) /
                          static_cast<double>(context->circularBuffer->capacity())) * 100.0;
    }

    // Push to telemetry
    publishAudioDiagnostics(underrunCount_, bufferHealthPct);
}

void ThreadedStrategy::publishAudioDiagnostics(int underrunCount, double bufferHealthPct) {
    if (telemetry_) {
        telemetry::AudioDiagnosticsTelemetry diag;
        diag.underrunCount = underrunCount;
        diag.bufferHealthPct = bufferHealthPct;
        telemetry_->writeAudioDiagnostics(diag);
    }
}
