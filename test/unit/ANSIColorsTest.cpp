#include <gtest/gtest.h>
#include "config/ANSIColors.h"

using namespace ANSIColors;

// ============================================================================
// ANSI Color Constants
// ============================================================================

TEST(ANSIColorsConstantsTest, Green_ContainsEscapeSequence) {
    EXPECT_FALSE(GREEN.empty());
    EXPECT_EQ(GREEN[0], '\x1b');
    EXPECT_TRUE(GREEN.find("[32m") != std::string::npos);
}

TEST(ANSIColorsConstantsTest, Yellow_ContainsEscapeSequence) {
    EXPECT_FALSE(YELLOW.empty());
    EXPECT_EQ(YELLOW[0], '\x1b');
    EXPECT_TRUE(YELLOW.find("[33m") != std::string::npos);
}

TEST(ANSIColorsConstantsTest, Red_ContainsEscapeSequence) {
    EXPECT_FALSE(RED.empty());
    EXPECT_EQ(RED[0], '\x1b');
    EXPECT_TRUE(RED.find("[31m") != std::string::npos);
}

TEST(ANSIColorsConstantsTest, Cyan_ContainsEscapeSequence) {
    EXPECT_FALSE(CYAN.empty());
    EXPECT_EQ(CYAN[0], '\x1b');
    EXPECT_TRUE(CYAN.find("[36m") != std::string::npos);
}

TEST(ANSIColorsConstantsTest, Reset_ContainsEscapeSequence) {
    EXPECT_FALSE(RESET.empty());
    EXPECT_EQ(RESET[0], '\x1b');
    EXPECT_TRUE(RESET.find("[0m") != std::string::npos);
}

TEST(ANSIColorsConstantsTest, Aliases_MatchBaseColors) {
    EXPECT_EQ(OK, GREEN);
    EXPECT_EQ(INFO, CYAN);
    EXPECT_EQ(WARNING, YELLOW);
    EXPECT_EQ(ERROR, RED);
}

TEST(ANSIColorsConstantsTest, AllColorsAreDistinct) {
    EXPECT_NE(GREEN, YELLOW);
    EXPECT_NE(GREEN, RED);
    EXPECT_NE(GREEN, CYAN);
    EXPECT_NE(GREEN, RESET);
    EXPECT_NE(YELLOW, RED);
    EXPECT_NE(YELLOW, CYAN);
    EXPECT_NE(YELLOW, RESET);
    EXPECT_NE(RED, CYAN);
    EXPECT_NE(RED, RESET);
    EXPECT_NE(CYAN, RESET);
}

// ============================================================================
// infoMessage / OKMessage / warningMessage / errorMessage
// ============================================================================

TEST(ANSIColorsMessageTest, infoMessage_WrapsMessageWithInfoAndReset) {
    const std::string msg = "test info";
    std::string result = infoMessage(msg);

    EXPECT_TRUE(result.find(INFO) != std::string::npos) << "Should contain INFO color";
    EXPECT_TRUE(result.find(msg) != std::string::npos) << "Should contain message";
    EXPECT_TRUE(result.find(RESET) != std::string::npos) << "Should contain RESET";
    EXPECT_TRUE(result.rfind(RESET) > result.find(msg)) << "RESET should be after message";
}

TEST(ANSIColorsMessageTest, infoMessage_EmptyString_StillWraps) {
    std::string result = infoMessage("");
    EXPECT_TRUE(result.find(INFO) != std::string::npos);
    EXPECT_TRUE(result.find(RESET) != std::string::npos);
}

TEST(ANSIColorsMessageTest, OKMessage_WrapsMessageWithGreenAndReset) {
    const std::string msg = "test ok";
    std::string result = OKMessage(msg);

    EXPECT_TRUE(result.find(GREEN) != std::string::npos) << "Should contain GREEN color";
    EXPECT_TRUE(result.find(msg) != std::string::npos) << "Should contain message";
    EXPECT_TRUE(result.find(RESET) != std::string::npos) << "Should contain RESET";
    EXPECT_TRUE(result.rfind(RESET) > result.find(msg)) << "RESET should be after message";
}

TEST(ANSIColorsMessageTest, OKMessage_EmptyString_StillWraps) {
    std::string result = OKMessage("");
    EXPECT_TRUE(result.find(GREEN) != std::string::npos);
    EXPECT_TRUE(result.find(RESET) != std::string::npos);
}

TEST(ANSIColorsMessageTest, warningMessage_WrapsMessageWithYellowAndReset) {
    const std::string msg = "test warning";
    std::string result = warningMessage(msg);

    EXPECT_TRUE(result.find(YELLOW) != std::string::npos) << "Should contain YELLOW color";
    EXPECT_TRUE(result.find(msg) != std::string::npos) << "Should contain message";
    EXPECT_TRUE(result.find(RESET) != std::string::npos) << "Should contain RESET";
    EXPECT_TRUE(result.rfind(RESET) > result.find(msg)) << "RESET should be after message";
}

TEST(ANSIColorsMessageTest, warningMessage_EmptyString_StillWraps) {
    std::string result = warningMessage("");
    EXPECT_TRUE(result.find(YELLOW) != std::string::npos);
    EXPECT_TRUE(result.find(RESET) != std::string::npos);
}

