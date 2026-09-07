#include <gtest/gtest.h>
#include "config/CLIconfig.h"
#include "config/ANSIColors.h"

#include <iostream>
#include <sstream>
#include <streambuf>

// RAII helper to capture std::cout output
class OutputCapture {
public:
    explicit OutputCapture(std::ostream& target) : target_(target), oldBuf_(target.rdbuf()) {
        target_.rdbuf(buffer_.rdbuf());
    }
    ~OutputCapture() {
        target_.rdbuf(oldBuf_);
    }
    std::string str() const { return buffer_.str(); }
    void reset() { buffer_.str(""); buffer_.clear(); }

private:
    std::ostream& target_;
    std::streambuf* oldBuf_;
    std::ostringstream buffer_;
};

// ============================================================================
// printUsage tests (from CLIconfig.cpp) with DI for cout
// ============================================================================

TEST(CLIConfigPrintUsageTest, PrintUsage_OutputsHeader) {
    OutputCapture capture(std::cout);
    printUsage("engine-sim-cli");

    EXPECT_NE(capture.str().find("Engine Simulator CLI v2.0"), std::string::npos);
}

TEST(CLIConfigPrintUsageTest, PrintUsage_OutputsUsageLine) {
    OutputCapture capture(std::cout);
    printUsage("my-program");

    EXPECT_NE(capture.str().find("Usage: my-program [options]"), std::string::npos);
    EXPECT_NE(capture.str().find("--script <engine_config.mr|.json>"), std::string::npos);
}

TEST(CLIConfigPrintUsageTest, PrintUsage_OutputsOptions) {
    OutputCapture capture(std::cout);
    printUsage("engine-sim-cli");

    EXPECT_NE(capture.str().find("--script"), std::string::npos);
    EXPECT_NE(capture.str().find("--load"), std::string::npos);
    EXPECT_NE(capture.str().find("--interactive"), std::string::npos);
    EXPECT_NE(capture.str().find("--play"), std::string::npos);
    EXPECT_NE(capture.str().find("--duration"), std::string::npos);
    EXPECT_NE(capture.str().find("--output"), std::string::npos);
    EXPECT_NE(capture.str().find("--connect-demo"), std::string::npos);
    EXPECT_NE(capture.str().find("--auto"), std::string::npos);
    EXPECT_NE(capture.str().find("--manual"), std::string::npos);
    EXPECT_NE(capture.str().find("--sine"), std::string::npos);
    EXPECT_NE(capture.str().find("--threaded"), std::string::npos);
    EXPECT_NE(capture.str().find("--silent"), std::string::npos);
    EXPECT_NE(capture.str().find("--cranking-volume"), std::string::npos);
    EXPECT_NE(capture.str().find("--sim-freq"), std::string::npos);
    EXPECT_NE(capture.str().find("--synth-latency"), std::string::npos);
    EXPECT_NE(capture.str().find("--pre-fill-ms"), std::string::npos);
    EXPECT_NE(capture.str().find("--diagnostic-frames"), std::string::npos);
    EXPECT_NE(capture.str().find("--diagnostic-freq"), std::string::npos);
}

TEST(CLIConfigPrintUsageTest, PrintUsage_OutputsNotes) {
    OutputCapture capture(std::cout);
    printUsage("engine-sim-cli");

    EXPECT_NE(capture.str().find("NOTES:"), std::string::npos);
    EXPECT_NE(capture.str().find("engine-sim-bridge/preset/"), std::string::npos);
    EXPECT_NE(capture.str().find("dyno brake mode"), std::string::npos);
    EXPECT_NE(capture.str().find("sync-pull"), std::string::npos);
    EXPECT_NE(capture.str().find("cursor-chasing"), std::string::npos);
}

