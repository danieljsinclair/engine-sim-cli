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
    EXPECT_TRUE(args.autoGearbox);
    EXPECT_FALSE(args.manualGearbox);
}

TEST(CommandLineParserTest, ManualFlagExplicit) {
    const char* argv[] = {"engine-sim-cli", "--play", "--silent", "--manual"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(4, const_cast<char**>(argv), args));
    EXPECT_FALSE(args.autoGearbox);
    EXPECT_TRUE(args.manualGearbox);
}

TEST(CommandLineParserTest, DefaultGearboxIsManual) {
    const char* argv[] = {"engine-sim-cli", "--play", "--silent"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(3, const_cast<char**>(argv), args));
    EXPECT_FALSE(args.autoGearbox);
    EXPECT_FALSE(args.manualGearbox);
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
    EXPECT_TRUE(args.autoGearbox);
    EXPECT_TRUE(args.connectDemo);
}
