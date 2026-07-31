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
