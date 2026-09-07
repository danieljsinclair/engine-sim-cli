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

// ============================================================================
// EngineInputTarget direct tests for setter/getter paths, decay, one-shots
// ============================================================================

class EngineInputTargetTest : public ::testing::Test {
protected:
    EngineInputTarget target_;

    EngineInputTargetTest() : target_(nullptr) {}
};

TEST_F(EngineInputTargetTest, DefaultState_AllZero) {
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.throttle, 0.0);
    EXPECT_TRUE(input.ignition);  // Default ignition is ON
    EXPECT_FALSE(input.starterButton);
    EXPECT_EQ(input.gearDelta, 0);
    EXPECT_EQ(input.gearSelector, 0);
    EXPECT_DOUBLE_EQ(input.dynoTorqueScale, -1.0);
    EXPECT_DOUBLE_EQ(input.brakeLevel, 0.0);
    EXPECT_FALSE(input.presetCycle);
    EXPECT_FALSE(input.gearAutoMode);
    EXPECT_DOUBLE_EQ(input.roadSpeedKmh, -1.0);
    EXPECT_FALSE(target_.quitRequested());
}

TEST_F(EngineInputTargetTest, SetThrottle_SetsValueAndLatches) {
    target_.setThrottle(0.5);
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.throttle, 0.5);
}

TEST_F(EngineInputTargetTest, AdjustThrottle_IncrementsFromCurrent) {
    target_.setThrottle(0.2);
    target_.adjustThrottle(0.1);
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.throttle, 0.3);
}

TEST_F(EngineInputTargetTest, AdjustThrottle_ClampsToRange) {
    target_.setThrottle(0.95);
    target_.adjustThrottle(0.1);  // Would exceed 1.0
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.throttle, 1.0);
}

TEST_F(EngineInputTargetTest, AdjustThrottle_NegativeClampsToZero) {
    target_.setThrottle(0.1);
    target_.adjustThrottle(-0.2);
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.throttle, 0.0);
}

TEST_F(EngineInputTargetTest, SetThrottleMomentary_ActivatesMomentaryMode) {
    target_.setThrottle(0.5);  // Latch at 0.5
    target_.setThrottleMomentary(0.8);  // Momentary override to 0.8
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.throttle, 0.8);
}

TEST_F(EngineInputTargetTest, ThrottleDecay_TowardLatchWhenMomentary) {
    target_.setThrottle(0.5);  // Latch at 0.5
    target_.setThrottleMomentary(0.8);  // Momentary to 0.8
    target_.buildInput();  // First frame at 0.8

    // Next frame: should decay toward 0.5 (0.8 + (0.8-0.5)*0.85 = 0.745)
    EngineInput input = target_.buildInput();
    EXPECT_LT(input.throttle, 0.8);
    EXPECT_GT(input.throttle, 0.5);
}

TEST_F(EngineInputTargetTest, ThrottleDecay_EventuallyReachesLatch) {
    target_.setThrottle(0.5);
    target_.setThrottleMomentary(0.8);
    target_.buildInput();

    // Iterate until decay completes
    double throttle = 0.8;
    for (int i = 0; i < 50; ++i) {
        EngineInput input = target_.buildInput();
        throttle = input.throttle;
        if (std::abs(throttle - 0.5) < 0.01) break;
    }
    EXPECT_NEAR(throttle, 0.5, 0.01);
}

TEST_F(EngineInputTargetTest, SetThrottle_ExitsMomentaryMode) {
    target_.setThrottleMomentary(0.8);
    target_.buildInput();
    target_.setThrottle(0.3);  // New explicit throttle clears momentary
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.throttle, 0.3);
    // Subsequent frames should stay at 0.3 without decay
    input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.throttle, 0.3);
}

TEST_F(EngineInputTargetTest, ShiftUp_IncrementsGearDelta) {
    target_.shiftUp();
    EngineInput input = target_.buildInput();
    EXPECT_EQ(input.gearDelta, 1);
    // gearDelta is one-shot
    input = target_.buildInput();
    EXPECT_EQ(input.gearDelta, 0);
}

TEST_F(EngineInputTargetTest, ShiftDown_DecrementsGearDelta) {
    target_.shiftDown();
    EngineInput input = target_.buildInput();
    EXPECT_EQ(input.gearDelta, -1);
    input = target_.buildInput();
    EXPECT_EQ(input.gearDelta, 0);
}

TEST_F(EngineInputTargetTest, ShiftUp_IncrementsGearSelector) {
    target_.shiftUp();  // 0 -> 1
    target_.buildInput();
    target_.shiftUp();  // 1 -> 2
    target_.buildInput();
    target_.shiftUp();  // 2 -> 3
    EngineInput input = target_.buildInput();
    EXPECT_EQ(input.gearSelector, 3);
}

TEST_F(EngineInputTargetTest, ShiftDown_DecrementsGearSelector) {
    target_.shiftUp(); target_.buildInput();
    target_.shiftUp(); target_.buildInput();  // selector = 2
    target_.shiftDown(); target_.buildInput();  // selector = 1
    EngineInput input = target_.buildInput();
    EXPECT_EQ(input.gearSelector, 1);
}

TEST_F(EngineInputTargetTest, ShiftUp_ClampsToMaxGear) {
    for (int i = 0; i < 10; ++i) {
        target_.shiftUp();
        target_.buildInput();
    }
    EngineInput input = target_.buildInput();
    EXPECT_LE(input.gearSelector, 8);
}

