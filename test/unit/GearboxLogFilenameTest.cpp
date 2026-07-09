// GearboxLogFilenameTest.cpp - Behavior tests for the auto-generated gearbox
// log filename produced by --gearbox-log.
//
// TARGETS REFACTOR COVERAGE FOR cpp:S1912:
//   The filename is built in processArgs() (CLIconfig.cpp) from the current
//   wall-clock time using std::localtime. The Tier-3 refactor swaps that for
//   the thread-safe std::localtime_r. These tests pin the OBSERVABLE behavior
//   (the filename format and that it carries a real timestamp) so the
//   localtime -> localtime_r change can be verified behavior-identical.
//
// TRIGGER NOTE (important — avoids a wrong test):
//   Auto-generation fires only when the option's value is the literal "true"
//   (--gearbox-log=true or --gearbox-log true). The BARE --gearbox-log flag
//   leaves logPath EMPTY (logging disabled) — it does NOT yield "true". So the
//   auto-generation tests use the "true" value form to reach the timestamp path.
//
// DESIGN:
//   - We assert the filename PATTERN (prefix, suffix, digit groups, separators)
//     rather than the exact timestamp, because the timestamp is wall-clock
//     dependent and a strict match would be a fragile test that tests itself.
//   - We assert the timestamp embedded in the filename is plausibly "now"
//     (same Y/M/D, within a small window) — that is the intent a refactor must
//     preserve. localtime_r producing the wrong struct would fail this.
//   - We assert that giving --gearbox-log an explicit (non-"true") value does
//     NOT trigger auto-generation (the value is used verbatim).

#include <gtest/gtest.h>
#include "config/CLIconfig.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Build an argv vector the way CLI11 expects (argv[0] is the program name),
// then parse it. Returns the parsed CommandLineArgs.
CommandLineArgs parseArgv(std::vector<std::string> args) {
    CommandLineArgs parsed;
    args.insert(args.begin(), "engine-sim-cli");
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& s : args) argv.push_back(s.data());
    parseArguments(static_cast<int>(argv.size()), argv.data(), parsed);
    return parsed;
}

// Format the current local time the same way the production code intends to
// (YYYYMMDD_HHMMSS), for comparison against the generated filename.
std::string expectedTimestampNow() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time, &tm);
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return std::string(buf);
}

}  // namespace

// ============================================================================
// --gearbox-log with value "true" -> auto-generated timestamped filename
//   (This is the only invocation that reaches the localtime code path.)
// ============================================================================

TEST(GearboxLogFilename, TrueValue_GeneratesTimestampedName) {
    // --auto is required for the gearbox path to be meaningful, but filename
    // generation happens in processArgs() regardless of the gearbox mode.
    auto args = parseArgv({"--auto", "--gearbox-log", "true"});

    // Filename must follow the gearbox_<timestamp>.csv contract.
    const std::regex pattern(R"(gearbox_\d{8}_\d{6}\.csv)");
    EXPECT_TRUE(std::regex_match(args.gearbox.logPath, pattern))
        << "Expected timestamped filename, got: " << args.gearbox.logPath;
}

TEST(GearboxLogFilename, TrueValue_HasGearboxPrefix) {
    auto args = parseArgv({"--auto", "--gearbox-log", "true"});
    EXPECT_EQ(args.gearbox.logPath.substr(0, 8), "gearbox_")
        << "Filename must keep the 'gearbox_' prefix";
}

TEST(GearboxLogFilename, TrueValue_HasCsvExtension) {
    auto args = parseArgv({"--auto", "--gearbox-log", "true"});
    EXPECT_EQ(args.gearbox.logPath.substr(args.gearbox.logPath.size() - 4), ".csv")
        << "Filename must keep the '.csv' extension";
}

