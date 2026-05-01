// MockDataSimulator.h - Mock simulator with fixed known values for integration testing
// Purpose: Enable deterministic integration testing of audio rendering mechanics

#ifndef MOCK_DATA_SIMULATOR_H
#define MOCK_DATA_SIMULATOR_H

#include "simulator/EngineSimTypes.h"
#include <cstring>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>

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

    // Audio buffer configuration — single source of truth via EngineSimDefaults
    static constexpr size_t AUDIO_BUFFER_SIZE = EngineSimDefaults::AUDIO_BUFFER_SIZE;
    static constexpr int SAMPLE_RATE = EngineSimDefaults::SAMPLE_RATE;

    // Initialize with fixed known values
    void initialize(const ISimulatorConfig* config);

    // Render audio - generates fixed known values
    bool renderAudio(float* buffer, int32_t frames, int32_t* outSamplesWritten);

    // Read audio buffer - for sync-pull mode
    bool readAudioBuffer(float* buffer, int32_t frames, int32_t* outSamplesRead);

    // Render on-demand - for sync-pull callbacks
    bool renderOnDemand(float* buffer, int32_t frames, int32_t* outSamplesWritten);

    // Reset state for test isolation
    void reset();

    // Accessors for test verification
    size_t getReadPosition() const { return m_readPosition; }
    size_t getWritePosition() const { return m_writePosition; }
    size_t getBufferSize() const { return m_audioBuffer.size(); }

    // Control functions
    bool setThrottle(double position);
    bool setSpeedControl(double position);
    const char* getLastError();

private:
    // Generate fixed known sample for given frame and channel
    int16_t generateFixedSample(int frame, int channel) const;

    // Convert int16 sample to float [-1.0, 1.0]
    float int16ToFloat(int16_t sample) const;

    // Get current frame number (for generating deterministic samples)
    int getCurrentFrame() const { return m_currentFrame; }

    // Update simulation state
    bool update(double deltaTime);

    // Start audio thread (mock)
    bool start();

    // Get simulation statistics
    bool getStats(EngineSimStats* outStats);

    // Member variables
    EngineSimStats m_stats;
    ISimulatorConfig m_config;
    std::vector<int16_t> m_audioBuffer;
    std::vector<float> m_conversionBuffer;
    std::atomic<std::size_t> m_readPosition{0};
    std::atomic<std::size_t> m_writePosition{0};
    std::atomic<int> m_currentFrame{0};
    std::atomic<double> m_throttle{0.0};
    std::atomic<double> m_speedControl{0.0};
    std::atomic<bool> m_audioThreadRunning{false};
    std::string m_lastError;
    std::mutex m_mutex;
};

// ============================================================================
// MockDataSimulatorHandle - Opaque handle wrapper
// ============================================================================

using MockDataSimulatorHandle = MockDataSimulatorContext*;

// ============================================================================
// MockDataSimulator - Test-friendly wrapper for MockDataSimulatorContext
// Provides convenient interface for unit tests
// Note: Basic ThreadedStrategy tests don't need engine API
// ============================================================================

class MockDataSimulator {
public:
    MockDataSimulator();
    ~MockDataSimulator();

    // Initialize the simulator
    void initialize(const ISimulatorConfig* config = nullptr);

    // Get context handle (for StrategyContext)
    MockDataSimulatorHandle getContext() const { return m_context.get(); }

    // Note: Basic tests don't need engine API, returning nullptr
    // For advanced tests that need engine API, we can add proper mocking later

    // Reset state for test isolation
    void reset() { m_context->reset(); }

private:
    std::unique_ptr<MockDataSimulatorContext> m_context;
};

#endif // MOCK_DATA_SIMULATOR_H
