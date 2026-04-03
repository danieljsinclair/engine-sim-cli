// AudioRendererFactory.cpp - Audio renderer factory implementation
// Factory decides internally based on API capabilities
// No enum, no switch - factory encapsulates selection logic (OCP, SRP)

#include "audio/renderers/IAudioRenderer.h"
#include "audio/renderers/ThreadedRenderer.h"
#include "audio/renderers/SyncPullRenderer.h"
#include "AudioSource.h"
#include "config/ANSIColors.h"

std::unique_ptr<IAudioRenderer> createAudioRendererFactory(const EngineSimAPI* engineAPI, bool preferSyncPull, ILogging* logger) {
    (void)engineAPI;
    if (!preferSyncPull) {
        if (logger) logger->info(LogMask::AUDIO, "THREADED mode selected (StartAudioThread available)");
        return std::make_unique<ThreadedRenderer>(logger);
    }

    if (logger) logger->info(LogMask::AUDIO, "SYNC-PULL mode selected (default)");
    return std::make_unique<SyncPullRenderer>(logger);
}
