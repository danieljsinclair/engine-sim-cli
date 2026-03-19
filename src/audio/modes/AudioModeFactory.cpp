// AudioModeFactory.cpp - Audio mode factory implementation
// Factory decides internally based on API capabilities
// No enum, no switch - factory encapsulates selection logic (OCP, SRP)

#include "audio/modes/IAudioMode.h"
#include "audio/modes/ThreadedAudioMode.h"
#include "audio/modes/SyncPullAudioMode.h"

#include <iostream>

std::unique_ptr<IAudioMode> createAudioModeFactory(const EngineSimAPI* engineAPI, bool preferSyncPull) {
    if (!preferSyncPull) {
        std::cout << "[Audio] Factory selected Threaded mode (StartAudioThread available)\n";
        return std::make_unique<ThreadedAudioMode>();
    }
    
    std::cout << "[Audio] Factory selected SyncPull mode (default)\n";
    return std::make_unique<SyncPullAudioMode>();
}
