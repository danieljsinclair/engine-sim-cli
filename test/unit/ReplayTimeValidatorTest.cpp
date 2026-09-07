// ReplayTimeValidatorTest.cpp - Spec-driven tests for the extracted validator.
//
// Tests the CONTRACT documented in ReplayTimeValidator.h (originally the
// anonymous-namespace function in CLIMain.cpp lines 77-101). We drive the
// validator through the IReplayTimeline interface seam with a hand-written
// stub — we do NOT depend on the concrete ReplayTelemetryProvider (which lives
// behind the bridge boundary) and we do NOT touch any internal-linkage symbol.
//
// Non-fragile assertions: exception checks assert the TYPE (CliException), not
// exact message strings; the clamp side-effect is asserted via the stub's
// captured setEndAtS argument.

#include <gtest/gtest.h>

#include "config/CLIconfig.h"          // CommandLineArgs, ReplayArgs
#include "config/CliException.h"        // CliException
#include "input/IReplayTimeline.h"      // input::IReplayTimeline (the seam)
#include "config/ReplayTimeValidator.h" // validateReplayTimeSlicing

// Minimal stand-in for input::IReplayTimeline (ISP: only the two methods the
// validator needs). Captures the last setEndAtS() argument so tests can assert
// the clamp side-effect. Last-end starts at a sentinel (-999) so "unchanged" is
// observable.
class StubTimeline : public input::IReplayTimeline {
public:
    explicit StubTimeline(double durationSeconds)
        : durationSeconds_(durationSeconds) {}

    double durationS() const override { return durationSeconds_; }
    void setEndAtS(double seconds) override { lastSetEndAtS_ = seconds; }

    double lastSetEndAtS() const { return lastSetEndAtS_; }

private:
    double durationSeconds_;
    double lastSetEndAtS_ = -999.0;
};

// Builds a CommandLineArgs with the replay time-slice fields populated. Other
// fields keep their defaults — the validator only reads args.replay.*.
static CommandLineArgs makeArgs(double startFromS, double endAtS) {
    CommandLineArgs args;
    args.replay.startFromS = startFromS;
    args.replay.endAtS = endAtS;
    return args;
}

// ============================================================================
// Spec: replay == nullptr -> return immediately, no throw.
// ============================================================================
TEST(ReplayTimeValidatorTest, NullTimeline_ReturnsEarlyNoThrow) {
    CommandLineArgs args = makeArgs(5.0, 30.0);
    EXPECT_NO_THROW(validateReplayTimeSlicing(args, nullptr));
}

// ============================================================================
// Spec: startFromS >= 0, traceDur > 0, startFromS >= traceDur -> throw CliException.
// ============================================================================
TEST(ReplayTimeValidatorTest, StartFromPastTraceEnd_ThrowsCliException) {
    StubTimeline timeline(/*durationS=*/10.0);
    CommandLineArgs args = makeArgs(/*startFromS=*/100.0, /*endAtS=*/-1.0);
    EXPECT_THROW(validateReplayTimeSlicing(args, &timeline), CliException);
}

// ============================================================================
// Spec: endAtS > traceDur -> warn + setEndAtS(-1.0), NO throw.
// Asserts the specified clamp side-effect via the captured argument.
// ============================================================================
TEST(ReplayTimeValidatorTest, EndAtPastTraceEnd_CallsSetEndAtNegativeOne_NoThrow) {
    StubTimeline timeline(/*durationS=*/10.0);
    CommandLineArgs args = makeArgs(/*startFromS=*/-1.0, /*endAtS=*/100.0);
    EXPECT_NO_THROW(validateReplayTimeSlicing(args, &timeline));
    EXPECT_DOUBLE_EQ(timeline.lastSetEndAtS(), -1.0);
}

// ============================================================================
// Spec: startFromS >= endAtS (both >= 0) -> throw CliException.
// Start strictly after end.
// ============================================================================
TEST(ReplayTimeValidatorTest, StartFromAfterEndAt_ThrowsCliException) {
    StubTimeline timeline(/*durationS=*/100.0);
    CommandLineArgs args = makeArgs(/*startFromS=*/50.0, /*endAtS=*/30.0);
    EXPECT_THROW(validateReplayTimeSlicing(args, &timeline), CliException);
}

// ============================================================================
// Spec: startFromS >= endAtS (both >= 0) -> throw CliException.
// Boundary: start == end (the >= makes equality fail too).
// ============================================================================
TEST(ReplayTimeValidatorTest, StartFromEqualsEndAt_ThrowsCliException) {
    StubTimeline timeline(/*durationS=*/100.0);
    CommandLineArgs args = makeArgs(/*startFromS=*/30.0, /*endAtS=*/30.0);
    EXPECT_THROW(validateReplayTimeSlicing(args, &timeline), CliException);
}

// ============================================================================
// Spec: valid slice (startFromS < endAtS < traceDur) -> no throw, no side-effect.
// The stub's captured end-at stays at its sentinel -> setEndAtS was never called.
// ============================================================================
TEST(ReplayTimeValidatorTest, ValidTimes_NoThrowNoSideEffect) {
    StubTimeline timeline(/*durationS=*/100.0);
    CommandLineArgs args = makeArgs(/*startFromS=*/5.0, /*endAtS=*/30.0);
    EXPECT_NO_THROW(validateReplayTimeSlicing(args, &timeline));
    EXPECT_DOUBLE_EQ(timeline.lastSetEndAtS(), -999.0);
}

// ============================================================================
// Spec: traceDur == 0 -> every time check is guarded by `traceDur > 0`, so even
// extreme start/end values must NOT throw.
// ============================================================================
TEST(ReplayTimeValidatorTest, ZeroTraceDuration_NoThrow) {
    StubTimeline timeline(/*durationS=*/0.0);
    CommandLineArgs args = makeArgs(/*startFromS=*/1e9, /*endAtS=*/2e9);
    EXPECT_NO_THROW(validateReplayTimeSlicing(args, &timeline));
}
