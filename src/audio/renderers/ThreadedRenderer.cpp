// ThreadedRenderer.cpp - Threaded renderer implementation (consolidated)
// Combines lifecycle and rendering for cursor-chasing mode
// Renders audio from cursor-chasing circular buffer using hardware feedback

#include "audio/renderers/ThreadedRenderer.h"
#include "audio/renderers/IAudioRenderer.h"
#include "AudioPlayer.h"
#include "AudioSource.h"
#include "../common/CircularBuffer.h"
#include "SyncPullAudio.h"
#include "config/ANSIColors.h"

// BufferOps is defined in AudioSource.h
using namespace BufferOps;

#include <iostream>
#include <algorithm>
#include <vector>

// ============================================================================
// AudioLoopConfig - Constants for audio loop timing
// ============================================================================

namespace AudioLoopConfig {
    constexpr double UPDATE_INTERVAL = 1.0 / 60.0;  // 60 Hz update rate
    constexpr int SAMPLE_RATE = 44100;
    constexpr int FRAMES_PER_UPDATE = 735;  // 44100 / 60
}

// ============================================================================
// ThreadedRenderer Implementation
// ============================================================================

ThreadedRenderer::ThreadedRenderer(ILogging* logger)
    : defaultLogger_(logger ? nullptr : new ConsoleLogger())
    , logger_(logger ? logger : defaultLogger_.get()) {
}

// === Lifecycle methods (from ThreadedAudioMode) ===

std::string ThreadedRenderer::getModeName() const {
    return "Threaded";
}

void ThreadedRenderer::updateSimulation(EngineSimHandle handle, const EngineSimAPI& api,
                                       AudioPlayer* audioPlayer) {
    // Threaded mode: simulation is updated in the main loop
    api.Update(handle, AudioLoopConfig::UPDATE_INTERVAL);
    (void)audioPlayer;
}

void ThreadedRenderer::generateAudio(IAudioSource& audioSource, AudioPlayer* audioPlayer) {
    if (!audioPlayer) {
        return;
    }

    int framesToWrite = audioPlayer->calculateCursorChasingSamples(AudioLoopConfig::FRAMES_PER_UPDATE);
    if (framesToWrite <= 0) {
        return;
    }

    std::vector<float> audioBuffer(framesToWrite * 2);
    if (audioSource.generateAudio(audioBuffer, framesToWrite)) {
        audioPlayer->addToCircularBuffer(audioBuffer.data(), framesToWrite);
    }
}

std::string ThreadedRenderer::getModeString() const {
    return "threaded";
}

bool ThreadedRenderer::startAudioThread(EngineSimHandle handle, const EngineSimAPI& api,
                                       AudioPlayer* audioPlayer) {
    if (!audioPlayer) {
        return true;
    }

    EngineSimResult result = api.StartAudioThread(handle);
    if (result != ESIM_SUCCESS) {
        logger_->error(LogMask::THREADED_AUDIO, "Failed to start audio thread");
        return false;
    }
    logger_->info(LogMask::THREADED_AUDIO, "audio thread started");
    return true;
}

void ThreadedRenderer::prepareBuffer(AudioPlayer* audioPlayer) {
    if (!audioPlayer) {
        return;
    }
    BufferOps::preFillCircularBuffer(audioPlayer);
    if (!audioPlayer->start()) {
        throw std::runtime_error(std::string(ANSIColors::RED) + "ERROR: Failed to start AudioUnit playback" + ANSIColors::RESET);
    }
    logger_->info(LogMask::THREADED_AUDIO, "Audio playback enabled");
}

void ThreadedRenderer::resetBufferAfterWarmup(AudioPlayer* audioPlayer) {
    if (!audioPlayer) {
        return;
    }
    BufferOps::resetAndRePrefillBuffer(audioPlayer);
}

void ThreadedRenderer::startPlayback(AudioPlayer* audioPlayer) {
    // Threaded mode: playback started in prepareBuffer
    (void)audioPlayer;
}

bool ThreadedRenderer::shouldDrainDuringWarmup() const {
    // Threaded mode: draining needed during warmup
    return true;
}

void ThreadedRenderer::configure(const SimulationConfig& config) {
    (void)config;
    // No configuration needed for threaded mode
}

