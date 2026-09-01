#include "config/CLIconfig.h"
#include "gtest/gtest.h"

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
    const char* argv[] = {"engine-sim-cli", "--silent", "--auto"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(3, const_cast<char**>(argv), args));
    EXPECT_TRUE(args.gearbox.automatic);
    EXPECT_FALSE(args.gearbox.manual);
}

TEST(CommandLineParserTest, ManualFlagExplicit) {
    const char* argv[] = {"engine-sim-cli", "--silent", "--manual"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(3, const_cast<char**>(argv), args));
    EXPECT_FALSE(args.gearbox.automatic);
    EXPECT_TRUE(args.gearbox.manual);
}

TEST(CommandLineParserTest, DefaultGearboxIsManual) {
    const char* argv[] = {"engine-sim-cli", "--silent"};
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
    EXPECT_TRUE(args.twin.liveTelemetry);
}

TEST(CommandLineParserTest, LiveTelemetryDefaultFalse) {
    const char* argv[] = {"engine-sim-cli", "--sine"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(2, const_cast<char**>(argv), args));
    EXPECT_FALSE(args.twin.liveTelemetry);
}

TEST(CommandLineParserTest, LiveTelemetryExcludesReplayTelemetry) {
    // Live (stdin) and recorded (file) telemetry are distinct input sources.
    const char* argv[] = {"engine-sim-cli", "--live-telemetry", "--replay-telemetry", "trace.csv"};
    CommandLineArgs args;

    EXPECT_FALSE(parseArguments(3, const_cast<char**>(argv), args));
}

TEST(CommandLineParserTest, LiveTelemetryExcludesConnectDemo) {
    const char* argv[] = {"engine-sim-cli", "--live-telemetry", "--connect-demo"};
    CommandLineArgs args;

    EXPECT_FALSE(parseArguments(3, const_cast<char**>(argv), args));
}

// --live-telemetry COMBINES with --script so the user can pick the engine to
// drive from CSV (e.g. the C63 V3). Without this, --live-telemetry is locked to
// preset[0] (the alphabetical first preset), because resolveConfigPaths only
// scans the preset dir when args.engineConfig is empty. Allowing the combo lets
// args.engineConfig carry the named engine so that engine — not preset[0] — loads.
TEST(CommandLineParserTest, LiveTelemetryCombinesWithScriptToSelectEngine) {
    const char* argv[] = {
        "engine-sim-cli",
        "--live-telemetry",
        "--script", "C63_M156_V3.mr",
        "--silent"
    };
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(5, const_cast<char**>(argv), args));
    EXPECT_TRUE(args.twin.liveTelemetry);
    EXPECT_EQ(args.engineConfig, "C63_M156_V3.mr")
        << "--live-telemetry must combine with --script so a named engine loads "
           "instead of the alphabetical preset[0]";
}

// The positional engine_config form is NOT an engine-selection seam here
// (output_wav is the first positional and consumes a bare argument), so it stays
// excluded from --live-telemetry. Regression guard against re-broadening.
// argc=4: all four argv elements are passed, so the positional engine config
// ("C63_M156_V3.mr") is present and must be rejected by the live-telemetry
// exclusion. (argc=3 would drop the engine config and test nothing.)
TEST(CommandLineParserTest, LiveTelemetryStillExcludesPositionalEngineConfig) {
    const char* argv[] = {
        "engine-sim-cli",
        "--live-telemetry",
        "out.wav",
        "C63_M156_V3.mr"
    };
    CommandLineArgs args;

    EXPECT_FALSE(parseArguments(4, const_cast<char**>(argv), args));
}

// --pin-tau-ms: PIN-coupling compliance time constant. Default 0 = the rigid
// pin (bit-identical legacy behavior); the tuned road value is ~150.
TEST(CommandLineParserTest, PinTauMsDefaultsToZero_RigidPin) {
    const char* argv[] = {"engine-sim-cli", "--script", "v8.mr"};
    CommandLineArgs args;
    EXPECT_TRUE(parseArguments(3, const_cast<char**>(argv), args));
    EXPECT_DOUBLE_EQ(args.twin.pinTauMs, 0.0);
}

TEST(CommandLineParserTest, PinTauMsParsesExplicitValue) {
    const char* argv[] = {"engine-sim-cli", "--script", "v8.mr", "--pin-tau-ms", "150"};
    CommandLineArgs args;
    EXPECT_TRUE(parseArguments(5, const_cast<char**>(argv), args));
    EXPECT_DOUBLE_EQ(args.twin.pinTauMs, 150.0);
}

TEST(CommandLineParserTest, PinTauMsParsesExplicitZero) {
    const char* argv[] = {"engine-sim-cli", "--script", "v8.mr", "--pin-tau-ms", "0"};
    CommandLineArgs args;
    EXPECT_TRUE(parseArguments(5, const_cast<char**>(argv), args));
    EXPECT_DOUBLE_EQ(args.twin.pinTauMs, 0.0);
}

// --span-tame <x>: output-stage taming amount, x in [0.0, 1.0], default 0.0
// = OFF = bit-identical audio. The runtime knob the owner dialled span-taming
// with (the script-side audio_volume lever is exhausted: the leveler
// re-normalizes it, so taming must live at the synthesizer output stage).

TEST(CommandLineParserTest, SpanTameParsesValidValue) {
    const char* argv[] = {"engine-sim-cli", "--span-tame", "0.75"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(3, const_cast<char**>(argv), args));
    EXPECT_FLOAT_EQ(args.spanTame, 0.75f);
}

TEST(CommandLineParserTest, SpanTameDefaultsToOff) {
    const char* argv[] = {"engine-sim-cli", "--silent"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(2, const_cast<char**>(argv), args));
    EXPECT_FLOAT_EQ(args.spanTame, 0.0f);
}

// Both interval ends are valid taming amounts (0 = explicit off, 1 = full
// taming) — only values OUTSIDE [0, 1] are rejected.
TEST(CommandLineParserTest, SpanTameAcceptsBothIntervalEnds) {
    const char* argvZero[] = {"engine-sim-cli", "--span-tame", "0.0"};
    CommandLineArgs argsZero;
    EXPECT_TRUE(parseArguments(3, const_cast<char**>(argvZero), argsZero));
    EXPECT_FLOAT_EQ(argsZero.spanTame, 0.0f);

    const char* argvOne[] = {"engine-sim-cli", "--span-tame", "1.0"};
    CommandLineArgs argsOne;
    EXPECT_TRUE(parseArguments(3, const_cast<char**>(argvOne), argsOne));
    EXPECT_FLOAT_EQ(argsOne.spanTame, 1.0f);
}

// Out-of-range values are rejected at parse time (a typo'd 1.5 or negative
// must fail the run, not silently clamp) — while a valid value on the same
// schema parses, so the rejection is about the range, not the flag shape.
TEST(CommandLineParserTest, SpanTameRejectsAboveRange) {
    const char* argv[] = {"engine-sim-cli", "--span-tame", "1.5"};
    CommandLineArgs args;
    EXPECT_FALSE(parseArguments(3, const_cast<char**>(argv), args));
}

TEST(CommandLineParserTest, SpanTameRejectsBelowRange) {
    const char* argv[] = {"engine-sim-cli", "--span-tame", "-0.1"};
    CommandLineArgs args;
    EXPECT_FALSE(parseArguments(3, const_cast<char**>(argv), args));
}
