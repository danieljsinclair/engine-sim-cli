// MockInputProvider.h - Mock input provider for testing
// Purpose: Enable deterministic input testing for integration tests
// Provides controlled input including shutdown signaling via shouldContinue=false

#ifndef MOCK_INPUT_PROVIDER_H
#define MOCK_INPUT_PROVIDER_H

#include "io/IInputProvider.h"
#include <string>

namespace test {

/**
 * MockInputProvider - Test double for IInputProvider
 *
 * Provides controlled input for integration testing, including
 * the ability to signal shutdown via shouldContinue=false.
 * This enables clean shutdown without relying on ISimulator::stop().
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
        // Note: We don't set shouldContinue_ here - that's controlled
        // by test code via setShouldContinue()
    }

    bool IsConnected() const override {
        return connected_;
    }

    // ================================================================
    // Input Queries
    // ================================================================

    /**
     * Poll for input and return current engine inputs.
     * Returns the current input state, with shouldContinue
     * controlling whether the simulation should continue running.
     */
    input::EngineInput OnUpdateSimulation(double dt) override {
        (void)dt; // Ignore delta time for mock

        // Build input from current state
        input::EngineInput input;
        input.throttle = throttle_;
        input.ignition = ignition_;
        input.starterMotor = starterMotor_;
        input.shouldContinue = shouldContinue_;

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
    void setStarterMotor(bool starter) {
        starterMotor_ = starter;
    }

    /**
     * Set shouldContinue flag for shutdown signaling
     * When false, the simulation loop will terminate cleanly
     */
    void setShouldContinue(bool shouldContinue) {
        shouldContinue_ = shouldContinue;
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
        starterMotor_ = false;
        shouldContinue_ = true;
        lastError_.clear();
        connected_ = false;
    }

private:
    // Input state
    double throttle_ = 0.1;
    bool ignition_ = true;
    bool starterMotor_ = false;
    bool shouldContinue_ = true;

    // State tracking
    bool connected_ = false;
    std::string lastError_;
};

} // namespace test

#endif // MOCK_INPUT_PROVIDER_H
