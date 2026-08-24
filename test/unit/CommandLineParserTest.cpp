#include "config/CLIconfig.h"
#include "gtest/gtest.h"

#include <filesystem>

TEST(CommandLineParserTest, ParsesOutputPathWithoutScript) {
    const char* argv[] = {"engine-sim-cli", "recording.wav"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(2, const_cast<char**>(argv), args));
    EXPECT_EQ(args.outputWav, "recording.wav");
}

TEST(CommandLineParserTest, ParsesOptionsAndTranslatesLoad) {
    const char* argv[] = {
        "engine-sim-cli",
        "--script", "v8_engine.mr",
        "--load", "50",
        "--silent",
        "--threaded",
        "--output", "output.wav"
    };
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(9, const_cast<char**>(argv), args));
    EXPECT_EQ(args.engineConfig, "v8_engine.mr");
    EXPECT_EQ(args.outputWav, "output.wav");
    EXPECT_DOUBLE_EQ(args.targetLoad, 0.5);
    EXPECT_TRUE(args.silent);
    EXPECT_TRUE(args.playAudio);
    EXPECT_FALSE(args.syncPull);
}

TEST(CommandLineParserTest, ParsesConnectDemoFlag) {
    const char* argv[] = {"engine-sim-cli", "--connect-demo"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(2, const_cast<char**>(argv), args));
    EXPECT_TRUE(args.connectDemo);
}

TEST(CommandLineParserTest, ConnectDemoSetsImplicitPlayAudio) {
    const char* argv[] = {"engine-sim-cli", "--connect-demo"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(2, const_cast<char**>(argv), args));
    EXPECT_TRUE(args.playAudio);
}

TEST(CommandLineParserTest, ConnectDemoSetsImplicitInteractive) {
    const char* argv[] = {"engine-sim-cli", "--connect-demo"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(2, const_cast<char**>(argv), args));
    EXPECT_TRUE(args.interactive);
}

TEST(CommandLineParserTest, ConnectDemoDefaultFalse) {
    const char* argv[] = {"engine-sim-cli", "--sine"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(2, const_cast<char**>(argv), args));
    EXPECT_FALSE(args.connectDemo);
}

// --auto / --manual gearbox flags

TEST(CommandLineParserTest, AutoFlagEnablesAutoGearbox) {
    const char* argv[] = {"engine-sim-cli", "--play", "--silent", "--auto"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(4, const_cast<char**>(argv), args));
    EXPECT_TRUE(args.gearbox.automatic);
    EXPECT_FALSE(args.gearbox.manual);
}

TEST(CommandLineParserTest, ManualFlagExplicit) {
    const char* argv[] = {"engine-sim-cli", "--play", "--silent", "--manual"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(4, const_cast<char**>(argv), args));
    EXPECT_FALSE(args.gearbox.automatic);
    EXPECT_TRUE(args.gearbox.manual);
}

TEST(CommandLineParserTest, DefaultGearboxIsManual) {
    const char* argv[] = {"engine-sim-cli", "--play", "--silent"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(3, const_cast<char**>(argv), args));
    EXPECT_FALSE(args.gearbox.automatic);
    EXPECT_FALSE(args.gearbox.manual);
}

TEST(CommandLineParserTest, AutoAndManualAreMutuallyExclusive) {
    const char* argv[] = {"engine-sim-cli", "--auto", "--manual"};
    CommandLineArgs args;

    EXPECT_FALSE(parseArguments(3, const_cast<char**>(argv), args));
}

TEST(CommandLineParserTest, AutoFlagWithConnectDemo) {
    const char* argv[] = {"engine-sim-cli", "--connect-demo", "--auto"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(3, const_cast<char**>(argv), args));
    EXPECT_TRUE(args.gearbox.automatic);
    EXPECT_TRUE(args.connectDemo);
}

// --live-telemetry: read live CSV from stdin (vehicle-sim --stdout-csv piped in).
// Live and recorded replay share the same stdin CSV contract, so they are
// distinct input sources and must not be combined with each other or with the
// keyboard-driven demo.

TEST(CommandLineParserTest, LiveTelemetryFlagParses) {
    const char* argv[] = {"engine-sim-cli", "--live-telemetry"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(2, const_cast<char**>(argv), args));
    EXPECT_TRUE(args.liveTelemetry);
}