TEST(CLIConfigPrintUsageTest, PrintUsage_OutputsInteractiveControls) {
    OutputCapture capture(std::cout);
    printUsage("engine-sim-cli");

    EXPECT_NE(capture.str().find("Interactive Controls:"), std::string::npos);
    EXPECT_NE(capture.str().find("Toggle ignition"), std::string::npos);
    EXPECT_NE(capture.str().find("Toggle starter"), std::string::npos);
    EXPECT_NE(capture.str().find("Increase/decrease throttle"), std::string::npos);
    EXPECT_NE(capture.str().find("Apply brake"), std::string::npos);
    EXPECT_NE(capture.str().find("Reset to idle"), std::string::npos);
    EXPECT_NE(capture.str().find("Increase dyno load"), std::string::npos);
    EXPECT_NE(capture.str().find("Decrease dyno load"), std::string::npos);
    EXPECT_NE(capture.str().find("Release dyno"), std::string::npos);
    EXPECT_NE(capture.str().find("Shift up"), std::string::npos);
    EXPECT_NE(capture.str().find("shift down"), std::string::npos);
    EXPECT_NE(capture.str().find("Cycle to next engine preset"), std::string::npos);
    EXPECT_NE(capture.str().find("Quit"), std::string::npos);
}

TEST(CLIConfigPrintUsageTest, PrintUsage_OutputsExamples) {
    OutputCapture capture(std::cout);
    printUsage("engine-sim-cli");

    EXPECT_NE(capture.str().find("Examples:"), std::string::npos);
    EXPECT_NE(capture.str().find("--interactive --play"), std::string::npos);
    EXPECT_NE(capture.str().find("--script v8_engine.mr --load 50"), std::string::npos);
    EXPECT_NE(capture.str().find("--sine --interactive --play"), std::string::npos);
    EXPECT_NE(capture.str().find("--load 75 --play"), std::string::npos);
}

TEST(CLIConfigPrintUsageTest, PrintUsage_IncludesProgramNameInExamples) {
    OutputCapture capture(std::cout);
    const char* progName = "custom-cli";
    printUsage(progName);

    EXPECT_NE(capture.str().find("custom-cli --interactive --play"), std::string::npos);
    EXPECT_NE(capture.str().find("custom-cli --script v8_engine.mr"), std::string::npos);
    EXPECT_NE(capture.str().find("custom-cli --sine"), std::string::npos);
    EXPECT_NE(capture.str().find("custom-cli --load 75"), std::string::npos);
}

TEST(CLIConfigPrintUsageTest, PrintUsage_IncludesSimFrequencyDefault) {
    OutputCapture capture(std::cout);
    printUsage("engine-sim-cli");

    std::string output = capture.str();
    EXPECT_NE(output.find(std::to_string(EngineSimDefaults::SIMULATION_FREQUENCY)), std::string::npos);
}

TEST(CLIConfigPrintUsageTest, PrintUsage_IncludesSynthLatencyDefault) {
    OutputCapture capture(std::cout);
    printUsage("engine-sim-cli");

    std::string output = capture.str();
    EXPECT_NE(output.find("0.02"), std::string::npos);
}

TEST(CLIConfigPrintUsageTest, PrintUsage_IncludesPreFillDefault) {
    OutputCapture capture(std::cout);
    printUsage("engine-sim-cli");

    std::string output = capture.str();
    EXPECT_NE(output.find(std::to_string(EngineSimDefaults::DEFAULT_PREFILL_MS)), std::string::npos);
}

// ============================================================================
// parseReplayTimeToSeconds tests (boundary values)
// ============================================================================

TEST(CLIConfigParseTimeTest, ParseSeconds_PlainNumber) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("30"), 30.0);
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("0"), 0.0);
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("1.5"), 1.5);
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("120.25"), 120.25);
}

TEST(CLIConfigParseTimeTest, ParseMinutesSeconds) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("1:30"), 90.0);
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("2:00"), 120.0);
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("0:05"), 5.0);
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("10:30.5"), 630.5);
}

TEST(CLIConfigParseTimeTest, ParseHoursMinutesSeconds) {
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("1:00:00"), 3600.0);
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("0:01:30"), 90.0);
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("0:00:05"), 5.0);
    EXPECT_DOUBLE_EQ(parseReplayTimeToSeconds("2:30:15.5"), 9015.5);
}

