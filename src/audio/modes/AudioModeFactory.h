// AudioModeFactory.h - Audio mode factory declaration
// Factory decides internally based on API capabilities (OCP, SRP)

#ifndef AUDIO_MODE_FACTORY_H
#define AUDIO_MODE_FACTORY_H

#include "IAudioMode.h"
#include <memory>

class EngineSimAPI;

std::unique_ptr<IAudioMode> createAudioModeFactory(const EngineSimAPI* engineAPI, bool preferSyncPull);

#endif // AUDIO_MODE_FACTORY_H
