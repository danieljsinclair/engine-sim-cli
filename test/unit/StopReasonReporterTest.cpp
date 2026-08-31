// StopReasonReporterTest.cpp - unit tests for the honest stop-reason message
//
// Pins the contract extracted from CLIMain.cpp (owner-reported 2026-08-30):
// the stop message must name the cause that ACTUALLY ended the run:
//   - --end-at bound (provider-reported)  => "--end-at Ns reached."
//   - provider disconnect (live stream EOF) => "end of replay trace."
//   - duration timer                        => "Ns duration reached."
//   - interactive                           => "user quit"
// The old logic printed the full-trace duration message for any duration > 0,
// so --replay-telemetry --end-at 5 over a 536s capture claimed "536.726s
// duration reached".

#include <gtest/gtest.h>

#include <string>

#include "config/StopReasonReporter.h"

namespace {

bool mentionsEndAtBound(const std::string& msg) { return msg.find("--end-at") != std::string::npos; }
bool mentionsDuration(const std::string& msg) { return msg.find("duration reached") != std::string::npos; }
bool mentionsTraceEnd(const std::string& msg) { return msg.find("end of replay trace") != std::string::npos; }
bool mentionsUserQuit(const std::string& msg) { return msg.find("user quit") != std::string::npos; }

} // namespace

// Replay + --end-at 5 over a 10s trace: the provider stopped the session at
// the bound while still connected. Must report the bound, NOT the 10s trace
// length (the owner's observed lie).
TEST(StopReasonReporterTest, EndAtReached_ReportsBoundNotFullTraceDuration) {
    const std::string msg = playbackStopMessage(
        /*interactive=*/false, /*durationS=*/10.0,
        /*endAtReached=*/true, /*endAtS=*/5.0, /*inputExhausted=*/false);
    EXPECT_TRUE(mentionsEndAtBound(msg)) << msg;
    EXPECT_TRUE(msg.find("--end-at 5") != std::string::npos) << msg;
    EXPECT_FALSE(mentionsDuration(msg)) << msg;
}

// Live + --end-at with no --duration (duration 0): the bound ends the run.
TEST(StopReasonReporterTest, EndAtReached_LiveNoDuration_ReportsBound) {
    const std::string msg = playbackStopMessage(
        false, 0.0, true, 5.0, true);
    EXPECT_TRUE(mentionsEndAtBound(msg)) << msg;
    EXPECT_FALSE(mentionsTraceEnd(msg)) << msg;
}

// Live stream EOF before any bound (--duration 6 watchdog, capture drained at
// ~1s): the provider disconnected, end-at never fired. Must report the trace
// end, NOT claim the 6s duration was reached.
TEST(StopReasonReporterTest, StreamEofBeforeDuration_ReportsTraceEndNotDuration) {
    const std::string msg = playbackStopMessage(
        false, 6.0, /*endAtReached=*/false, -1.0, /*inputExhausted=*/true);
    EXPECT_TRUE(mentionsTraceEnd(msg)) << msg;
    EXPECT_FALSE(mentionsDuration(msg)) << msg;
}

// Live stream EOF that lands BEFORE the requested --end-at bound (short
// capture): the stream ended, the bound did not. Trace end, not the bound.
TEST(StopReasonReporterTest, StreamEofBeforeEndAt_ReportsTraceEndNotBound) {
    const std::string msg = playbackStopMessage(
        false, 0.0, /*endAtReached=*/false, 8.0, /*inputExhausted=*/true);
    EXPECT_TRUE(mentionsTraceEnd(msg)) << msg;
    EXPECT_FALSE(mentionsEndAtBound(msg)) << msg;
}

// Plain replay (no --end-at): still connected, duration = trace length and
// the duration timer genuinely ended the run. Duration message stays.
TEST(StopReasonReporterTest, DurationStop_ReportsDuration) {
    const std::string msg = playbackStopMessage(
        false, 10.0, false, -1.0, false);
    EXPECT_TRUE(mentionsDuration(msg)) << msg;
    EXPECT_TRUE(msg.find("10") != std::string::npos) << msg;
    EXPECT_FALSE(mentionsTraceEnd(msg)) << msg;
}

// Unbounded run, provider still connected, no bound: trace-end fallback.
TEST(StopReasonReporterTest, NoDurationNoExhaustion_ReportsTraceEnd) {
    const std::string msg = playbackStopMessage(
        false, 0.0, false, -1.0, false);
    EXPECT_TRUE(mentionsTraceEnd(msg)) << msg;
}

// Interactive mode: user quit wins.
TEST(StopReasonReporterTest, Interactive_ReportsUserQuit) {
    const std::string msg = playbackStopMessage(
        true, 10.0, false, -1.0, false);
    EXPECT_TRUE(mentionsUserQuit(msg)) << msg;
    EXPECT_FALSE(mentionsDuration(msg)) << msg;
}
