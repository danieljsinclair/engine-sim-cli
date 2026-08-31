// CommandLineTorqueFlagsTest.cpp — parse/default-off contracts for the two
// torque feature toggles:
//   --effective-throttle      (engine drive from AP commanded torque)
//   --torque-informed-gearbox (torque as a shift-decision input)
// Both DEFAULT OFF; off must be byte-identical to today's behaviour, which at
// the CLI layer means: flag absent -> args field false, nothing else changes.

#include "config/CLIconfig.h"
#include "gtest/gtest.h"

namespace {

CommandLineArgs parse(const std::vector<const char*>& argv) {
    CommandLineArgs args;
    std::vector<char*> mutableArgv;
    mutableArgv.reserve(argv.size());
    for (const char* arg : argv) mutableArgv.push_back(const_cast<char*>(arg));
    EXPECT_TRUE(parseArguments(static_cast<int>(mutableArgv.size()),
                               mutableArgv.data(), args));
    return args;
}

}  // namespace

TEST(CommandLineTorqueFlagsTest, DefaultsAreOffWhenFlagsAbsent) {
    // A plain invocation touching none of the torque plumbing: both toggles
    // must stay off — the byte-identical default path.
    const auto args = parse({"engine-sim-cli", "--script", "v8.mr"});

    EXPECT_FALSE(args.twin.effectiveThrottle);
    EXPECT_FALSE(args.twin.torqueInformedGearbox);
}

TEST(CommandLineTorqueFlagsTest, OtherFlagsLeaveTorqueTogglesOff) {
    // A busy live-telemetry invocation WITHOUT the torque flags: still off.
    const auto args = parse({"engine-sim-cli",
                             "--live-telemetry",
                             "--wheel-coupling", "pin",
                             "--pin-tau-ms", "150",
                             "--coupling-model", "torque-converter",
                             "--start"});

    EXPECT_FALSE(args.twin.effectiveThrottle);
    EXPECT_FALSE(args.twin.torqueInformedGearbox);
}

TEST(CommandLineTorqueFlagsTest, ParsesEffectiveThrottleFlag) {
    const auto args = parse({"engine-sim-cli", "--live-telemetry",
                             "--effective-throttle"});

    EXPECT_TRUE(args.twin.effectiveThrottle);
    EXPECT_FALSE(args.twin.torqueInformedGearbox);  // toggles are independent
}

TEST(CommandLineTorqueFlagsTest, ParsesTorqueInformedGearboxFlag) {
    const auto args = parse({"engine-sim-cli", "--live-telemetry",
                             "--torque-informed-gearbox"});

    EXPECT_TRUE(args.twin.torqueInformedGearbox);
    EXPECT_FALSE(args.twin.effectiveThrottle);      // toggles are independent
}

TEST(CommandLineTorqueFlagsTest, ParsesBothTorqueFlagsTogether) {
    // The features compose (audible engine AND informed shifts on the same AP
    // drive), so both flags on one invocation must parse.
    const auto args = parse({"engine-sim-cli", "--live-telemetry",
                             "--effective-throttle",
                             "--torque-informed-gearbox"});

    EXPECT_TRUE(args.twin.effectiveThrottle);
    EXPECT_TRUE(args.twin.torqueInformedGearbox);
}

TEST(CommandLineTorqueFlagsTest, EffectiveThrottleParsesWithoutTelemetry) {
    // The flag is inert config, not a mode: it must parse standalone without
    // demanding --live-telemetry/--replay-telemetry.
    const auto args = parse({"engine-sim-cli", "--effective-throttle"});

    EXPECT_TRUE(args.twin.effectiveThrottle);
}
