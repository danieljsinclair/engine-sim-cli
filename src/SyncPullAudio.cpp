// SyncPullAudio.cpp - Sync-pull audio rendering implementation

#include "SyncPullAudio.h"
#include "AudioSource.h"
#include "ConsoleColors.h"

#include <iostream>

// ============================================================================
// SyncPullAudio Implementation
// ============================================================================

SyncPullAudio::SyncPullAudio()
    : engineHandle_(nullptr), engineAPI_(nullptr), sampleRate_(44100) {
}

SyncPullAudio::~SyncPullAudio() {
    cleanup();
}

bool SyncPullAudio::initialize(EngineSimHandle handle, const EngineSimAPI* api, int sampleRate) {
    cleanup();

    engineHandle_ = handle;
    engineAPI_ = api;
    sampleRate_ = sampleRate;

    if (engineHandle_ && engineAPI_) {
        std::cout << "[SyncPullAudio] Initialized - direct render on callback\n";
        return true;
    }

    std::cerr << "[SyncPullAudio] ERROR: Invalid handle or API\n";
    return false;
}

void SyncPullAudio::cleanup() {
    engineHandle_ = nullptr;
    engineAPI_ = nullptr;
    sampleRate_ = 44100;
    preBuffer_.clear();
    preBufferReadPos_ = 0;
}

void SyncPullAudio::preFillBuffer(int targetMs) {
    if (!isEnabled()) return;
    
    int framesNeeded = (sampleRate_ * targetMs) / 1000;
    preBuffer_.resize(framesNeeded * 2);  // stereo
    
    int framesRead = 0;
    engineAPI_->RenderOnDemand(engineHandle_, preBuffer_.data(), framesNeeded, &framesRead);
    
    // Resize to actual frames we got
    preBuffer_.resize(framesRead * 2);
    preBufferReadPos_ = 0;
    
    std::cout << ANSIColors::colorPreFill("[SyncPullAudio] Pre-filled ") 
              << "\x1b[35m" << (framesRead * 1000 / sampleRate_) << "ms\x1b[0m buffer\n";
}

int SyncPullAudio::renderOnDemand(float* outputBuffer, int framesToRender) {
    if (!isEnabled() || !outputBuffer || framesToRender <= 0) {
        return 0;
    }

    int framesRead = 0;
    
    // First, try to read from pre-buffer
    if (!preBuffer_.empty() && preBufferReadPos_ < preBuffer_.size()) {
        size_t framesInBuffer = (preBuffer_.size() - preBufferReadPos_) / 2;
        size_t framesToCopy = std::min((size_t)framesToRender, framesInBuffer);
        
        // Copy stereo frames from pre-buffer
        for (size_t i = 0; i < framesToCopy * 2; i++) {
            outputBuffer[i] = preBuffer_[preBufferReadPos_ + i];
        }
        
        preBufferReadPos_ += framesToCopy * 2;
        outputBuffer += framesToCopy * 2;
        framesRead = framesToCopy;
        framesToRender -= framesToCopy;
        
        // Debug: show pre-buffer depletion
        if (preBufferReadPos_ >= preBuffer_.size()) {
            std::cout << "[SyncPullAudio] Pre-buffer depleted after copying " << framesToCopy << " frames\n";
        }
    }
    
    // Then generate remaining frames on-demand
    if (framesToRender > 0) {
        int newFrames = 0;
        engineAPI_->RenderOnDemand(engineHandle_, outputBuffer, framesToRender, &newFrames);
        framesRead += newFrames;
        
        // Debug: warn if on-demand returned fewer frames than requested
        // Only print every 10th underflow to reduce verbosity
        static int underflowCount = 0;
        if (newFrames < framesToRender) {
            if (++underflowCount % 10 == 0) {
                std::cout << "[SyncPullAudio] UNDERFLOW (x" << underflowCount << "): requested " << framesToRender << ", got " << newFrames << "\n";
            }
        }
    }

    return framesRead;
}
