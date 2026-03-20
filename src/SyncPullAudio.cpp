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
}

int SyncPullAudio::renderOnDemand(float* outputBuffer, int framesToRender) {
    if (!isEnabled() || !outputBuffer || framesToRender <= 0) {
        return 0;
    }

    // Render audio synchronously on-demand
    int framesRead = 0;
    engineAPI_->RenderOnDemand(engineHandle_, outputBuffer, framesToRender, &framesRead);

    return framesRead;
}
