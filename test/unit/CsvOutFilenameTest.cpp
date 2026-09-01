// CsvOutFilenameTest.cpp - Behavior tests for the auto-generated road-test
// CSV filename produced by --csv-out.
//
// TRIGGER NOTE:
//   Auto-generation fires only when --csv-out's value is the literal "true"
//   (--csv-out true or --csv-out=true). The BARE --csv-out flag leaves csvOut
//   EMPTY (CSV output disabled) — it does NOT yield "true". So the auto-gen
//   tests use the "true" value form to reach the timestamp path.
//
// DESIGN:
//   - We assert the filename PATTERN (prefix, digit groups, extension), not the
//     exact timestamp — wall-clock dependent, strict match would be fragile.
//   - We assert the embedded timestamp is plausibly "now" in UTC (the road log
//     travels across timezones, so the name is UTC). A refactor feeding
//     gmtime_r the wrong source would fail.
//   - Two calls a tick apart must yield DISTINCT names — the core requirement:
//     reruns never overwrite prior logs.

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

// Format the current UTC time the same way the production code generates the
// filename (YYYYMMDD_HHMMSS via gmtime_r), for comparison.
std::string expectedTimestampNowUtc() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time, &tm);
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return std::string(buf);
}

}  // namespace

// ============================================================================
// --csv-out with value "true" -> auto-generated UTC timestamped filename
// ============================================================================

TEST(CsvOutFilename, TrueValue_GeneratesTimestampedName) {
    auto args = parseArgv({"--csv-out", "true"});

    // Filename must follow the roadtest_<timestamp>.csv contract.
    const std::regex pattern(R"(roadtest_\d{8}_\d{6}\.csv)");
    EXPECT_TRUE(std::regex_match(args.csvOut, pattern))
        << "Expected timestamped filename, got: " << args.csvOut;
}

TEST(CsvOutFilename, TrueValue_HasRoadtestPrefix) {
    auto args = parseArgv({"--csv-out", "true"});
    EXPECT_EQ(args.csvOut.substr(0, 9), "roadtest_")
        << "Filename must keep the 'roadtest_' prefix";
}

TEST(CsvOutFilename, TrueValue_HasCsvExtension) {
    auto args = parseArgv({"--csv-out", "true"});
    EXPECT_EQ(args.csvOut.substr(args.csvOut.size() - 4), ".csv")
        << "Filename must keep the '.csv' extension";
}

TEST(CsvOutFilename, TrueValue_DateGroupMatchesTodayUtc) {
    // The first 8 digits (YYYYMMDD) must equal today's UTC date. This is the
    // intent the gmtime_r path must preserve: a correct broken-down UTC time.
    auto args = parseArgv({"--csv-out", "true"});
    ASSERT_GE(args.csvOut.size(), 17u);
    const std::string fileDate = args.csvOut.substr(9, 8);
    const std::string todayDate = expectedTimestampNowUtc().substr(0, 8);
    EXPECT_EQ(fileDate, todayDate)
        << "Filename date " << fileDate << " does not match today(UTC) " << todayDate;
}

TEST(CsvOutFilename, TrueValue_TimeGroupIsPlausiblyNowUtc) {
    // The HHMMSS group must be within a few minutes of now(UTC). Window avoids
    // a fragile wall-clock test; still catches a wrong/stale time.
    auto args = parseArgv({"--csv-out", "true"});
    ASSERT_GE(args.csvOut.size(), 22u);

    const std::string fileStamp = args.csvOut.substr(9, 15);  // YYYYMMDD_HHMMSS
    const std::string nowStamp = expectedTimestampNowUtc();

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
        << "Filename timestamp " << fileStamp << " is too far from now(UTC) " << nowStamp;
}

// ============================================================================
// Two calls a tick apart -> DISTINCT names (reruns never overwrite)
// ============================================================================

TEST(CsvOutFilename, TwoCalls_DistinctNames) {
    // Two invocations separated by >1 second of wall-clock time must produce
    // two different filenames. This is the whole point of the feature: reruns
    // never overwrite a prior road log.
    auto a = parseArgv({"--csv-out", "true"});
    // Wait long enough for the wall-clock second to roll over. The timestamp
    // has 1-second resolution, so we spin until the second advances.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    CommandLineArgs b;
    do {
        b = parseArgv({"--csv-out", "true"});
    } while (b.csvOut == a.csvOut && std::chrono::steady_clock::now() < deadline);

    EXPECT_NE(a.csvOut, b.csvOut)
        << "Two runs a second apart produced the SAME filename (would overwrite): "
        << a.csvOut;
}

// ============================================================================
// --csv-out WITH a value -> value used verbatim (no auto-generation)
// ============================================================================

TEST(CsvOutFilename, ExplicitValue_IsUsedVerbatim) {
    auto args = parseArgv({"--csv-out", "my_road.csv"});
    EXPECT_EQ(args.csvOut, "my_road.csv");
}

TEST(CsvOutFilename, ExplicitValue_DoesNotTriggerAutoGeneration) {
    auto args = parseArgv({"--csv-out", "custom_path.csv"});
    const std::regex autoPattern(R"(roadtest_\d{8}_\d{6}\.csv)");
    EXPECT_FALSE(std::regex_match(args.csvOut, autoPattern))
        << "Explicit value must not be overwritten by auto-generated name";
}

// ============================================================================
// --csv-out omitted, or bare flag -> empty (CSV output disabled)
// ============================================================================

TEST(CsvOutFilename, Omitted_DefaultsToEmpty) {
    auto args = parseArgv({});
    EXPECT_TRUE(args.csvOut.empty())
        << "Without --csv-out, the path must be empty (CSV output disabled)";
}

TEST(CsvOutFilename, BareFlag_LeavesEmpty) {
    // The bare --csv-out flag (no value) does NOT trigger auto-generation —
    // CLI11 leaves csvOut empty, which means CSV output is disabled. This pins
    // the current observable behavior; the "true" sentinel is the ONLY trigger.
    auto args = parseArgv({"--csv-out"});
    EXPECT_TRUE(args.csvOut.empty())
        << "Bare --csv-out must leave the path empty (CSV output disabled)";
}
