// test_capture_eof_exit.cpp - capture-EOF termination smoke tests
//
// Owner decision 2026-08-30 ("EOF = immediate termination"): when the
// telemetry capture ends, the WHOLE CLI stops there and then — no grace
// window, no simulating the last sample forever. Before the fix the live
// stdin path (--live-telemetry) held the last sample and ran unbounded at
// capture end (only --duration/--end-at bounded it), and a --replay-telemetry
// --end-at run reported the FULL trace length as its stop reason.
//
// These tests run the real binary:
//   1. --live-telemetry < finite 1s capture: exits just after the capture
//      drains (~1s) and prints "end of replay trace." NOTE: --duration cannot
//      be combined with --live-telemetry (the telemetry input bounds the run;
//      the fail-fast refuses the combo), so there is no duration watchdog —
//      the test relies on the capture's own EOF to terminate. (Both the wall
//      time and the message are red discriminators; the wall assert alone
//      would be flake-prone, the message assert alone would not prove prompt
//      exit — together they pin the contract.)
//   2. --replay-telemetry 10s capture --end-at 5: stops at 5s and must say
//      so ("--end-at 5s reached"), not claim the 10s trace duration.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "SmokeTestHelper.h"

namespace {

std::string logPath() {
    return SmokeTestHelper::getProjectRoot() + "/build/cli_test_" +
           std::to_string(getpid()) + ".log";
}

// The log is shared (appended) by every runCLI call in this process, so read
// only this run's delta: snapshot the size before, read from there after.
long long logSize() {
    std::ifstream f(logPath(), std::ios::binary | std::ios::ate);
    return f ? static_cast<long long>(f.tellg()) : 0;
}

std::string logDelta(long long from) {
    std::ifstream f(logPath(), std::ios::binary);
    f.seekg(static_cast<std::streamoff>(from));
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeCapture(const std::string& path, double endS, double stepS) {
    std::ofstream f(path);
    f << "time_s,throttle_pct,road_speed_kmh\n";
    for (double t = 0.0; t <= endS + 1e-9; t += stepS) {
        f << t << ",20,30\n";
    }
}

int exitCodeOf(int raw) {
    return WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
}

} // namespace

TEST(CaptureEofExitTest, LiveStdinEof_ExitsWholeCliAtCaptureEnd) {
    const std::string csv =
        SmokeTestHelper::getProjectRoot() + "/build/cli_eof_1s.csv";
    writeCapture(csv, 1.0, 0.05);

    const long long before = logSize();
    const auto t0 = std::chrono::steady_clock::now();
    // --duration is deliberately NOT used here: the fail-fast refuses
    // --duration + --live-telemetry (the telemetry input bounds the run). The
    // capture's own EOF terminates the run.
    const int rc = SmokeTestHelper::runCLI(
        "--live-telemetry --silent < " + csv);
    const double wall =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
            .count();
    const std::string out = logDelta(before);

    EXPECT_EQ(exitCodeOf(rc), 0) << "clean exit at capture end, out:\n" << out;
    EXPECT_LT(wall, 4.5)
        << "capture is 1s: EOF must exit ~1s, not hang. wall=" << wall;
    EXPECT_TRUE(out.find("end of replay trace") != std::string::npos) << out;
    EXPECT_TRUE(out.find("duration reached") == std::string::npos)
        << "EOF ended the run, not the duration timer:\n" << out;
}

TEST(CaptureEofExitTest, ReplayEndAt_ReportsEndAtNotFullTraceDuration) {
    const std::string csv =
        SmokeTestHelper::getProjectRoot() + "/build/cli_eof_10s.csv";
    writeCapture(csv, 10.0, 0.1);

    const long long before = logSize();
    const int rc = SmokeTestHelper::runCLI(
        "--replay-telemetry " + csv + " --end-at 5 --silent");
    const std::string out = logDelta(before);

    EXPECT_EQ(exitCodeOf(rc), 0) << out;
    EXPECT_TRUE(out.find("--end-at 5") != std::string::npos)
        << "The stop reason must name the bound that ended the run:\n" << out;
    EXPECT_TRUE(out.find("duration reached") == std::string::npos)
        << "The full-trace duration message must not print for an --end-at "
           "run:\n" << out;
}
