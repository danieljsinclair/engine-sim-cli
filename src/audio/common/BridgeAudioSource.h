// BridgeAudioSource.h - Bridge-backed audio source implementation
// Implements IAudioSource using engine-sim bridge

#ifndef AUDIO_COMMON_BRIDGE_AUDIO_SOURCE_H
#define AUDIO_COMMON_BRIDGE_AUDIO_SOURCE_H

#include "audio/common/IAudioSource.h"

#include "engine_sim_bridge.h"
#include "bridge/engine_sim_loader.h"

namespace audio {

/**
 * Bridge-backed audio source implementation.
 * Uses engine-sim bridge to generate audio samples.
 */
class BridgeAudioSource : public IAudioSource {
public:
    BridgeAudioSource(EngineSimHandle handle, const EngineSimAPI& api);
    ~BridgeAudioSource() override = default;

    // IAudioSource interface implementation
    int generateAudio(float* buffer, int frames) override;
    void update(double deltaTime) override;

    // Additional methods specific to bridge audio source
    void updateStats(const EngineSimStats& stats);

private:
    EngineSimHandle handle_;
    const EngineSimAPI& api_;
};

} // namespace audio

#endif // AUDIO_COMMON_BRIDGE_AUDIO_SOURCE_H
