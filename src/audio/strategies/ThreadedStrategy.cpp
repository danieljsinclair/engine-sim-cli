// ThreadedStrategy.cpp - Cursor-chasing audio strategy
// SRP: Single responsibility - only implements threaded cursor-chasing rendering
// OCP: New strategies can be added without modifying existing code
// DIP: Depends on abstractions, not concrete implementations

#include "audio/strategies/ThreadedStrategy.h"
#include "ILogging.h"
#include "Verification.h"

#include <cstring>
#include <algorithm>
#include <chrono>

using namespace std::chrono;

// ============================================================================
// NullTelemetryWriter - Silently discards all telemetry writes
// Used as default when no telemetry is injected (Null Object Pattern)
// ============================================================================

namespace {

class NullTelemetryWriter : public telemetry::ITelemetryWriter {
public:
    void write(const telemetry::TelemetryData&) override {}
    void writeEngineState(const telemetry::EngineStateTelemetry&) override {}
    void writeFramePerformance(const telemetry::FramePerformanceTelemetry&) override {}
    void writeAudioDiagnostics(const telemetry::AudioDiagnosticsTelemetry&) override {}
    void writeVehicleInputs(const telemetry::VehicleInputsTelemetry&) override {}
    void writeSimulatorMetrics(const telemetry::SimulatorMetricsTelemetry&) override {}
    void reset() override {}
    const char* getName() const override { return "NullTelemetryWriter"; }
};

} // anonymous namespace

// ============================================================================
// ThreadedStrategy Implementation
// ============================================================================

ThreadedStrategy::ThreadedStrategy(ILogging* logger, telemetry::ITelemetryWriter* telemetry)
    : defaultLogger_(logger ? nullptr : new ConsoleLogger())
    , logger_(logger ? logger : defaultLogger_.get())
    , defaultTelemetry_(telemetry ? nullptr : new NullTelemetryWriter())
    , telemetry_(telemetry ? telemetry : defaultTelemetry_.get())
{
    ASSERT(logger_, "ThreadedStrategy: logger must not be null");
    ASSERT(telemetry_, "ThreadedStrategy: telemetry must not be null");
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

bool ThreadedStrategy::isPlaying() const {
    return audioState_.isPlaying.load();
}

bool ThreadedStrategy::shouldDrainDuringWarmup() const {
    return true;
}

void ThreadedStrategy::fillBufferFromEngine(EngineSimHandle handle, const EngineSimAPI& api, int defaultFramesPerUpdate) {
    if (!handle) return;

    // Cursor chasing: adjust how many frames to read based on buffer fill level
    size_t available = circularBuffer_.available();
    int bufferSize = static_cast<int>(circularBuffer_.capacity());
    int sampleRate = audioState_.sampleRate;
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
        AddFrames(buffer.data(), totalRead);
    }
}

// ============================================================================
// Lifecycle Method Implementations
// ============================================================================

bool ThreadedStrategy::initialize(const AudioStrategyConfig& config) {
    ASSERT(logger_, "ThreadedStrategy::initialize: logger must not be null");
    ASSERT(telemetry_, "ThreadedStrategy::initialize: telemetry must not be null");

    // Initialize circular buffer with appropriate capacity
    int bufferCapacity = config.sampleRate * 2;

    if (!circularBuffer_.initialize(bufferCapacity)) {
        logger_->error(LogMask::AUDIO, "ThreadedStrategy::initialize: Failed to initialize circular buffer");
        return false;
    }

    audioState_.sampleRate = config.sampleRate;
    audioState_.isPlaying = false;
    circularBuffer_.reset();

    logger_->info(LogMask::AUDIO,
                  "ThreadedStrategy initialized: bufferCapacity=%d frames (%.2f seconds)",
                  bufferCapacity, bufferCapacity / static_cast<double>(config.sampleRate));

    return true;
}

void ThreadedStrategy::prepareBuffer() {
    // Pre-fill circular buffer with silence for smooth playback start
    int preFillFrames = static_cast<int>(audioState_.sampleRate * 0.1);
    int capacity = static_cast<int>(circularBuffer_.capacity());
    preFillFrames = std::min(preFillFrames, capacity);

    std::vector<float> silence(preFillFrames * 2);
    std::fill(silence.begin(), silence.end(), 0.0f);

    size_t framesWritten = circularBuffer_.write(silence.data(), preFillFrames);
    (void)framesWritten;

    logger_->debug(LogMask::AUDIO, "ThreadedStrategy::prepareBuffer: Pre-filled %d frames with silence", static_cast<int>(framesWritten));
}

