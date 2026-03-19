// ThreadedAudioMode.cpp - Threaded audio mode implementation
// Renderer is injected into context by each mode (OCP, SRP, DI)

#include "audio/modes/ThreadedAudioMode.h"
#include "audio/modes/IAudioMode.h"
#include "AudioPlayer.h"
#include "AudioSource.h"
#include "CircularBuffer.h"
#include "SyncPullAudio.h"

#include "audio/renderers/SyncPullRenderer.h"
#include "audio/renderers/CircularBufferRenderer.h"

#include <iostream>

// ============================================================================
// AudioLoopConfig - Constants for audio loop timing
// ============================================================================

namespace AudioLoopConfig {
    constexpr double UPDATE_INTERVAL = 1.0 / 60.0;  // 60 Hz update rate
    constexpr int SAMPLE_RATE = 44100;
    constexpr int FRAMES_PER_UPDATE = 735;  // 44100 / 60
}

// ============================================================================
// ThreadedAudioMode Implementation
// DI: Creates and injects CircularBufferRenderer into context
// ============================================================================

std::string ThreadedAudioMode::getModeName() const {
    return "Threaded";
}

void ThreadedAudioMode::updateSimulation(EngineSimHandle handle, const EngineSimAPI& api,
                                         AudioPlayer* audioPlayer) {
    // Threaded mode: simulation is updated in the main loop
    api.Update(handle, AudioLoopConfig::UPDATE_INTERVAL);
    (void)audioPlayer;
}

void ThreadedAudioMode::generateAudio(IAudioSource& audioSource, AudioPlayer* audioPlayer) {
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

std::string ThreadedAudioMode::getModeString() const {
    return "threaded";
}

bool ThreadedAudioMode::startAudioThread(EngineSimHandle handle, const EngineSimAPI& api,
                                         AudioPlayer* audioPlayer) {
    if (!audioPlayer) {
        return true;
    }
    
    EngineSimResult result = api.StartAudioThread(handle);
    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to start audio thread\n";
        return false;
    }
    std::cout << "[Audio thread started (threaded mode)]\n";
    return true;
}

void ThreadedAudioMode::prepareBuffer(AudioPlayer* audioPlayer) {
    if (!audioPlayer) {
        return;
    }
    BufferOps::preFillCircularBuffer(audioPlayer);
    audioPlayer->start();
    std::cout << "[Audio playback enabled]\n";
}

void ThreadedAudioMode::resetBufferAfterWarmup(AudioPlayer* audioPlayer) {
    if (!audioPlayer) {
        return;
    }
    BufferOps::resetAndRePrefillBuffer(audioPlayer);
}

void ThreadedAudioMode::startPlayback(AudioPlayer* audioPlayer) {
    // Threaded mode: playback started in prepareBuffer
    (void)audioPlayer;
}

bool ThreadedAudioMode::shouldDrainDuringWarmup() const {
    // Threaded mode: draining needed during warmup
    return true;
}

std::unique_ptr<AudioUnitContext> ThreadedAudioMode::createContext(
    int sampleRate,
    EngineSimHandle engineHandle,
    const EngineSimAPI* engineAPI,
    bool silent
) {
    (void)engineHandle;
    (void)engineAPI;

    auto context = std::make_unique<AudioUnitContext>();
    context->sampleRate = sampleRate;
    context->silent = silent;

    // Create CircularBuffer for cursor-chasing mode
    context->circularBuffer = std::make_unique<CircularBuffer>();
    if (!context->circularBuffer->initialize(96000)) {
        std::cerr << "ERROR: Failed to initialize CircularBuffer\n";
        return nullptr;
    }

    // Initialize write pointer with 100ms pre-fill
    context->writePointer.store(static_cast<int>(sampleRate * 0.1));
    context->readPointer.store(0);
    context->totalFramesRead.store(0);

    // DI: Inject CircularBufferRenderer into context (mode knows its own renderer)
    // This eliminates conditional branching in AudioPlayer
    static CircularBufferRenderer circularBufferRenderer;
    context->setRenderer(&circularBufferRenderer);

    std::cout << "[Audio] Threaded/cursor-chasing mode initialized via factory\n";
    return context;
}
