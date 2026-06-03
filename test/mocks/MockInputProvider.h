// MockInputProvider.h - Mock input provider for testing
// Purpose: Enable deterministic input testing for integration tests
// Provides controlled input for integration testing

#ifndef MOCK_INPUT_PROVIDER_H
#define MOCK_INPUT_PROVIDER_H

#include "io/IInputProvider.h"
#include <string>

namespace test {

/**
 * MockInputProvider - Test double for IInputProvider
 *
 * Provides controlled input for integration testing.
 * Stop signalling is handled by the session's stop() method,
 * not through the input provider.
 */
class MockInputProvider : public input::IInputProvider {
public:
    MockInputProvider() = default;
    ~MockInputProvider() override = default;

    // ================================================================
    // Lifecycle
    // ================================================================

    bool Initialize() override {
        connected_ = true;
        return true;
    }

    void Shutdown() override {
        connected_ = false;
    }

    bool IsConnected() const override {
        return connected_;
    }

    // ================================================================
    // Input Queries
    // ================================================================

    /**
     * Poll for input and return current engine inputs.
     */
    input::EngineInput OnUpdateSimulation(double dt) override {
        (void)dt; // Ignore delta time for mock

        // Build input from current state
        input::EngineInput input;
        input.throttle = throttle_;
        input.ignition = ignition_;
        input.starterButton = starterButton_;

        return input;
    }

    std::string GetProviderName() const override {
        return "MockInputProvider";
    }

    std::string GetLastError() const override {
        return lastError_;
    }

    // ================================================================
    // Mock Control Methods
    // ================================================================

    /**
     * Set throttle position (0.0 - 1.0)
     */
    void setThrottle(double throttle) {
        throttle_ = throttle;
    }

    /**
     * Set ignition state
     */
    void setIgnition(bool ignition) {
        ignition_ = ignition;
    }

    /**
     * Set starter motor state
     */
    void setStarterButton(bool pressed) {
        starterButton_ = pressed;
    }

    /**
     * Set error message (for testing error handling)
     */
    void setError(const std::string& error) {
        lastError_ = error;
    }

    /**
     * Reset to default state
     */
    void reset() {
        throttle_ = 0.1;
        ignition_ = true;
        starterButton_ = false;
        lastError_.clear();
        connected_ = false;
    }

private:
    // Input state
    double throttle_ = 0.1;
    bool ignition_ = true;
    bool starterButton_ = false;

    // State tracking
    bool connected_ = false;
    std::string lastError_;
};

} // namespace test

#endif // MOCK_INPUT_PROVIDER_H
