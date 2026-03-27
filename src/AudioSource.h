// AudioSource.h - Audio source abstraction interface and implementations
// Uses SineGenerator for sine wave generation (DRY principle)

#ifndef AUDIO_SOURCE_H
#define AUDIO_SOURCE_H

#include <memory>
#include <vector>

#include "engine_sim_bridge.h"
#include "engine_sim_loader.h"
#include "SineGenerator.h"

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
    SineGenerator sineGenerator_;
    double currentRPM_;
    bool useSineGenerator_;

public:
    BaseAudioSource(EngineSimHandle h, const EngineSimAPI& a, bool useSine);
    virtual ~BaseAudioSource() = default;

    // Common implementation using SineGenerator - DRY between modes
    bool generateAudio(std::vector<float>& buffer, int frames) override;

    // Update stats - needed for threaded mode to get RPM
    void updateStats(const EngineSimStats& stats) override;

    // Allow subclasses to update RPM for sine generation
    void setCurrentRPM(double rpm) { currentRPM_ = rpm; }
    double getCurrentRPM() const { return currentRPM_; }

    // Access to sine generator for external frequency updates
    SineGenerator& getSineGenerator() { return sineGenerator_; }
};

// Sine wave audio source - uses SineGenerator for DRY sine generation
// Both sync-pull and threaded modes use the same SineGenerator
class SineAudioSource : public BaseAudioSource {
public:
    SineAudioSource(EngineSimHandle h, const EngineSimAPI& a);

    void displayProgress(double currentTime, double duration, bool interactive, const EngineSimStats& stats, double throttle, int underrunCount, const AudioPlayer* audioPlayer = nullptr) override;
};

// Engine audio source - uses engine API for audio generation
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
