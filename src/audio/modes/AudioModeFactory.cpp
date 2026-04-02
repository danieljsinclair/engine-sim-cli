// AudioModeFactory.cpp - Audio mode factory implementation
// Factory decides internally based on API capabilities
// No enum, no switch - factory encapsulates selection logic (OCP, SRP)

#include "audio/modes/IAudioMode.h"
#include "audio/modes/ThreadedAudioMode.h"
#include "audio/modes/SyncPullAudioMode.h"
#include "AudioSource.h"
#include "config/ANSIColors.h"

std::unique_ptr<IAudioMode> createAudioModeFactory(const EngineSimAPI* engineAPI, bool preferSyncPull, ILogging* logger) {
    (void)engineAPI;
    if (!preferSyncPull) {
        if (logger) logger->info(LogMask::AUDIO, "THREADED mode selected (StartAudioThread available)");
        return std::make_unique<ThreadedAudioMode>(logger);
    }

    if (logger) logger->info(LogMask::AUDIO, "SYNC-PULL mode selected (default)");
    return std::make_unique<SyncPullAudioMode>(logger);
}
