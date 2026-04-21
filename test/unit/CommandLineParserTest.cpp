#include "config/CLIconfig.h"
#include "gtest/gtest.h"

TEST(CommandLineParserTest, ParsesDefaultEngineAndOutputPath) {
    const char* argv[] = {"engine-sim-cli", "--default-engine", "recording.wav"};
    CommandLineArgs args;

    EXPECT_TRUE(parseArguments(3, const_cast<char**>(argv), args));
    EXPECT_TRUE(args.useDefaultEngine);
    EXPECT_EQ(args.engineConfig, "(default engine)");
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
