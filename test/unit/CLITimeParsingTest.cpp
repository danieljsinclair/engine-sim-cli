// CLITimeParsingTest.cpp - Unit tests for parseReplayTimeToSeconds() and
// CLI11 wiring for --start-from / --end-at options.
//
// These tests verify the bug reported where --start-from 99 (and other values)
// caused the replay to start at ~02:26 instead of the specified time.

#include <gtest/gtest.h>
#include "config/CLIconfig.h"

// ============================================================================
// parseReplayTimeToSeconds — plain seconds
// ============================================================================

TEST(CLITimeParsing, PlainSeconds_99) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("99"), 99.0);
}

TEST(CLITimeParsing, PlainSeconds_500) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("500"), 500.0);
}

TEST(CLITimeParsing, PlainSeconds_1234) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("1234"), 1234.0);
}

TEST(CLITimeParsing, PlainSeconds_999) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("999"), 999.0);
}

TEST(CLITimeParsing, PlainSeconds_Zero) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("0"), 0.0);
}

TEST(CLITimeParsing, PlainSeconds_LargeValue) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("3600"), 3600.0);
}

// ============================================================================
// parseReplayTimeToSeconds — fractional seconds
// ============================================================================

TEST(CLITimeParsing, FractionalSeconds_0_500) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("0.500"), 0.5);
}

TEST(CLITimeParsing, FractionalSeconds_12_500) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("12.500"), 12.5);
}

TEST(CLITimeParsing, FractionalSeconds_DotPrefix) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds(".500"), 0.5);
}

// ============================================================================
// parseReplayTimeToSeconds — mm:ss
// ============================================================================

TEST(CLITimeParsing,_mm_ss_01_00) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("01:00"), 60.0);
}

TEST(CLITimeParsing, mm_ss_01_30_500) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("01:30.500"), 90.5);
}

TEST(CLITimeParsing, mm_ss_02_26) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("02:26"), 146.0);
}

TEST(CLITimeParsing, mm_ss_00_00) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("00:00"), 0.0);
}

TEST(CLITimeParsing, mm_ss_59_59) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("59:59"), 3599.0);
}

// ============================================================================
// parseReplayTimeToSeconds — hh:mm:ss
// ============================================================================

TEST(CLITimeParsing, hh_mm_ss_01_00_00) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("01:00:00"), 3600.0);
}

TEST(CLITimeParsing, hh_mm_ss_01_00_00_500) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("01:00:00.500"), 3600.5);
}

TEST(CLITimeParsing, hh_mm_ss_00_00_00) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("00:00:00"), 0.0);
}

TEST(CLITimeParsing, hh_mm_ss_02_26_00) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("02:26:00"), 8760.0);
}

// ============================================================================
// parseReplayTimeToSeconds — invalid inputs
// ============================================================================

TEST(CLITimeParsing, Invalid_EmptyString) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds(""), -1.0);
}

TEST(CLITimeParsing, Invalid_Nonsense) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("abc"), -1.0);
}

TEST(CLITimeParsing, Invalid_TooManyColons) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("1:2:3:4"), -1.0);
}

TEST(CLITimeParsing, Invalid_TrailingColon) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("01:"), -1.0);
}

TEST(CLITimeParsing, Invalid_LeadingColon) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds(":01"), -1.0);
}

// ============================================================================
// Integration: CLI11 wiring for --start-from and --end-at
// ============================================================================

// Helper: simulate argv as a mutable vector (CLI11 takes char**).
static std::vector<std::string> makeArgv(std::vector<std::string> args) {
    return args;
}

static CommandLineArgs parseArgv(std::vector<std::string> args) {
    CommandLineArgs parsed;
    // CLI11 requires argv[0] to be program name
    args.insert(args.begin(), "engine-sim-cli");
    // Need --replay-telemetry to satisfy --start-from's needs() constraint
    if (std::find(args.begin(), args.end(), "--start-from") != args.end() ||
        std::find(args.begin(), args.end(), "--end-at") != args.end()) {
        args.push_back("--replay-telemetry");
        args.push_back("dummy.csv");
    }
    std::vector<char*> argv;
    for (auto& s : args) argv.push_back(s.data());
    parseArguments(static_cast<int>(argv.size()), argv.data(), parsed);
    return parsed;
}