bool ThreadedStrategy::startPlayback(EngineSimHandle handle, const EngineSimAPI* api) {
    if (!api || !handle) {
        logger_->error(LogMask::AUDIO, "ThreadedStrategy::startPlayback: Invalid engine API or handle");
        return false;
    }

    EngineSimResult result = api->StartAudioThread(handle);
    if (result != ESIM_SUCCESS) {
        logger_->error(LogMask::AUDIO, "ThreadedStrategy::startPlayback: Failed to start audio thread (result=%d)", result);
        return false;
    }

    audioState_.isPlaying.store(true);

    logger_->info(LogMask::AUDIO, "ThreadedStrategy::startPlayback: Audio thread started");

    return true;
}

void ThreadedStrategy::stopPlayback(EngineSimHandle handle, const EngineSimAPI* api) {
    if (api && handle) {
        logger_->debug(LogMask::AUDIO, "ThreadedStrategy::stopPlayback: Audio thread will stop with simulation");
    }

    audioState_.isPlaying.store(false);

    logger_->info(LogMask::AUDIO, "ThreadedStrategy::stopPlayback: Playback stopped");
}

void ThreadedStrategy::resetBufferAfterWarmup() {
    circularBuffer_.reset();

    logger_->info(LogMask::AUDIO, "ThreadedStrategy::resetBufferAfterWarmup: Buffer reset complete");
}

void ThreadedStrategy::updateSimulation(EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) {
    if (handle && audioState_.isPlaying.load()) {
        double deltaTimeSeconds = deltaTimeMs / 1000.0;
        EngineSimResult result = api.Update(handle, deltaTimeSeconds);

        if (result != ESIM_SUCCESS) {
            logger_->warning(LogMask::AUDIO, "ThreadedStrategy::updateSimulation: Engine update failed (result=%d)", result);
        }
    }
}

bool ThreadedStrategy::render(
    AudioBufferList* ioData,
    UInt32 numberFrames
) {
    if (!ioData) {
        return false;
    }

    if (!circularBuffer_.isInitialized()) {
        return false;
    }

    int availableFrames = getAvailableFrames();
    int framesToRead = std::min(static_cast<int>(numberFrames), availableFrames);

    if (framesToRead <= 0) {
        std::memset(ioData->mBuffers[0].mData, 0, ioData->mBuffers[0].mDataByteSize);
        updateDiagnostics(0, static_cast<int>(numberFrames));
        return true;
    }

    circularBuffer_.read(static_cast<float*>(ioData->mBuffers[0].mData), framesToRead);

    updateDiagnostics(availableFrames, numberFrames);

    return true;
}

bool ThreadedStrategy::AddFrames(
    float* buffer,
    int frameCount
) {
    if (!buffer || frameCount <= 0) {
        return false;
    }

    if (!circularBuffer_.isInitialized()) {
        return false;
    }

    size_t framesWritten = circularBuffer_.write(buffer, frameCount);

    if (framesWritten != static_cast<size_t>(frameCount)) {
        logger_->warning(LogMask::AUDIO, "ThreadedStrategy::AddFrames: Only wrote %zu/%d frames",
                      framesWritten, frameCount);
    }

    return true;
}

void ThreadedStrategy::reset() {
    logger_->debug(LogMask::AUDIO, "ThreadedStrategy::reset");
}

std::string ThreadedStrategy::getModeString() const {
    return "Threaded mode";
}

// ============================================================================
// Private Helper Methods
// ============================================================================

int ThreadedStrategy::getAvailableFrames() const {
    return static_cast<int>(circularBuffer_.available());
}

void ThreadedStrategy::updateDiagnostics(
    int availableFrames,
    int requestedFrames
) {
    if (availableFrames < requestedFrames) {
        underrunCount_++;
    } else {
        underrunCount_ = 0;
    }

    double bufferHealthPct = 0.0;
    if (circularBuffer_.capacity() > 0) {
        bufferHealthPct = (static_cast<double>(availableFrames) /
                          static_cast<double>(circularBuffer_.capacity())) * 100.0;
    }

    publishAudioDiagnostics(underrunCount_, bufferHealthPct);
}

void ThreadedStrategy::publishAudioDiagnostics(int underrunCount, double bufferHealthPct) {
    telemetry::AudioDiagnosticsTelemetry diag;
    diag.underrunCount = underrunCount;
    diag.bufferHealthPct = bufferHealthPct;
    telemetry_->writeAudioDiagnostics(diag);
}
