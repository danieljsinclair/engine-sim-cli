// DemoKeyboardInputProviderTest.cpp - End-to-end test for keyboard → brake chain
// Proves: MockKeyboardInput → DemoKeyboardInputProvider → DemoInputProvider → EngineInput.brakeLevel

#include <gtest/gtest.h>
#include "input/DemoKeyboardInputProvider.h"
#include "input/DemoInputProvider.h"
#include "input/DemoThrottleSource.h"
#include "input/GearSelectorInput.h"
#include "input/IgnitionInput.h"
#include "input/IDemoControls.h"
#include "MockKeyboardInput.h"
#include "twin/IceVehicleProfile.h"

using namespace input;
using namespace twin;

class DemoKeyboardInputProviderTest : public ::testing::Test {
protected:
    IceVehicleProfile profile_{IceVehicleProfile::zf8hp45()};
    MockKeyboardInput* rawMock_{nullptr};

    std::unique_ptr<DemoKeyboardInputProvider> wrapper_;

    void SetUp() override {
        auto mockKb = std::make_unique<MockKeyboardInput>();
        rawMock_ = mockKb.get();

        auto throttle = std::make_unique<DemoThrottleSource>();
        auto gearSelector = std::make_unique<GearSelectorInput>();
        auto ignition = std::make_unique<IgnitionInput>();

        auto provider = std::make_unique<DemoInputProvider>(
            std::move(throttle),
            std::move(gearSelector),
            std::move(ignition),
            profile_
        );

        IDemoControls* controls = dynamic_cast<IDemoControls*>(provider.get());

        wrapper_ = std::make_unique<DemoKeyboardInputProvider>(
            std::move(mockKb),
            std::move(provider),
            controls
        );

        ASSERT_TRUE(wrapper_->Initialize());
    }

    EngineInput tick() {
        return wrapper_->OnUpdateSimulation(0.016);
    }
};

// PROVE: pressing 'b' → brakeLevel = 1.0
TEST_F(DemoKeyboardInputProviderTest, BrakePress_ShowsInEngineInput) {
    rawMock_->enqueue('b');
    EngineInput input = tick();
    EXPECT_DOUBLE_EQ(input.brakeLevel, 1.0)
        << "Single 'b' press should set brakeLevel to 1.0";
}

// PROVE: no key → brakeLevel = 0.0
TEST_F(DemoKeyboardInputProviderTest, NoKey_BrakeIsZero) {
    EngineInput input = tick();
    EXPECT_DOUBLE_EQ(input.brakeLevel, 0.0);
}

// PROVE: held 'b' (OS key repeat) → brakeLevel stays 1.0
TEST_F(DemoKeyboardInputProviderTest, BrakeHeld_StaysOnViaRepeat) {
    for (int i = 0; i < 20; ++i) {
        rawMock_->enqueue('b');
        EngineInput input = tick();
        EXPECT_DOUBLE_EQ(input.brakeLevel, 1.0)
            << "brakeLevel should be 1.0 on frame " << (i + 1) << " while key held";
    }
}

// PROVE: brake releases after key is released (no more OS repeat events)
// NOTE: We don't assert the exact timeout duration — that's KeyHoldBridge's contract
TEST_F(DemoKeyboardInputProviderTest, BrakeReleased_GoesToZeroAfterRelease) {
    // Hold brake via OS repeat for a few frames
    for (int i = 0; i < 5; ++i) {
        rawMock_->enqueue('b');
        EngineInput input = tick();
        EXPECT_DOUBLE_EQ(input.brakeLevel, 1.0)
            << "brakeLevel should be 1.0 while key is held";
    }

    // Release key — no more OS repeat events
    // KeyHoldBridge's timeout will eventually expire, but we don't test exact timing
    // Instead, verify that brake DOES go to zero after enough frames with no events
    bool brakeWentToZero = false;
    for (int i = 0; i < 20; ++i) {  // generous upper bound
        EngineInput input = tick();
        if (input.brakeLevel == 0.0) {
            brakeWentToZero = true;
            break;
        }
    }

    EXPECT_TRUE(brakeWentToZero)
        << "brakeLevel should eventually reach 0.0 after key release";
}

// PROVE: brake and other controls don't interfere
TEST_F(DemoKeyboardInputProviderTest, BrakeDoesNotAffectOtherControls) {
    rawMock_->enqueue('b');
    EngineInput input = tick();
    EXPECT_DOUBLE_EQ(input.brakeLevel, 1.0);
    // Gear should still come from twin (not affected by brake)
    EXPECT_GE(input.gearAbsolute, 0);
}
