// AudioPlayerFactory.h - Factory for audio mode creation
// Now returns IAudioMode directly - no enum, no switch, no boolean flags

#ifndef AUDIO_PLAYER_FACTORY_H
#define AUDIO_PLAYER_FACTORY_H

#include <memory>

#include "engine_sim_bridge.h"
#include "AudioMode.h"

// Forward declarations
class AudioPlayer;

// Note: createAudioModeFactory is now defined in AudioMode.h
// This header is kept for backward compatibility

#endif // AUDIO_PLAYER_FACTORY_H