TEST_F(EngineInputTargetTest, ShiftDown_ClampsToReverse) {
    target_.shiftDown();  // 0 -> -1 (REVERSE)
    target_.buildInput();
    target_.shiftDown();  // Should stay at -1
    target_.buildInput();
    EngineInput input = target_.buildInput();
    EXPECT_GE(input.gearSelector, -1);
}

TEST_F(EngineInputTargetTest, ToggleIgnition_FlipsState) {
    EXPECT_TRUE(target_.buildInput().ignition);
    target_.toggleIgnition();
    EXPECT_FALSE(target_.buildInput().ignition);
    target_.toggleIgnition();
    EXPECT_TRUE(target_.buildInput().ignition);
}

TEST_F(EngineInputTargetTest, SetStarter_OneShotTrueThenFalse) {
    target_.setStarter();
    EXPECT_TRUE(target_.buildInput().starterButton);
    EXPECT_FALSE(target_.buildInput().starterButton);
}

TEST_F(EngineInputTargetTest, CyclePreset_OneShotTrueThenFalse) {
    target_.cyclePreset();
    EXPECT_TRUE(target_.buildInput().presetCycle);
    EXPECT_FALSE(target_.buildInput().presetCycle);
}

TEST_F(EngineInputTargetTest, AdjustDynoTorque_IncrementsFromZero) {
    target_.adjustDynoTorque(0.3);
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.dynoTorqueScale, 0.3);
}

TEST_F(EngineInputTargetTest, AdjustDynoTorque_ClampsToRange) {
    target_.adjustDynoTorque(0.8);
    target_.adjustDynoTorque(0.5);  // Would exceed 1.0
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.dynoTorqueScale, 1.0);
}

TEST_F(EngineInputTargetTest, AdjustDynoTorque_NegativeClampsToZero) {
    target_.adjustDynoTorque(0.2);
    target_.adjustDynoTorque(-0.5);
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.dynoTorqueScale, 0.0);
}

TEST_F(EngineInputTargetTest, ReleaseDynoTorque_SetsToZero) {
    target_.adjustDynoTorque(0.5);
    target_.buildInput();
    target_.releaseDynoTorque();
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.dynoTorqueScale, 0.0);
}

TEST_F(EngineInputTargetTest, SetBrake_SetsLevel) {
    target_.setBrake(0.7);
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.brakeLevel, 0.7);
}

TEST_F(EngineInputTargetTest, AdjustSpeed_IncrementsFromNegativeSentinel) {
    target_.adjustSpeed(50.0);
    EngineInput input = target_.buildInput();
    // Initial roadSpeedKmh_ is -1.0, so 50 + (-1) = 49.0
    EXPECT_DOUBLE_EQ(input.roadSpeedKmh, 49.0);
}

TEST_F(EngineInputTargetTest, AdjustSpeed_ClampsToRange) {
    target_.adjustSpeed(200.0);
    target_.adjustSpeed(150.0);  // Would exceed 300
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.roadSpeedKmh, 300.0);
}

TEST_F(EngineInputTargetTest, AdjustSpeed_NegativeClampsToZero) {
    target_.adjustSpeed(50.0);
    target_.adjustSpeed(-100.0);
    EngineInput input = target_.buildInput();
    EXPECT_DOUBLE_EQ(input.roadSpeedKmh, 0.0);
}

TEST_F(EngineInputTargetTest, Quit_SetsRequestedFlag) {
    EXPECT_FALSE(target_.quitRequested());
    target_.quit();
    EXPECT_TRUE(target_.quitRequested());
}

TEST_F(EngineInputTargetTest, SetGearAutoMode_EnablesAutoMode) {
    target_.setGearAutoMode(true);
    EngineInput input = target_.buildInput();
    EXPECT_TRUE(input.gearAutoMode);
}

TEST_F(EngineInputTargetTest, AutoMode_DisablesManualShift) {
    target_.setGearAutoMode(true);
    target_.shiftUp();
    EngineInput input = target_.buildInput();
    EXPECT_EQ(input.gearDelta, 0);  // Should not shift in auto mode
    EXPECT_EQ(input.gearSelector, 0);
}

TEST_F(EngineInputTargetTest, BuildInput_ResetsOneShots) {
    target_.setStarter();
    target_.cyclePreset();
    target_.shiftUp();
    target_.shiftDown();  // gearDelta = -1

    EngineInput input1 = target_.buildInput();
    EXPECT_TRUE(input1.starterButton);
    EXPECT_TRUE(input1.presetCycle);
    EXPECT_EQ(input1.gearDelta, -1);

    // Next buildInput should have all one-shots reset
    EngineInput input2 = target_.buildInput();
    EXPECT_FALSE(input2.starterButton);
    EXPECT_FALSE(input2.presetCycle);
    EXPECT_EQ(input2.gearDelta, 0);
}

TEST_F(EngineInputTargetTest, BuildInput_ResetsThrottleTouched) {
    target_.setThrottle(0.5);
    target_.buildInput();  // throttleTouched_ = false now
    target_.setThrottleMomentary(0.8);
    target_.buildInput();  // momentary active

    // Without new throttleTouched, should decay
    EngineInput input = target_.buildInput();
    EXPECT_LT(input.throttle, 0.8);
}
