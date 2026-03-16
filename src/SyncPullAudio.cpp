// SyncPullAudio.cpp - Sync-pull audio rendering implementation

#include "SyncPullAudio.h"

#include <cstring>
#include <iostream>
#include <iomanip>

// ============================================================================
// SyncPullAudio Implementation
// ============================================================================

SyncPullAudio::SyncPullAudio()
    : engineHandle_(nullptr), engineAPI_(nullptr), sampleRate_(44100), silent_(false) {
}

SyncPullAudio::~SyncPullAudio() {
    cleanup();
}

bool SyncPullAudio::initialize(EngineSimHandle handle, const EngineSimAPI* api, int sampleRate, bool silent) {
    cleanup();

    engineHandle_ = handle;
    engineAPI_ = api;
    sampleRate_ = sampleRate;
    silent_ = silent;

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
    silent_ = false;
}

int SyncPullAudio::renderOnDemand(float* outputBuffer, int framesToRender) {
    if (!isEnabled() || !outputBuffer || framesToRender <= 0) {
        return 0;
    }

    // Render audio synchronously on-demand
    int framesRead = 0;
    engineAPI_->RenderOnDemand(engineHandle_, outputBuffer, framesToRender, &framesRead);

    // DIAGNOSTICS: Print first few samples periodically
    static int cbCount = 0;
    if (cbCount++ % 50 == 0) {
        std::cout << "[SYNC-PULL] req=" << framesToRender << " got=" << framesRead;
        for (int j = 0; j < std::min(5, framesRead); j++) {
            std::cout << " [" << j << "]=" << std::fixed << std::setprecision(4) << outputBuffer[j*2];
        }
        std::cout << "\n";
    }

    // Silent mode: zero output after processing
    if (silent_) {
        std::memset(outputBuffer, 0, framesToRender * 2 * sizeof(float));
    }

    return framesRead;
}