TEST(CommandLineParserTest, LiveTelemetryDefaultFalse) {
    const char* argv[] = {"engine-sim-cli", "--sine"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(2, const_cast<char**>(argv), args));
    EXPECT_FALSE(args.liveTelemetry);
}

TEST(CommandLineParserTest, LiveTelemetryExcludesReplayTelemetry) {
    // Live (stdin) and recorded (file) telemetry are distinct input sources.
    const char* argv[] = {"engine-sim-cli", "--live-telemetry", "--replay-telemetry", "trace.csv"};
    CommandLineArgs args;

    EXPECT_FALSE(parseArguments(4, const_cast<char**>(argv), args));
}

TEST(CommandLineParserTest, LiveTelemetryExcludesConnectDemo) {
    const char* argv[] = {"engine-sim-cli", "--live-telemetry", "--connect-demo"};
    CommandLineArgs args;

    EXPECT_FALSE(parseArguments(3, const_cast<char**>(argv), args));
}

// --live-telemetry COMBINES with --script so the user can pick the engine to
// drive from CSV (e.g. the C63 V2b). Without this, --live-telemetry is locked to
// preset[0] (the alphabetical first preset), because resolveConfigPaths only
// scans the preset dir when args.engineConfig is empty. Allowing the combo lets
// args.engineConfig carry the named engine so that engine — not preset[0] — loads.
TEST(CommandLineParserTest, LiveTelemetryCombinesWithScriptToSelectEngine) {
    const char* argv[] = {
        "engine-sim-cli",
        "--live-telemetry",
        "--script", "C63_M156_V2b.mr",
        "--silent"
    };
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(5, const_cast<char**>(argv), args));
    EXPECT_TRUE(args.liveTelemetry);
    EXPECT_EQ(args.engineConfig, "C63_M156_V2b.mr")
        << "--live-telemetry must combine with --script so a named engine loads "
           "instead of the alphabetical preset[0]";
}

// The positional engine_config form is NOT an engine-selection seam here
// (output_wav is the first positional and consumes a bare argument), so it stays
// excluded from --live-telemetry. Regression guard against re-broadening.
TEST(CommandLineParserTest, LiveTelemetryStillExcludesPositionalEngineConfig) {
    const char* argv[] = {
        "engine-sim-cli",
        "--live-telemetry",
        "out.wav",
        "C63_M156_V2b.mr"
    };
    CommandLineArgs args;

    EXPECT_FALSE(parseArguments(4, const_cast<char**>(argv), args));
}

// ============================================================================
// --afterfire-wav resolution
// ============================================================================
// The argument may be a literal path OR a glob. Resolution must locate the
// DIRECTORY against the install root while leaving glob metacharacters in the
// filename untouched, so the bridge can expand them against a directory that
// actually exists. Resolving the whole string (the previous behaviour) made
// every glob miss, because no file is literally named "smooth_2*.wav" — the
// pattern silently degraded to the engine's default impulse response.

// A glob must survive resolution intact: the '*' stays in the leaf and the
// parent directory is resolved to somewhere that exists.
TEST(AfterfireWavArgumentTest, GlobLeafSurvivesDirectoryResolution) {
    const std::string resolved =
        resolveAfterfireWavArgument("es/sound-library/smooth/smooth_2*.wav");

    ASSERT_FALSE(resolved.empty());
    const std::filesystem::path p(resolved);
    EXPECT_EQ(p.filename().string(), "smooth_2*.wav")
        << "the glob pattern must be preserved verbatim for the bridge to expand";
    EXPECT_TRUE(std::filesystem::is_directory(p.parent_path()))
        << "parent must resolve to a real directory, got: " << p.parent_path();
}

// The literal-path case must keep working — same directory resolution, and the
// result must point at a file that exists.
TEST(AfterfireWavArgumentTest, LiteralPathResolvesToExistingFile) {
    const std::string resolved =
        resolveAfterfireWavArgument("es/sound-library/smooth/smooth_20.wav");

    ASSERT_FALSE(resolved.empty());
    EXPECT_TRUE(std::filesystem::exists(resolved))
        << "literal path should resolve to a real file, got: " << resolved;
}

// Empty means "use the engine default IR" and must stay empty — an empty string
// is the documented sentinel, so resolution must not invent a path for it.
TEST(AfterfireWavArgumentTest, EmptyArgumentStaysEmpty) {
    EXPECT_TRUE(resolveAfterfireWavArgument("").empty());
}

