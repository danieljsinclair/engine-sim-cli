// MockDataSimulator.h - Mock simulator with fixed known values for integration testing
// Purpose: Enable deterministic integration testing of audio rendering mechanics

#ifndef MOCK_DATA_SIMULATOR_H
#define MOCK_DATA_SIMULATOR_H

#include "engine_sim_bridge.h"
#include <vector>
#include <atomic>
#include <mutex>

// ============================================================================
// MockDataSimulatorContext - Mock context mimicking EngineSimContext
// Provides fixed known values for deterministic integration testing
// ============================================================================

class MockDataSimulatorContext {
public:
    MockDataSimulatorContext();
    ~MockDataSimulatorContext() = default;

    // Fixed known audio values for deterministic testing
    // Pattern: left channel = +1000 + frame, right channel = +2000 + frame
    // This pattern is different from ThreadedRenderer baseline (+0/+1) to distinguish tests
    static constexpr int16_t FIXED_LEFT_BASE = 1000;
    static constexpr int16_t FIXED_RIGHT_BASE = 2000;

    // Fixed simulator state
    static constexpr double FIXED_RPM_AT_IDLE = 800.0;
    static constexpr double FIXED_LOAD_AT_IDLE = 0.0;
    static constexpr double MAX_RPM = 6000.0;

    // Audio buffer configuration
    static constexpr size_t AUDIO_BUFFER_SIZE = 96000;  // 2 seconds @ 48kHz (stereo samples)
    static constexpr int SAMPLE_RATE = 48000;

    // Initialize with fixed known values
    void initialize(const EngineSimConfig* config);

    // Render audio - generates fixed known values
    EngineSimResult renderAudio(float* buffer, int32_t frames, int32_t* outSamplesWritten);

    // Read audio buffer - for sync-pull mode
    EngineSimResult readAudioBuffer(float* buffer, int32_t frames, int32_t* outSamplesRead);

    // Render on-demand - for sync-pull callbacks
    EngineSimResult renderOnDemand(float* buffer, int32_t frames, int32_t* outSamplesWritten);

    // Control functions
    EngineSimResult setThrottle(double position);
    EngineSimResult setSpeedControl(double position);
    EngineSimResult update(double deltaTime);
    EngineSimResult startAudioThread();
    EngineSimResult getStats(EngineSimStats* outStats);
    const char* getLastError();

    // Get current frame number (for generating deterministic samples)
    int getCurrentFrame() const { return m_currentFrame; }

    // Reset state for test isolation
    void reset();

    // Accessors for test verification
    size_t getReadPosition() const { return m_readPosition; }
    size_t getWritePosition() const { return m_writePosition; }
    size_t getBufferSize() const { return m_audioBuffer.size(); }

private:
    // Generate fixed known sample for given frame and channel
    int16_t generateFixedSample(int frame, int channel) const;

    // Convert int16 sample to float [-1.0, 1.0]
    float int16ToFloat(int16_t sample) const;

    // Fill buffer with fixed known audio
    void fillAudioBuffer(int framesToGenerate);

    // State
    std::vector<int16_t> m_audioBuffer;      // Internal audio buffer (int16 stereo)
    std::atomic<size_t> m_writePosition{0}; // Write pointer
    std::atomic<size_t> m_readPosition{0};   // Read pointer
    std::atomic<int> m_currentFrame{0};      // Current frame for deterministic generation

    // Control state
    std::atomic<double> m_throttle{0.0};
    std::atomic<double> m_speedControl{0.0};

    // Configuration
    EngineSimConfig m_config;

    // Statistics
    EngineSimStats m_stats;

    // Error handling
    std::string m_lastError;
    std::mutex m_mutex;  // Protect buffer access

    // Thread management
    std::atomic<bool> m_audioThreadRunning{false};

    // Conversion buffer
    std::vector<int16_t> m_conversionBuffer;
};

// ============================================================================
// MockDataSimulatorHandle - Opaque handle wrapper
// ============================================================================

using MockDataSimulatorHandle = MockDataSimulatorContext*;

#endif // MOCK_DATA_SIMULATOR_H
