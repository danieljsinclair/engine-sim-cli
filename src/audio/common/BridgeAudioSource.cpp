// BridgeAudioSource.cpp - Bridge-backed audio source implementation

#include "BridgeAudioSource.h"

#include <cstring>

namespace audio {

BridgeAudioSource::BridgeAudioSource(EngineSimHandle handle, const EngineSimAPI& api)
    : handle_(handle)
    , api_(api)
{
}

int BridgeAudioSource::generateAudio(float* buffer, int frames) {
    if (!handle_) {
        // Return silence if no handle
        std::memset(buffer, 0, frames * 2 * sizeof(float));
        return frames;
    }

    // Call bridge Render function to generate audio
    // The bridge API expects interleaved stereo float buffer
    int32_t framesWritten = 0;
    EngineSimResult result = api_.Render(handle_, buffer, frames, &framesWritten);

    // If render failed or returned 0, fill with silence
    if (result != ESIM_SUCCESS || framesWritten <= 0) {
        std::memset(buffer, 0, frames * 2 * sizeof(float));
        return frames;
    }

    return framesWritten;
}

void BridgeAudioSource::update(double deltaTime) {
    // Update bridge simulation state
    if (handle_) {
        api_.Update(handle_, deltaTime);
    }
}

void BridgeAudioSource::updateStats(const EngineSimStats& stats) {
    // Stats can be used for diagnostics or presentation
    // Currently not used in the audio generation path
    (void)stats;
}

} // namespace audio
