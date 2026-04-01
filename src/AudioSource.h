// AudioSource.h - Audio source abstraction interface and implementations

#ifndef AUDIO_SOURCE_H
#define AUDIO_SOURCE_H

#include <memory>
#include <vector>

#include "engine_sim_bridge.h"
#include "bridge/engine_sim_loader.h"

class AudioPlayer;

// ============================================================================
// Audio source abstraction - base class with common implementation
// ============================================================================

class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual bool generateAudio(std::vector<float>& buffer, int frames) = 0;
    virtual void updateStats(const EngineSimStats& stats) = 0;
    virtual void displayProgress(double currentTime, double duration, bool interactive, const EngineSimStats& stats, double throttle, int underrunCount, const AudioPlayer* audioPlayer = nullptr) = 0;
};

// Base audio source - DRY: contains common generateAudio() implementation using SineGenerator
// Both SineAudioSource and EngineAudioSource inherit from this base
class BaseAudioSource : public IAudioSource {
protected:
    EngineSimHandle handle_;
    const EngineSimAPI& api_;

public:
    BaseAudioSource(EngineSimHandle h, const EngineSimAPI& a);
    virtual ~BaseAudioSource() = default;

    bool generateAudio(std::vector<float>& buffer, int frames) override;
    void updateStats(const EngineSimStats& stats) override;
};

class EngineAudioSource : public BaseAudioSource {
public:
    EngineAudioSource(EngineSimHandle h, const EngineSimAPI& a);

    void displayProgress(double currentTime, double duration, bool interactive, const EngineSimStats& stats, double throttle, int underrunCount, const AudioPlayer* audioPlayer = nullptr) override;
};

// ============================================================================
// Shared Buffer Operations (DRY: same for both modes)
// ============================================================================

namespace BufferOps {
    void preFillCircularBuffer(AudioPlayer* player);
    void resetAndRePrefillBuffer(AudioPlayer* player);
}

// ============================================================================
// Shared Warmup Phase Logic
// ============================================================================

namespace WarmupOps {
    void runWarmup(EngineSimHandle handle, const EngineSimAPI& api, AudioPlayer* audioPlayer, bool playAudio);
}

// ============================================================================
// Timing Control
// ============================================================================

namespace TimingOps {
    struct LoopTimer {
        std::chrono::steady_clock::time_point absoluteStartTime;
        int iterationCount;

        LoopTimer();

        void sleepToMaintain60Hz();
    };
}

#endif // AUDIO_SOURCE_H