std::unique_ptr<AudioUnitContext> ThreadedRenderer::createContext(
    int sampleRate,
    EngineSimHandle engineHandle,
    const EngineSimAPI* engineAPI
) {
    (void)engineHandle;
    (void)engineAPI;

    auto context = std::make_unique<AudioUnitContext>();
    context->sampleRate = sampleRate;

    // Create CircularBuffer for cursor-chasing mode
    context->circularBuffer = std::make_unique<CircularBuffer>();
    if (!context->circularBuffer->initialize(96000)) {
        logger_->error(LogMask::THREADED_AUDIO, "Failed to initialize CircularBuffer");
        return nullptr;
    }

    // Initialize write pointer with 100ms pre-fill
    context->writePointer.store(static_cast<int>(sampleRate * 0.1));
    context->readPointer.store(0);
    context->totalFramesRead.store(0);

    // DI: Inject this renderer into context
    context->setRenderer(this);

    logger_->info(LogMask::THREADED_AUDIO, "Threaded/cursor-chasing mode initialized");
    return context;
}

// === Rendering methods (from ThreadedRenderer) ===

bool ThreadedRenderer::render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) {
    AudioUnitContext* context = static_cast<AudioUnitContext*>(ctx);

    if (!context || !context->circularBuffer || !context->circularBuffer->isInitialized()) {
        return false;
    }

    const int bufferSize = static_cast<int>(context->circularBuffer->capacity());

    for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
        AudioBuffer& buffer = ioData->mBuffers[i];
        float* data = static_cast<float*>(buffer.mData);

        // Clamp frames to buffer capacity
        UInt32 framesToWrite = numberFrames;
        if (framesToWrite * 2 * sizeof(float) > buffer.mDataByteSize) {
            framesToWrite = buffer.mDataByteSize / (2 * sizeof(float));
        }

        // Calculate available frames using cursor-chasing logic
        int readPtr = context->readPointer.load();
        int writePtr = context->writePointer.load();

        int available;
        if (writePtr >= readPtr) {
            available = writePtr - readPtr;
        } else {
            available = (bufferSize - readPtr) + writePtr;
        }

        // Determine how many frames we can actually read
        int framesToRead = std::min(static_cast<int>(framesToWrite), available);

        // Track underruns for diagnostics
        if (framesToRead < static_cast<int>(framesToWrite)) {
            context->underrunCount.fetch_add(1);
            if (context->underrunCount.load() % 10 == 0) {
                std::cout << "[SYNC-PULL] UNDERFLOW (x" << context->underrunCount.load()
                          << "): requested " << framesToWrite << ", got " << framesToRead << "\n";
            }
        }

        // Read from circular buffer
        size_t framesRead = context->circularBuffer->read(data, framesToRead);
        (void)framesRead;

        // Handle underrun: fill remaining with silence
        if (framesToRead < static_cast<int>(framesToWrite)) {
            int silenceFrames = framesToWrite - framesToRead;
            // Fill from where we left off
            EngineSimAudio::fillSilence(data + framesToRead * 2, silenceFrames);
            context->bufferStatus = (available < bufferSize / 8) ? 2 : 1;
        } else {
            context->bufferStatus = 0;
        }

        // Update read cursor (hardware position)
        int newReadPtr = (readPtr + framesToRead) % bufferSize;
        context->readPointer.store(newReadPtr);
        context->totalFramesRead.fetch_add(framesToRead);
    }

    return true;
}

bool ThreadedRenderer::isEnabled() const {
    // Will be checked via context in actual use
    return true;
}

bool ThreadedRenderer::AddFrames(void* ctx, float* buffer, int frameCount) {
    AudioUnitContext* context = static_cast<AudioUnitContext*>(ctx);

    if (!context || !context->circularBuffer || !context->circularBuffer->isInitialized()) {
        return false;
    }

    // Write frames to circular buffer
    size_t framesWritten = context->circularBuffer->write(buffer, frameCount);

    // Update write pointer
    int writePtr = context->writePointer.load();
    int bufferSize = static_cast<int>(context->circularBuffer->capacity());
    int newWritePtr = (writePtr + static_cast<int>(framesWritten)) % bufferSize;
    context->writePointer.store(newWritePtr);

    return framesWritten > 0;
}
