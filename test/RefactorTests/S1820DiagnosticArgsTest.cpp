// S1820DiagnosticArgsTest.cpp
//
// BDD-style tests pinning the contract of the DiagnosticArgs extraction
// (SonarQube S1820 partial).
//
// CONTEXT:
//   CommandLineArgs has 21 fields. We extract the 3 diagnostic-output knobs
//   into a dedicated sub-struct:
//
//     presentation::DiagnosticOutputFilter diagnostics  // --diagnostic-frames / --diagnostic-freq
//     std::string                            csvOut       // --csv-out
//     float                                  spanTame     // --span-tame
//
//   The sub-struct lives as a nested member type (`struct DiagnosticArgs`)
//   on CommandLineArgs, mirroring the TwinArgs / StartArgs precedent.
//   Accessed via args.diagnostics.X after the extraction.
//
// CONTRACT under test:
//   1. Default values match the original struct's defaults.
//   2. Each flag --X sets the corresponding nested field.
//   3. CSV auto-generation (--csv-out true) still produces a roadtest_<utc>.csv
//      timestamped filename — the diagnostic output path is preserved.
//   4. spanTame range [0.0, 1.0] is still enforced.

#include <gtest/gtest.h>
#include "config/CLIconfig.h"

#include <regex>
#include <string>
#include <vector>

namespace {

CommandLineArgs parseArgv(std::vector<std::string> flags) {
    CommandLineArgs parsed;
    flags.insert(flags.begin(), "engine-sim-cli");
    std::vector<char*> argv;
    argv.reserve(flags.size());
    for (auto& s : flags) argv.push_back(s.data());
    parseArguments(static_cast<int>(argv.size()), argv.data(), parsed);
    return parsed;
}

}  // namespace

// ============================================================================
// Default values
// ============================================================================

TEST(DiagnosticArgsDefaults, NoFlags_CsvOutEmpty) {
    auto args = parseArgv({"--silent"});
    EXPECT_TRUE(args.diagnostics.csvOut.empty())
        << "Without --csv-out, the path is empty (CSV output disabled)";
}

TEST(DiagnosticArgsDefaults, NoFlags_SpanTameZero) {
    // 0.0 = OFF = bit-identical legacy audio.
    auto args = parseArgv({"--silent"});
    EXPECT_FLOAT_EQ(args.diagnostics.spanTame, 0.0f)
        << "Default spanTame is 0.0 (feature OFF, bit-identical audio)";
}

TEST(DiagnosticArgsDefaults, NoFlags_DiagnosticsFilterZero) {
    // DiagnosticOutputFilter is a bitmask; the default is all-off.
    auto args = parseArgv({"--silent"});
    EXPECT_FALSE(args.diagnostics.filter.frames)
        << "Default diagnostic-frames is off";
    EXPECT_FALSE(args.diagnostics.filter.freq)
        << "Default diagnostic-freq is off";
}

// ============================================================================
// --diagnostic-frames / --diagnostic-freq round-trip
// ============================================================================

TEST(DiagnosticArgsFlag, DiagnosticFrames_SetsFilterFrames) {
    auto args = parseArgv({"--silent", "--diagnostic-frames"});
    EXPECT_TRUE(args.diagnostics.filter.frames)
        << "--diagnostic-frames must set filter.frames on the nested struct";
}

TEST(DiagnosticArgsFlag, DiagnosticFreq_SetsFilterFreq) {
    auto args = parseArgv({"--silent", "--diagnostic-freq"});
    EXPECT_TRUE(args.diagnostics.filter.freq)
        << "--diagnostic-freq must set filter.freq on the nested struct";
}

// ============================================================================
// --csv-out round-trip
// ============================================================================

TEST(DiagnosticArgsFlag, CsvOut_ExplicitValue_Preserved) {
    auto args = parseArgv({"--csv-out", "my_road.csv"});
    EXPECT_EQ(args.diagnostics.csvOut, "my_road.csv")
        << "Explicit --csv-out value must be preserved verbatim";
}

TEST(DiagnosticArgsFlag, CsvOut_TrueValue_GeneratesTimestampedName) {
    auto args = parseArgv({"--csv-out", "true"});
    // The exact timestamp is wall-clock dependent; we only assert the
    // shape: roadtest_<digits>.csv.
    const std::regex pattern(R"(roadtest_\d{8}_\d{6}\.csv)");
    EXPECT_TRUE(std::regex_match(args.diagnostics.csvOut, pattern))
        << "Expected timestamped filename, got: " << args.diagnostics.csvOut;
}

TEST(DiagnosticArgsFlag, CsvOut_BareFlag_LeavesEmpty) {
    // Bare --csv-out (no value) does NOT trigger auto-generation — the
    // output path stays empty. This is the existing CLI11 behaviour.
    auto args = parseArgv({"--csv-out"});
    EXPECT_TRUE(args.diagnostics.csvOut.empty());
}

// ============================================================================
// --span-tame round-trip
// ============================================================================

TEST(DiagnosticArgsFlag, SpanTame_ParsesValue) {
    auto args = parseArgv({"--span-tame", "0.75"});
    EXPECT_FLOAT_EQ(args.diagnostics.spanTame, 0.75f);
}

TEST(DiagnosticArgsFlag, SpanTame_AcceptsBothIntervalEnds) {
    auto zero = parseArgv({"--span-tame", "0.0"});
    EXPECT_FLOAT_EQ(zero.diagnostics.spanTame, 0.0f);

    auto one = parseArgv({"--span-tame", "1.0"});
    EXPECT_FLOAT_EQ(one.diagnostics.spanTame, 1.0f);
}

TEST(DiagnosticArgsFlag, SpanTame_RejectsAboveRange) {
    const char* argv[] = {"engine-sim-cli", "--span-tame", "1.5"};
    CommandLineArgs probe;
    EXPECT_FALSE(parseArguments(3, const_cast<char**>(argv), probe))
        << "--span-tame 1.5 must fail (range rejection)";
}

TEST(DiagnosticArgsFlag, SpanTame_RejectsBelowRange) {
    const char* argv[] = {"engine-sim-cli", "--span-tame", "-0.1"};
    CommandLineArgs probe;
    EXPECT_FALSE(parseArguments(3, const_cast<char**>(argv), probe))
        << "--span-tame -0.1 must fail (range rejection)";
}

// ============================================================================
// API pin: the struct member MUST be named `diagnostics` (caller contract).
// ============================================================================

TEST(DiagnosticArgsContract, FieldIsNamedDiagnostics) {
    auto args = parseArgv({"--silent"});
    // If the extraction renamed the field, these expressions would fail
    // to compile, which is exactly what we want.
    static_cast<void>(args.diagnostics.filter.frames);
    static_cast<void>(args.diagnostics.filter.freq);
    static_cast<void>(args.diagnostics.csvOut);
    static_cast<void>(args.diagnostics.spanTame);
    SUCCEED() << "All 3 fields exist on args.diagnostics; the field is named diagnostics";
}