TEST(GearboxLogFilename, TrueValue_DateGroupMatchesToday) {
    // The first 8 digits (YYYYMMDD) must equal today's local date. This is the
    // intent the localtime -> localtime_r refactor must preserve: a correct
    // broken-down local time. A refactor that feeds localtime_r the wrong
    // source (or ignores its output) would drift the date.
    auto args = parseArgv({"--auto", "--gearbox-log", "true"});
    ASSERT_GE(args.gearbox.logPath.size(), 16u);
    const std::string fileDate = args.gearbox.logPath.substr(8, 8);
    const std::string todayDate = expectedTimestampNow().substr(0, 8);
    EXPECT_EQ(fileDate, todayDate)
        << "Filename date " << fileDate << " does not match today " << todayDate;
}

TEST(GearboxLogFilename, TrueValue_TimeGroupIsPlausiblyNow) {
    // The HHMMSS group must be within a few minutes of now. We use a window
    // rather than an exact match to avoid a fragile wall-clock test. This
    // still catches a refactor that produces a wrong/stale time.
    auto args = parseArgv({"--auto", "--gearbox-log", "true"});
    ASSERT_GE(args.gearbox.logPath.size(), 21u);

    const std::string fileStamp = args.gearbox.logPath.substr(8, 15);  // YYYYMMDD_HHMMSS
    const std::string nowStamp = expectedTimestampNow();

    std::tm fileTm{};
    std::tm nowTm{};
    std::istringstream fs(fileStamp);
    std::istringstream ns(nowStamp);
    fs >> std::get_time(&fileTm, "%Y%m%d_%H%M%S");
    ns >> std::get_time(&nowTm, "%Y%m%d_%H%M%S");
    ASSERT_FALSE(fs.fail());
    ASSERT_FALSE(ns.fail());

    const auto fileSecs = std::mktime(&fileTm);
    const auto nowSecs = std::mktime(&nowTm);
    ASSERT_NE(fileSecs, static_cast<std::time_t>(-1));
    ASSERT_NE(nowSecs, static_cast<std::time_t>(-1));

    // Allow up to 5 minutes of slack for test scheduling / wall-clock drift.
    const long slack = 300;
    EXPECT_LE(std::labs(static_cast<long>(fileSecs - nowSecs)), slack)
        << "Filename timestamp " << fileStamp << " is too far from now " << nowStamp;
}

// ============================================================================
// --gearbox-log WITH a value -> value used verbatim (no auto-generation)
// ============================================================================

TEST(GearboxLogFilename, ExplicitValue_IsUsedVerbatim) {
    auto args = parseArgv({"--auto", "--gearbox-log", "my_log.csv"});
    EXPECT_EQ(args.gearbox.logPath, "my_log.csv");
}

TEST(GearboxLogFilename, ExplicitValue_DoesNotTriggerAutoGeneration) {
    // A user-supplied path that isn't the literal "true" must be preserved
    // exactly — auto-generation must only happen for the "true" sentinel.
    auto args = parseArgv({"--auto", "--gearbox-log", "custom_path.csv"});
    const std::regex autoPattern(R"(gearbox_\d{8}_\d{6}\.csv)");
    EXPECT_FALSE(std::regex_match(args.gearbox.logPath, autoPattern))
        << "Explicit value must not be overwritten by auto-generated name";
}

// ============================================================================
// --gearbox-log omitted, or bare flag -> empty path (logging disabled)
// ============================================================================

TEST(GearboxLogFilename, Omitted_DefaultsToEmpty) {
    auto args = parseArgv({"--auto"});
    EXPECT_TRUE(args.gearbox.logPath.empty())
        << "Without --gearbox-log, the path must be empty (logging disabled)";
}

TEST(GearboxLogFilename, BareFlag_LeavesEmpty) {
    // The bare --gearbox-log flag (no value) does NOT trigger auto-generation —
    // CLI11 leaves logPath empty, which means logging is disabled. This pins
    // the current observable behavior; it documents that the "true" sentinel is
    // the ONLY trigger (not the bare flag).
    auto args = parseArgv({"--auto", "--gearbox-log"});
    EXPECT_TRUE(args.gearbox.logPath.empty())
        << "Bare --gearbox-log must leave the path empty (logging disabled)";
}
