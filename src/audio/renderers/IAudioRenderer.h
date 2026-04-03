// IAudioRenderer.h - Unified renderer interface (consolidated from IAudioMode + IAudioRenderer)
// Strategy pattern - abstracts rendering mode behavior (sync-pull vs threaded)
// OCP: New renderers can be added without modifying existing code
// SRP: Each renderer encapsulates its own lifecycle and rendering logic
// DI: Renderer is injected via constructor

#ifndef IAUDIO_RENDERER_H
#define IAUDIO_RENDERER_H

#include <memory>
#include <string>

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>

#include "engine_sim_bridge.h"
#include "bridge/engine_sim_loader.h"
#include "ILogging.h"

class AudioPlayer;
class AudioUnitContext;
class IAudioSource;
struct SimulationConfig;

class IAudioRenderer {
public:
    virtual ~IAudioRenderer() = default;

    // === Lifecycle Methods ===

    // Get mode name for diagnostics
    virtual std::string getModeName() const = 0;

    // Update simulation - different strategies for each mode
    virtual void updateSimulation(EngineSimHandle handle, const EngineSimAPI& api,
                                   AudioPlayer* audioPlayer) = 0;

    // Generate audio - threaded mode generates audio in main loop
    virtual void generateAudio(IAudioSource& audioSource, AudioPlayer* audioPlayer) = 0;

    // Get mode string for diagnostics display
    virtual std::string getModeString() const = 0;

    // Start audio thread if needed (threaded mode only)
    virtual bool startAudioThread(EngineSimHandle handle, const EngineSimAPI& api,
                                   AudioPlayer* audioPlayer) = 0;

    // Prepare audio buffer for playback
    virtual void prepareBuffer(AudioPlayer* audioPlayer) = 0;

    // Reset buffer after warmup (threaded mode only)
    virtual void resetBufferAfterWarmup(AudioPlayer* audioPlayer) = 0;

    // Start playback
    virtual void startPlayback(AudioPlayer* audioPlayer) = 0;

    // Check if drain is needed during warmup
    virtual bool shouldDrainDuringWarmup() const = 0;

    // Configure mode with simulation config
    virtual void configure(const SimulationConfig& config) = 0;

    // Factory method: Create the audio context for AudioPlayer
    // Each renderer creates its own context with appropriate audio components
    virtual std::unique_ptr<AudioUnitContext> createContext(
        int sampleRate,
        EngineSimHandle engineHandle,
        const EngineSimAPI* engineAPI
    ) = 0;

    // === Rendering Methods ===

    // Render audio to the buffer list - returns true if rendering succeeded
    virtual bool render(
        void* context,
        AudioBufferList* ioData,
        UInt32 numberFrames
    ) = 0;

    // Check if this renderer is active/enabled
    virtual bool isEnabled() const = 0;

    // Get the name of this renderer for diagnostics
    virtual const char* getName() const = 0;

    // Add frames to the renderer's buffer (for playBuffer compatibility)
    // Returns true if frames were added successfully
    virtual bool AddFrames(void* context, float* buffer, int frameCount) = 0;
};

std::unique_ptr<IAudioRenderer> createAudioRendererFactory(const EngineSimAPI* engineAPI, bool preferSyncPull = true, ILogging* logger = nullptr);

#endif // IAUDIO_RENDERER_H
