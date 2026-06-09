// KeyboardInputProviderTest.cpp - Tests for consolidated KeyboardInputProvider
// Proves: KeyHoldBridge correctly separates edge-triggered vs level-triggered keys

#include <gtest/gtest.h>
#include "input/KeyboardInputProvider.h"
#include "input/EngineInputTarget.h"
#include "MockKeyboardInput.h"

using namespace input;

class KeyboardInputProviderTest : public ::testing::Test {
protected:
    MockKeyboardInput* rawMock_{nullptr};
    EngineInputTarget* rawTarget_{nullptr};
    std::unique_ptr<KeyboardInputProvider> provider_;
    std::unique_ptr<EngineInputTarget> target_;

    void SetUp() override {
        auto mock = std::make_unique<MockKeyboardInput>();
        rawMock_ = mock.get();
        auto target = std::make_unique<EngineInputTarget>();
        rawTarget_ = target.get();
        provider_ = std::make_unique<KeyboardInputProvider>(std::move(mock), target.get());
        ASSERT_TRUE(provider_->Initialize());
        target_.reset(target.release());
    }

    EngineInput tick() {
        return provider_->OnUpdateSimulation(0.016);
    }
};

// Brake: pressing 'b' sets brakeLevel to 1.0 (level-triggered)
TEST_F(KeyboardInputProviderTest, BrakePress_SetsBrakeLevel) {
    rawMock_->enqueue('b');
    EngineInput input = tick();
    EXPECT_DOUBLE_EQ(input.brakeLevel, 1.0);
}

// Brake: releasing 'b' resets brakeLevel to 0.0 after timeout
TEST_F(KeyboardInputProviderTest, BrakeReleased_ResetsToZero) {
    rawMock_->enqueue('b');
    EngineInput input1 = tick();
    EXPECT_DOUBLE_EQ(input1.brakeLevel, 1.0);

    // KeyHoldBridge timeout will eventually expire
    bool brakeWentToZero = false;
    for (int i = 0; i < 20; ++i) {
        EngineInput input = tick();
        if (input.brakeLevel == 0.0) {
            brakeWentToZero = true;
            break;
        }
    }
    EXPECT_TRUE(brakeWentToZero)
        << "After releasing brake, brakeLevel should eventually reset to 0.0";
}

// Brake held via OS repeat stays at 1.0
TEST_F(KeyboardInputProviderTest, BrakeHeld_StaysOn) {
    for (int i = 0; i < 10; ++i) {
        rawMock_->enqueue('b');
        EngineInput input = tick();
        EXPECT_DOUBLE_EQ(input.brakeLevel, 1.0);
    }
}

// No key → brakeLevel is 0.0
TEST_F(KeyboardInputProviderTest, NoKey_BrakeIsZero) {
    EngineInput input = tick();
    EXPECT_DOUBLE_EQ(input.brakeLevel, 0.0);
}

// 'w' key increases throttle by exactly 0.05 (one step per press)
TEST_F(KeyboardInputProviderTest, WKey_IncreasesThrottleByOneStep) {
    EngineInput baseline = tick();
    double baselineThrottle = baseline.throttle;

    rawMock_->enqueue('w');
    EngineInput input = tick();
    EXPECT_DOUBLE_EQ(input.throttle, baselineThrottle + 0.05);
}

// OS repeat for 'w' continues to ramp throttle (isKeyRepeating fires on repeat)
TEST_F(KeyboardInputProviderTest, WKeyRepeat_ContinuesRampingThrottle) {
    rawMock_->enqueue('w');
    EngineInput input1 = tick();
    double afterFirst = input1.throttle;

    // OS repeat — isKeyRepeating fires, throttle ramps again
    rawMock_->enqueue('w');
    EngineInput input2 = tick();
    EXPECT_DOUBLE_EQ(input2.throttle, afterFirst + 0.05)
        << "OS repeat should continue ramping throttle";
}

// Space does NOT repeat (edge-triggered only)
TEST_F(KeyboardInputProviderTest, SpaceKeyRepeat_DoesNotReZero) {
    rawMock_->enqueue(' ');
    EngineInput input1 = tick();
    EXPECT_DOUBLE_EQ(input1.throttle, 0.0);

    // Ramp throttle up first
    rawMock_->enqueue('w');
    tick();

    // Space repeat — should NOT zero throttle again (space is keyPressed only)
    rawMock_->enqueue(' ');
    EngineInput input3 = tick();
    EXPECT_GT(input3.throttle, 0.0)
        << "Space repeat should NOT re-zero throttle";
}

// Space key zeros throttle (edge-triggered)
TEST_F(KeyboardInputProviderTest, SpaceKey_ZerosThrottle) {
    rawMock_->enqueue(' ');
    EngineInput input = tick();
    EXPECT_DOUBLE_EQ(input.throttle, 0.0);
}

// 'r' key sets 20% throttle (edge-triggered)
TEST_F(KeyboardInputProviderTest, RKey_Sets20PercentThrottle) {
    rawMock_->enqueue('r');
    EngineInput input = tick();
    EXPECT_DOUBLE_EQ(input.throttle, 0.2);
}

// ']' key shifts up once (edge-triggered, one step per press)
TEST_F(KeyboardInputProviderTest, BracketRight_ShiftsUp) {
    rawMock_->enqueue(']');
    EngineInput input = tick();
    EXPECT_EQ(input.gearDelta, 1);
}

// ']' repeated does NOT shift again (isKeyPressed deduplicates)
TEST_F(KeyboardInputProviderTest, BracketRightRepeat_DoesNotShiftAgain) {
    rawMock_->enqueue(']');
    EngineInput input1 = tick();
    ASSERT_EQ(input1.gearDelta, 1);

    rawMock_->enqueue(']');
    EngineInput input2 = tick();
    EXPECT_EQ(input2.gearDelta, 0)
        << "OS repeat should NOT shift gear again";
}

// '[' key shifts down (edge-triggered)
TEST_F(KeyboardInputProviderTest, BracketLeft_ShiftsDown) {
    rawMock_->enqueue(']');
    tick();

    rawMock_->enqueue('[');
    EngineInput input = tick();
    EXPECT_EQ(input.gearDelta, -1);
}

// 'i' key toggles ignition (edge-triggered)
TEST_F(KeyboardInputProviderTest, IKey_TogglesIgnition) {
    EngineInput baseline = tick();
    bool baselineIgnition = baseline.ignition;

    rawMock_->enqueue('i');
    EngineInput input = tick();
    EXPECT_EQ(input.ignition, !baselineIgnition);
}

// 's' key sets starterButton momentary (one frame, then false)
TEST_F(KeyboardInputProviderTest, SKey_SetsStarterButtonMomentary) {
    rawMock_->enqueue('s');
    EngineInput input = tick();
    EXPECT_TRUE(input.starterButton);

    EngineInput nextInput = tick();
    EXPECT_FALSE(nextInput.starterButton);
}

// 'p' key sets presetCycle momentary (one frame, then false)
TEST_F(KeyboardInputProviderTest, PKey_SetsPresetCycleMomentary) {
    rawMock_->enqueue('p');
    EngineInput input = tick();
    EXPECT_TRUE(input.presetCycle);

    EngineInput nextInput = tick();
    EXPECT_FALSE(nextInput.presetCycle);
}
