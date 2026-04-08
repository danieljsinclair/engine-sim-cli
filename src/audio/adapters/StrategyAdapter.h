// StrategyAdapter.h - Adapter to bridge IAudioStrategy to IAudioRenderer
// Allows gradual migration from old to new architecture
// SOLID: Adapter pattern maintains interface contract

#ifndef STRATEGY_ADAPTER_H
#define STRATEGY_ADAPTER_H

#include "audio/strategies/IAudioStrategy.h"
#include "audio/renderers/IAudioRenderer.h"
#include "audio/state/StrategyContext.h"

// Forward declaration to avoid full include of SimulationConfig
class SimulationConfig;

/**
 * StrategyAdapter - Adapter that bridges IAudioStrategy to IAudioRenderer interface
 *
 * This adapter allows existing AudioPlayer to use new IAudioStrategy implementations
 * while maintaining the old IAudioRenderer interface contract.
 *
 * SRP: Only adapts one interface to another
 * OCP: Can add new strategies without modifying this adapter
 * DIP: Depends on abstractions (IAudioStrategy, IAudioRenderer)
 */
class StrategyAdapter : public IAudioRenderer {
public:
    /**
     * Constructor - Adapts IAudioStrategy to IAudioRenderer interface
     * @param strategy The IAudioStrategy to adapt (owned by adapter)
     * @param context The StrategyContext to use (owned by adapter)
     * @param logger Optional logger for diagnostics (non-owning)
     */
    StrategyAdapter(std::unique_ptr<IAudioStrategy> strategy,
                 std::unique_ptr<StrategyContext> context,
                 ILogging* logger = nullptr);

    ~StrategyAdapter() override = default;

    // ================================================================
    // IAudioRenderer Implementation
    // All methods delegate to IAudioStrategy
    // ================================================================

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
    void configure(const SimulationConfig& config) override;  // Keep original signature for compatibility

    std::unique_ptr<AudioUnitContext> createContext(
        int sampleRate,
        EngineSimHandle engineHandle,
        const EngineSimAPI* engineAPI
    ) override;

    bool render(
        void* context,
        AudioBufferList* ioData,
        UInt32 numberFrames
    ) override;

    bool isEnabled() const override;
    const char* getName() const override;
    bool AddFrames(void* context, float* buffer, int frameCount) override;

    // ================================================================
    // Adapter-specific methods
    // ================================================================

    /**
     * Get the adapted strategy
     * @return Pointer to the IAudioStrategy being adapted
     */
    IAudioStrategy* getStrategy() const { return strategy_.get(); }

    /**
     * Get the strategy context
     * @return Pointer to the StrategyContext
     */
    StrategyContext* getStrategyContext() const { return context_.get(); }

private:
    std::unique_ptr<IAudioStrategy> strategy_;
    std::unique_ptr<StrategyContext> context_;
    int sampleRate_;
    EngineSimHandle engineHandle_;
    const EngineSimAPI* engineAPI_;
    AudioUnitContext* mockContext_;  // Non-owning pointer to mock context for isPlaying updates
    ILogging* logger_;  // Logger for diagnostics (non-owning)

    /**
     * Create mock AudioUnitContext from StrategyContext
     * This bridges the new StrategyContext to the old AudioUnitContext
     */
    std::unique_ptr<AudioUnitContext> createMockContext(
        int sampleRate,
        EngineSimHandle engineHandle,
        const EngineSimAPI* engineAPI
    );
};

#endif // STRATEGY_ADAPTER_H