TEST(CLIConfigParseTimeTest, ParseInvalid_ReturnsNegative) {
    EXPECT_LT(parseReplayTimeToSeconds(""), 0.0);
    EXPECT_LT(parseReplayTimeToSeconds("invalid"), 0.0);
    EXPECT_LT(parseReplayTimeToSeconds("1:2:3:4"), 0.0);
    EXPECT_LT(parseReplayTimeToSeconds(":30"), 0.0);
    EXPECT_LT(parseReplayTimeToSeconds("30:"), 0.0);
    EXPECT_LT(parseReplayTimeToSeconds("1::30"), 0.0);
}

// ============================================================================
// ANSIColors message function tests (from CLIConfig.cpp's four *Message functions)
// ============================================================================

TEST(CLIConfigANSIColorsTest, infoMessage_ReturnsWrappedString) {
    std::string result = ANSIColors::infoMessage("test message");
    EXPECT_NE(result.find(ANSIColors::INFO), std::string::npos);
    EXPECT_NE(result.find("test message"), std::string::npos);
    EXPECT_NE(result.find(ANSIColors::RESET), std::string::npos);
}

TEST(CLIConfigANSIColorsTest, OKMessage_ReturnsWrappedString) {
    std::string result = ANSIColors::OKMessage("test message");
    EXPECT_NE(result.find(ANSIColors::GREEN), std::string::npos);
    EXPECT_NE(result.find("test message"), std::string::npos);
    EXPECT_NE(result.find(ANSIColors::RESET), std::string::npos);
}

TEST(CLIConfigANSIColorsTest, warningMessage_ReturnsWrappedString) {
    std::string result = ANSIColors::warningMessage("test message");
    EXPECT_NE(result.find(ANSIColors::YELLOW), std::string::npos);
    EXPECT_NE(result.find("test message"), std::string::npos);
    EXPECT_NE(result.find(ANSIColors::RESET), std::string::npos);
}

TEST(CLIConfigANSIColorsTest, errorMessage_ReturnsWrappedString) {
    std::string result = ANSIColors::errorMessage("test message");
    EXPECT_NE(result.find(ANSIColors::RED), std::string::npos);
    EXPECT_NE(result.find("test message"), std::string::npos);
    EXPECT_NE(result.find(ANSIColors::RESET), std::string::npos);
}

TEST(CLIConfigANSIColorsTest, AllMessageFunctions_ContainInputMessage) {
    const std::string testMsg = "Hello World!";

    EXPECT_NE(ANSIColors::infoMessage(testMsg).find(testMsg), std::string::npos);
    EXPECT_NE(ANSIColors::OKMessage(testMsg).find(testMsg), std::string::npos);
    EXPECT_NE(ANSIColors::warningMessage(testMsg).find(testMsg), std::string::npos);
    EXPECT_NE(ANSIColors::errorMessage(testMsg).find(testMsg), std::string::npos);
}

TEST(CLIConfigANSIColorsTest, AllMessageFunctions_EmptyString_ReturnsWrappedEmpty) {
    EXPECT_NE(ANSIColors::infoMessage("").find(ANSIColors::INFO), std::string::npos);
    EXPECT_NE(ANSIColors::OKMessage("").find(ANSIColors::GREEN), std::string::npos);
    EXPECT_NE(ANSIColors::warningMessage("").find(ANSIColors::YELLOW), std::string::npos);
    EXPECT_NE(ANSIColors::errorMessage("").find(ANSIColors::RED), std::string::npos);
}

// NOTE: validateReplayTimeSlicing(), CreateSimulationConfig(), reportStopReason(),
// and createInputProvider() all live in an anonymous namespace inside CLIMain.cpp
// (internal linkage). They CANNOT be unit-tested directly from this translation unit.
// See CLIMain.cpp lines 63-101 (namespace { ... }).
//
// These functions are exercised indirectly via the smoke tests (test/smoke/*), which
// invoke the real engine-sim-cli binary with --replay-telemetry, --auto, --connect-demo
// etc. and capture its exit code + output. That is the testable surface for these paths
// until the production code is refactored to expose them (see options in PR description).