// A non-matching glob must still resolve (to a real directory + the pattern).
// Detecting "matched nothing" is the bridge's job at configure time; the CLI
// resolver must not swallow it here, or fail-fast could never fire.
TEST(AfterfireWavArgumentTest, NonMatchingGlobStillResolvesForLaterExpansion) {
    const std::string resolved =
        resolveAfterfireWavArgument("es/sound-library/smooth/smooth_P*.wav");

    ASSERT_FALSE(resolved.empty());
    const std::filesystem::path p(resolved);
    EXPECT_EQ(p.filename().string(), "smooth_P*.wav");
    EXPECT_FALSE(std::filesystem::exists(resolved))
        << "no file matches this pattern literally; the bridge reports the miss";
}

// ============================================================================
// --afterfire-pop-overlap / --afterfire-min-pop-interval-ms
// ============================================================================
// Pop PLAYBACK behaviour (the WAV overlay), not the physics: how a pop arriving
// while another is still sounding on the same exhaust channel is admitted. These
// bind straight to the AfterfireConfig handed to SimulatorFactory::configureAfterfire,
// so what the parser writes is what the chamber receives.

// The default must be SumOnTop (blend). Overlap handling is a safety net, not
// the fix — constantly overlapping pops are a per-vehicle physics tuning problem
// — so the default layers events rather than discarding them. Suppression is
// reachable only by asking for it.
TEST(AfterfirePopOverlapArgsTest, DefaultsToSumOnTop) {
    const char* argv[] = {"engine-sim-cli", "--duration", "1", "--enable-afterfire"};
    CommandLineArgs args;

    ASSERT_TRUE(parseArguments(4, const_cast<char**>(argv), args));
    EXPECT_EQ(args.afterfire.popOverlapMode, AfterfirePopOverlap::SumOnTop);
}

// The opt-out must actually be reachable from the command line: this is the only
// way to get suppression now that it is no longer the default.
TEST(AfterfirePopOverlapArgsTest, SuppressSelectsTheOptOutMode) {
    const char* argv[] = {"engine-sim-cli", "--duration", "1", "--enable-afterfire",
                          "--afterfire-pop-overlap", "suppress"};
    CommandLineArgs args;

    ASSERT_TRUE(parseArguments(6, const_cast<char**>(argv), args));
    EXPECT_EQ(args.afterfire.popOverlapMode, AfterfirePopOverlap::SuppressWhilePlaying);
}

// The word "sum" must be accepted explicitly, not merely be the default — a user
// pinning the default in a script should not hit a parse error.
TEST(AfterfirePopOverlapArgsTest, SumIsAcceptedExplicitly) {
    const char* argv[] = {"engine-sim-cli", "--duration", "1", "--enable-afterfire",
                          "--afterfire-pop-overlap", "sum"};
    CommandLineArgs args;

    ASSERT_TRUE(parseArguments(6, const_cast<char**>(argv), args));
    EXPECT_EQ(args.afterfire.popOverlapMode, AfterfirePopOverlap::SumOnTop);
}

// An unrecognised mode must FAIL rather than silently fall back to the default:
// a typo that quietly ran the other mode would be indistinguishable from a
// tuning result.
TEST(AfterfirePopOverlapArgsTest, UnknownModeIsRejected) {
    const char* argv[] = {"engine-sim-cli", "--duration", "1", "--enable-afterfire",
                          "--afterfire-pop-overlap", "layered"};
    CommandLineArgs args;

    EXPECT_FALSE(parseArguments(6, const_cast<char**>(argv), args));
}

// The inter-pop floor default must match the mixer's, since that is the value
// the chamber uses when the flag is absent.
TEST(AfterfirePopOverlapArgsTest, MinPopIntervalDefaultsToMixerDefault) {
    const char* argv[] = {"engine-sim-cli", "--duration", "1", "--enable-afterfire"};
    CommandLineArgs args;

    ASSERT_TRUE(parseArguments(4, const_cast<char**>(argv), args));
    EXPECT_DOUBLE_EQ(args.afterfire.minPopIntervalMs,
                     DEFAULT_AFTERFIRE_MIN_POP_INTERVAL_MS);
}

TEST(AfterfirePopOverlapArgsTest, MinPopIntervalOverrideIsParsed) {
    const char* argv[] = {"engine-sim-cli", "--duration", "1", "--enable-afterfire",
                          "--afterfire-min-pop-interval-ms", "12.5"};
    CommandLineArgs args;

    ASSERT_TRUE(parseArguments(6, const_cast<char**>(argv), args));
    EXPECT_DOUBLE_EQ(args.afterfire.minPopIntervalMs, 12.5);
}