TEST(ANSIColorsMessageTest, errorMessage_WrapsMessageWithRedAndReset) {
    const std::string msg = "test error";
    std::string result = errorMessage(msg);

    EXPECT_TRUE(result.find(RED) != std::string::npos) << "Should contain RED color";
    EXPECT_TRUE(result.find(msg) != std::string::npos) << "Should contain message";
    EXPECT_TRUE(result.find(RESET) != std::string::npos) << "Should contain RESET";
    EXPECT_TRUE(result.rfind(RESET) > result.find(msg)) << "RESET should be after message";
}

TEST(ANSIColorsMessageTest, errorMessage_EmptyString_StillWraps) {
    std::string result = errorMessage("");
    EXPECT_TRUE(result.find(RED) != std::string::npos);
    EXPECT_TRUE(result.find(RESET) != std::string::npos);
}

// ============================================================================
// getDispositionColour - boundary value tests
// ============================================================================

// The function signature: getDispositionColour(bool isGreen = false, bool isYellow = false, bool isRed = true)
// Logic: if isGreen -> GREEN, else if isYellow -> YELLOW, else if isRed -> RED, else -> RESET

TEST(ANSIColorsDispositionTest, AllFalse_ReturnsReset) {
    std::string result = getDispositionColour(false, false, false);
    EXPECT_EQ(result, RESET);
}

TEST(ANSIColorsDispositionTest, OnlyGreenTrue_ReturnsGreen) {
    std::string result = getDispositionColour(true, false, false);
    EXPECT_EQ(result, GREEN);
}

TEST(ANSIColorsDispositionTest, OnlyYellowTrue_ReturnsYellow) {
    std::string result = getDispositionColour(false, true, false);
    EXPECT_EQ(result, YELLOW);
}

TEST(ANSIColorsDispositionTest, OnlyRedTrue_ReturnsRed) {
    std::string result = getDispositionColour(false, false, true);
    EXPECT_EQ(result, RED);
}

TEST(ANSIColorsDispositionTest, GreenAndYellowTrue_GreenWins) {
    std::string result = getDispositionColour(true, true, false);
    EXPECT_EQ(result, GREEN);
}

TEST(ANSIColorsDispositionTest, GreenAndRedTrue_GreenWins) {
    std::string result = getDispositionColour(true, false, true);
    EXPECT_EQ(result, GREEN);
}

TEST(ANSIColorsDispositionTest, YellowAndRedTrue_YellowWins) {
    std::string result = getDispositionColour(false, true, true);
    EXPECT_EQ(result, YELLOW);
}

TEST(ANSIColorsDispositionTest, AllThreeTrue_GreenWins) {
    std::string result = getDispositionColour(true, true, true);
    EXPECT_EQ(result, GREEN);
}

TEST(ANSIColorsDispositionTest, DefaultArguments_RedByDefault) {
    // Default: isGreen=false, isYellow=false, isRed=true
    std::string result = getDispositionColour();
    EXPECT_EQ(result, RED);
}

TEST(ANSIColorsDispositionTest, DefaultIsRed_ExplicitFalseOthers) {
    std::string result = getDispositionColour(false, false); // isRed defaults to true
    EXPECT_EQ(result, RED);
}

// Test the overload used in ConsolePresentation: getDispositionColour(bool good, bool warning)
TEST(ANSIColorsDispositionTest, TwoArgOverload_GoodTrue_ReturnsGreen) {
    // This is the 2-arg version: getDispositionColour(bool good, bool warning)
    // good=true, warning=false -> GREEN
    std::string result = getDispositionColour(true, false);
    EXPECT_EQ(result, GREEN);
}

TEST(ANSIColorsDispositionTest, TwoArgOverload_GoodFalseWarningTrue_ReturnsYellow) {
    std::string result = getDispositionColour(false, true);
    EXPECT_EQ(result, YELLOW);
}

TEST(ANSIColorsDispositionTest, TwoArgOverload_BothFalse_ReturnsRed) {
    std::string result = getDispositionColour(false, false);
    EXPECT_EQ(result, RED);
}

TEST(ANSIColorsDispositionTest, TwoArgOverload_BothTrue_GreenWins) {
    std::string result = getDispositionColour(true, true);
    EXPECT_EQ(result, GREEN);
}

// ============================================================================
// Boundary tests with edge case strings
// ============================================================================

TEST(ANSIColorsBoundaryTest, MessageWithEscapeCodes_DoesNotInterfere) {
    std::string msg = "\x1b[31mRED\x1b[0m";
    std::string result = infoMessage(msg);

    // Should wrap the entire string including its own escape codes
    EXPECT_TRUE(result.find(INFO) != std::string::npos);
    EXPECT_TRUE(result.find(RESET) != std::string::npos);
}

TEST(ANSIColorsBoundaryTest, VeryLongMessage_HandlesCorrectly) {
    std::string msg(10000, 'x');
    std::string result = warningMessage(msg);

    EXPECT_TRUE(result.find(YELLOW) != std::string::npos);
    EXPECT_TRUE(result.find(RESET) != std::string::npos);
    EXPECT_EQ(result.size(), YELLOW.size() + msg.size() + RESET.size());
}

TEST(ANSIColorsBoundaryTest, MessageWithNewlines_HandlesCorrectly) {
    std::string msg = "line1\nline2\nline3";
    std::string result = errorMessage(msg);

    EXPECT_TRUE(result.find(RED) != std::string::npos);
    EXPECT_TRUE(result.find("line1") != std::string::npos);
    EXPECT_TRUE(result.find("line2") != std::string::npos);
    EXPECT_TRUE(result.find("line3") != std::string::npos);
    EXPECT_TRUE(result.find(RESET) != std::string::npos);
}