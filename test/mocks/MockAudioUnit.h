// MockAudioUnit.h - Mock audio capture for integration testing
// Purpose: Capture all render calls for verification and testing
// Note: This is a simple capture class, not a full IAudioHardwareProvider implementation

#ifndef MOCK_AUDIO_UNIT_H
#define MOCK_AUDIO_UNIT_H

#include <vector>
#include <mutex>
#include <chrono>
#include <functional>

// ============================================================================
// MockAudioUnit - Simple capture class for render calls
// Used for verification in integration tests
// ============================================================================

class MockAudioUnit {
public:
    // Record of a single render call
    struct RenderCall {
        std::vector<float> samples;
        int framesRequested;
        int framesActuallyWritten;
        std::chrono::milliseconds timestamp;
        double callbackIntervalMs;

        RenderCall() : framesRequested(0), framesActuallyWritten(0),
                      timestamp(std::chrono::milliseconds(0)), callbackIntervalMs(0.0) {}

        RenderCall(int req, int written, std::chrono::milliseconds ts, double interval)
            : framesRequested(req)
            , framesActuallyWritten(written)
            , timestamp(ts)
            , callbackIntervalMs(interval)
        {
        }
    };

    // Constructor
    MockAudioUnit();
    ~MockAudioUnit() = default;

    // Simulate render callback (called by test harness)
    void simulateRender(float* buffer, int frames);

    // Test control methods
    void clearRenderCalls();
    size_t getRenderCallCount() const;
    std::vector<RenderCall> getRenderCalls() const;

    // Verification helpers
    bool hasExpectedSampleCount(int expectedCallCount) const;
    bool samplesWithinTolerance(const std::vector<float>& expected, float tolerance = 0.001f) const;
    bool noUnderrunsDetected() const;
    bool allFramesWritten(int expectedFramesPerCall) const;

    // Get latest render call
    bool getLatestRenderCall(RenderCall& outCall) const;

    // Get render call by index
    bool getRenderCall(size_t index, RenderCall& outCall) const;

    // Calculate statistics
    double getAverageFramesWritten() const;
    int getTotalFramesWritten() const;
    double getAverageCallbackInterval() const;

    // Enable/disable automatic buffer filling
    void setAutoFillBuffer(bool enable);
    bool getAutoFillBuffer() const;

    // Set custom render handler for advanced testing
    using RenderHandler = std::function<void(float*, int)>;
    void setRenderHandler(RenderHandler handler);

    // Get timing information
    std::chrono::milliseconds getFirstRenderTime() const;
    std::chrono::milliseconds getLastRenderTime() const;
    double getTotalRenderDurationMs() const;

private:
    std::vector<RenderCall> m_renderCalls;
    mutable std::mutex m_mutex;

    bool m_autoFillBuffer;
    RenderHandler m_renderHandler;

    std::chrono::milliseconds m_lastRenderTime;
};

#endif // MOCK_AUDIO_UNIT_H
