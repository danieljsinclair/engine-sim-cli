// KeyboardInputProviderTest.cpp - TDD: RED phase for brake detection
// Proves: pressing 'b' sets brakeLevel in EngineInput

#include <gtest/gtest.h>
#include "input/KeyboardInputProvider.h"
#include "MockKeyboardInput.h"

using namespace input;

class KeyboardInputProviderBrakeTest : public ::testing::Test {
protected:
    MockKeyboardInput* rawMock_{nullptr};
    std::unique_ptr<KeyboardInputProvider> provider_;

    void SetUp() override {
        auto mock = std::make_unique<MockKeyboardInput>();
        rawMock_ = mock.get();
        provider_ = std::make_unique<KeyboardInputProvider>(std::move(mock));
        ASSERT_TRUE(provider_->Initialize());
    }

    EngineInput tick() {
        return provider_->OnUpdateSimulation(0.016);
    }
};

// RED: pressing 'b' should set brakeLevel to 1.0
TEST_F(KeyboardInputProviderBrakeTest, BrakePress_SetsBrakeLevel) {
    rawMock_->enqueue('b');
    EngineInput input = tick();
    EXPECT_DOUBLE_EQ(input.brakeLevel, 1.0)
        << "Pressing 'b' should set brakeLevel to 1.0";
}

// RED: no key → brakeLevel stays 0.0
TEST_F(KeyboardInputProviderBrakeTest, NoKey_BrakeIsZero) {
    EngineInput input = tick();
    EXPECT_DOUBLE_EQ(input.brakeLevel, 0.0);
}

// RED: space key zeros throttle
TEST_F(KeyboardInputProviderBrakeTest, SpaceKey_ZerosThrottle) {
    rawMock_->enqueue(' ');
    EngineInput input = tick();
    EXPECT_DOUBLE_EQ(input.throttle, 0.0)
        << "Pressing ' ' should set throttle to 0.0";
}

// RED: 'r' key sets 20% throttle
TEST_F(KeyboardInputProviderBrakeTest, RKey_Sets20PercentThrottle) {
    rawMock_->enqueue('r');
    EngineInput input = tick();
    EXPECT_DOUBLE_EQ(input.throttle, 0.2)
        << "Pressing 'r' should set throttle to 0.2";
}

// RED: 'w' key increases throttle by 0.05
TEST_F(KeyboardInputProviderBrakeTest, WKey_IncreasesThrottle) {
    // Get baseline throttle
    EngineInput baseline = tick();
    double baselineThrottle = baseline.throttle;

    rawMock_->enqueue('w');
    EngineInput input = tick();
    EXPECT_DOUBLE_EQ(input.throttle, baselineThrottle + 0.05)
        << "Pressing 'w' should increase throttle by 0.05";
}

// RED: ']' key shifts up (gearDelta == 1)
TEST_F(KeyboardInputProviderBrakeTest, BracketRight_ShiftsUp) {
    rawMock_->enqueue(']');
    EngineInput input = tick();
    EXPECT_EQ(input.gearDelta, 1)
        << "Pressing ']' should set gearDelta to 1";
}

// RED: '[' key shifts down (gearDelta == -1)
TEST_F(KeyboardInputProviderBrakeTest, BracketLeft_ShiftsDown) {
    // First shift up to establish state
    rawMock_->enqueue(']');
    tick();

    // Now shift down
    rawMock_->enqueue('[');
    EngineInput input = tick();
    EXPECT_EQ(input.gearDelta, -1)
        << "Pressing '[' should set gearDelta to -1";
}

// RED: 'i' key toggles ignition
TEST_F(KeyboardInputProviderBrakeTest, IKey_TogglesIgnition) {
    // Get baseline ignition state
    EngineInput baseline = tick();
    bool baselineIgnition = baseline.ignition;

    rawMock_->enqueue('i');
    EngineInput input = tick();
    EXPECT_EQ(input.ignition, !baselineIgnition)
        << "Pressing 'i' should toggle ignition state";
}

// RED: 's' key sets starterButton momentary (true for one frame, then false)
TEST_F(KeyboardInputProviderBrakeTest, SKey_SetsStarterButtonMomentary) {
    rawMock_->enqueue('s');
    EngineInput input = tick();
    EXPECT_TRUE(input.starterButton)
        << "Pressing 's' should set starterButton to true";

    // Tick again with no key
    EngineInput nextInput = tick();
    EXPECT_FALSE(nextInput.starterButton)
        << "After one frame, starterButton should be false";
}

// RED: 'p' key sets presetCycle momentary (true for one frame, then false)
TEST_F(KeyboardInputProviderBrakeTest, PKey_SetsPresetCycleMomentary) {
    rawMock_->enqueue('p');
    EngineInput input = tick();
    EXPECT_TRUE(input.presetCycle)
        << "Pressing 'p' should set presetCycle to true";

    // Tick again with no key
    EngineInput nextInput = tick();
    EXPECT_FALSE(nextInput.presetCycle)
        << "After one frame, presetCycle should be false";
}

// RED: With KeyHoldBridge, holding 'w' keeps increasing throttle (level-triggered)
TEST_F(KeyboardInputProviderBrakeTest, WKeyHeld_IncreasesThrottleContinuously) {
    EngineInput baseline = tick();
    double baselineThrottle = baseline.throttle;

    // Press 'w' once
    rawMock_->enqueue('w');
    EngineInput input1 = tick();
    EXPECT_GT(input1.throttle, baselineThrottle)
        << "Pressing 'w' should increase throttle";

    // Press 'w' again (simulating OS key repeat)
    rawMock_->enqueue('w');
    EngineInput input2 = tick();
    EXPECT_GT(input2.throttle, input1.throttle)
        << "Holding 'w' (level-triggered) should keep increasing throttle";
}

// RED: BRAKE LATCH BUG - brakeLevel never resets to zero after release
TEST_F(KeyboardInputProviderBrakeTest, BrakeReleased_ResetsToZero) {
    // Press brake
    rawMock_->enqueue('b');
    EngineInput input1 = tick();
    EXPECT_DOUBLE_EQ(input1.brakeLevel, 1.0)
        << "Pressing 'b' should set brakeLevel to 1.0";

    // Release brake - KeyHoldBridge timeout will eventually reset brake to 0
    // We don't test exact timing, just that it DOES go to zero
    bool brakeWentToZero = false;
    for (int i = 0; i < 20; ++i) {  // generous upper bound
        EngineInput input = tick();
        if (input.brakeLevel == 0.0) {
            brakeWentToZero = true;
            break;
        }
    }

    EXPECT_TRUE(brakeWentToZero)
        << "After releasing brake, brakeLevel should eventually reset to 0.0";
}