// --start-from with plain seconds
TEST(CLIWiring, StartFrom_PlainSeconds_99) {
    auto args = parseArgv({"--start-from", "99"});
    EXPECT_DOUBLE_EQ(args.replay.startFromS, 99.0);
    EXPECT_EQ(args.replay.startFrom, "99");
}

TEST(CLIWiring, StartFrom_PlainSeconds_500) {
    auto args = parseArgv({"--start-from", "500"});
    EXPECT_DOUBLE_EQ(args.replay.startFromS, 500.0);
}

TEST(CLIWiring, StartFrom_PlainSeconds_1234) {
    auto args = parseArgv({"--start-from", "1234"});
    EXPECT_DOUBLE_EQ(args.replay.startFromS, 1234.0);
}

TEST(CLIWiring, StartFrom_PlainSeconds_999) {
    auto args = parseArgv({"--start-from", "999"});
    EXPECT_DOUBLE_EQ(args.replay.startFromS, 999.0);
}

// --start-from with hh:mm:ss
TEST(CLIWiring, StartFrom_hh_mm_ss_01_00_00) {
    auto args = parseArgv({"--start-from", "01:00:00"});
    EXPECT_DOUBLE_EQ(args.replay.startFromS, 3600.0);
}

// --start-from with mm:ss
TEST(CLIWiring, StartFrom_mm_ss_02_26) {
    auto args = parseArgv({"--start-from", "02:26"});
    EXPECT_DOUBLE_EQ(args.replay.startFromS, 146.0);
}

// --end-at with plain seconds
TEST(CLIWiring, EndAt_PlainSeconds_200) {
    auto args = parseArgv({"--end-at", "200"});
    EXPECT_DOUBLE_EQ(args.replay.endAtS, 200.0);
}

// --end-at with hh:mm:ss
TEST(CLIWiring, EndAt_hh_mm_ss_01_00_00) {
    auto args = parseArgv({"--end-at", "01:00:00"});
    EXPECT_DOUBLE_EQ(args.replay.endAtS, 3600.0);
}

// Both --start-from and --end-at together
TEST(CLIWiring, StartFrom_And_EndAt) {
    auto args = parseArgv({"--start-from", "99", "--end-at", "200"});
    EXPECT_DOUBLE_EQ(args.replay.startFromS, 99.0);
    EXPECT_DOUBLE_EQ(args.replay.endAtS, 200.0);
}

// Without --start-from: replayStartFromS should remain -1.0 (disabled)
TEST(CLIWiring, NoStartFrom_DefaultDisabled) {
    const char* argv[] = {"engine-sim-cli", "--replay-telemetry", "dummy.csv"};
    CommandLineArgs args;
    EXPECT_TRUE(parseArguments(3, const_cast<char**>(argv), args));
    EXPECT_DOUBLE_EQ(args.replay.startFromS, -1.0);
    EXPECT_TRUE(args.replay.startFrom.empty());
}

// --start-from without --replay-telemetry should fail (needs() constraint)
TEST(CLIWiring, StartFrom_WithoutReplayTelemetry_Fails) {
    const char* argv[] = {"engine-sim-cli", "--start-from", "99"};
    CommandLineArgs args;
    EXPECT_FALSE(parseArguments(3, const_cast<char**>(argv), args));
}

// --start-from with invalid time should fail
TEST(CLIWiring, StartFrom_InvalidTime_Fails) {
    const char* argv[] = {"engine-sim-cli", "--replay-telemetry", "dummy.csv",
                          "--start-from", "abc"};
    CommandLineArgs args;
    EXPECT_FALSE(parseArguments(5, const_cast<char**>(argv), args));
}

// Verify that --replay-telemetry path is correctly captured alongside --start-from
TEST(CLIWiring, ReplayTelemetryPath_WithStartFrom) {
    const char* argv[] = {"engine-sim-cli", "--replay-telemetry", "my_capture.csv",
                          "--start-from", "99"};
    CommandLineArgs args;
    EXPECT_TRUE(parseArguments(5, const_cast<char**>(argv), args));
    EXPECT_EQ(args.replay.telemetryPath, "my_capture.csv");
    EXPECT_DOUBLE_EQ(args.replay.startFromS, 99.0);
}
