// SyncPullRenderer.h - Sync-pull renderer (consolidated)
// Combines lifecycle and rendering for lock-step mode where engine simulation advances with audio playback
// Renders audio on-demand synchronously from the engine simulator

#ifndef SYNC_PULL_RENDERER_H
#define SYNC_PULL_RENDERER_H

#include <memory>
#include <string>

#include "engine_sim_bridge.h"
#include "audio/renderers/IAudioRenderer.h"
#include "ILogging.h"

class AudioPlayer;
class AudioUnitContext;
class IAudioSource;
class SyncPullAudio;
struct SimulationConfig;

class SyncPullRenderer : public IAudioRenderer {
public:
    // Constructor with DI logger (defaults to ConsoleLogger if null)
    explicit SyncPullRenderer(ILogging* logger = nullptr);

    // === IAudioRenderer lifecycle methods ===

    std::string getModeName() const override;

    void updateSimulation(EngineSimHandle handle, const EngineSimAPI& api,
                          AudioPlayer* audioPlayer) override;

    void generateAudio(IAudioSource& audioSource, AudioPlayer* audioPlayer) override;

    std::string getModeString() const override;

    bool startAudioThread(EngineSimHandle handle, const EngineSimAPI& api,
                          AudioPlayer* audioPlayer) override;

    void prepareBuffer(AudioPlayer* audioPlayer) override;

    void resetBufferAfterWarmup(AudioPlayer* audioPlayer) override;

    void startPlayback(AudioPlayer* audioPlayer) override;

    bool shouldDrainDuringWarmup() const override;

    void configure(const SimulationConfig& config) override;

    std::unique_ptr<AudioUnitContext> createContext(
        int sampleRate,
        EngineSimHandle engineHandle,
        const EngineSimAPI* engineAPI
    ) override;

    // === IAudioRenderer rendering methods ===

    bool render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) override;

    bool isEnabled() const override;

    const char* getName() const override { return "SyncPullRenderer"; }

    bool AddFrames(void* ctx, float* buffer, int frameCount) override;

private:
    SyncPullAudio* syncPullAudio_ = nullptr;  // Raw pointer to avoid cycles
    int preFillMs_ = 50;  // Pre-fill buffer duration in ms

    // Logging: owns ConsoleLogger by default, or uses injected logger
    std::unique_ptr<ConsoleLogger> defaultLogger_;
    ILogging* logger_;  // Non-null, points to defaultLogger_ or injected logger
};

#endif // SYNC_PULL_RENDERER_H