// 0 is a MEANINGFUL value ("no floor"), not a missing-argument sentinel, so it
// must pass the range check and survive into the config rather than being
// replaced by the default.
TEST(AfterfirePopOverlapArgsTest, MinPopIntervalZeroDisablesTheFloor) {
    const char* argv[] = {"engine-sim-cli", "--duration", "1", "--enable-afterfire",
                          "--afterfire-min-pop-interval-ms", "0"};
    CommandLineArgs args;

    ASSERT_TRUE(parseArguments(6, const_cast<char**>(argv), args));
    EXPECT_DOUBLE_EQ(args.afterfire.minPopIntervalMs, 0.0);
}

// A negative interval is meaningless; the range check must reject it.
TEST(AfterfirePopOverlapArgsTest, NegativeMinPopIntervalIsRejected) {
    const char* argv[] = {"engine-sim-cli", "--duration", "1", "--enable-afterfire",
                          "--afterfire-min-pop-interval-ms", "-5"};
    CommandLineArgs args;

    EXPECT_FALSE(parseArguments(6, const_cast<char**>(argv), args));
}

// ============================================================================
// --afterfire-pop-decay-divisor
// ============================================================================
// The added exponential decay applied to a pop on playback (tau = length/n).
// It binds straight to the AfterfireConfig handed to
// SimulatorFactory::configureAfterfire, so what the parser writes is what the
// chamber receives.
//
// SCOPE, because it is easy to misread this flag: it only ever shapes an UNSHAPED
// pop. A custom WAV that already decays by itself (any real backfire recording) is
// never decayed a second time whatever this is set to — applying the envelope on
// top of an already-shaped sample buried every crack after the first. That
// conditionality lives in the mixer and is pinned by the engine-sim
// AfterfirePopDecay tests; these tests pin only that the VALUE arrives intact.

// The default must match the mixer's, since that is the value the chamber uses
// when the flag is absent. A stale default here would silently reshape every pop.
TEST(AfterfirePopDecayArgsTest, DivisorDefaultsToMixerDefault) {
    const char* argv[] = {"engine-sim-cli", "--duration", "1", "--enable-afterfire"};
    CommandLineArgs args;

    ASSERT_TRUE(parseArguments(4, const_cast<char**>(argv), args));
    EXPECT_DOUBLE_EQ(args.afterfire.popDecayDivisor,
                     DEFAULT_AFTERFIRE_POP_DECAY_DIVISOR);
}

TEST(AfterfirePopDecayArgsTest, DivisorOverrideIsParsed) {
    const char* argv[] = {"engine-sim-cli", "--duration", "1", "--enable-afterfire",
                          "--afterfire-pop-decay-divisor", "1.5"};
    CommandLineArgs args;

    ASSERT_TRUE(parseArguments(6, const_cast<char**>(argv), args));
    EXPECT_DOUBLE_EQ(args.afterfire.popDecayDivisor, 1.5);
}

// 0 is a MEANINGFUL value ("no added decay"), not a missing-argument sentinel, so
// it must pass the range check and survive into the config rather than being
// replaced by the default. This is the value a user reaches for when a pop is
// being over-shaped, so it must actually be reachable.
TEST(AfterfirePopDecayArgsTest, ZeroDivisorDisablesTheAddedDecay) {
    const char* argv[] = {"engine-sim-cli", "--duration", "1", "--enable-afterfire",
                          "--afterfire-pop-decay-divisor", "0"};
    CommandLineArgs args;

    ASSERT_TRUE(parseArguments(6, const_cast<char**>(argv), args));
    EXPECT_DOUBLE_EQ(args.afterfire.popDecayDivisor, 0.0);
}

// A negative divisor is meaningless (it would AMPLIFY the pop exponentially rather
// than decay it, growing without bound to the end of the sample), so the range
// check must reject it rather than let it through to the audio path.
TEST(AfterfirePopDecayArgsTest, NegativeDivisorIsRejected) {
    const char* argv[] = {"engine-sim-cli", "--duration", "1", "--enable-afterfire",
                          "--afterfire-pop-decay-divisor", "-2"};
    CommandLineArgs args;

    EXPECT_FALSE(parseArguments(6, const_cast<char**>(argv), args));
}
