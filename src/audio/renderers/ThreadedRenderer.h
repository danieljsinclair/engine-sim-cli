// ThreadedRenderer.h - Threaded/cursor-chasing renderer (consolidated)
// Combines lifecycle and rendering for cursor-chasing mode with separate audio thread
// Uses hardware feedback to maintain 100ms lead, preventing underruns

#ifndef THREADED_RENDERER_H
#define THREADED_RENDERER_H

#include <memory>
#include <string>

#include "engine_sim_bridge.h"
#include "audio/renderers/IAudioRenderer.h"
#include "ILogging.h"

class AudioPlayer;
class AudioUnitContext;
class IAudioSource;
class CircularBuffer;
struct SimulationConfig;

class ThreadedRenderer : public IAudioRenderer {
public:
    // Constructor with DI logger (defaults to ConsoleLogger if null)
    explicit ThreadedRenderer(ILogging* logger = nullptr);

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

    const char* getName() const override { return "ThreadedRenderer"; }

    bool AddFrames(void* ctx, float* buffer, int frameCount) override;

private:
    // Logging: owns ConsoleLogger by default, or uses injected logger
    std::unique_ptr<ConsoleLogger> defaultLogger_;
    ILogging* logger_;  // Non-null, points to defaultLogger_ or injected logger
};

#endif // THREADED_RENDERER_H
