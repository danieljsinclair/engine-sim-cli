// VerificationMacroTest.cpp - Characterization net for the ASSERT macro
// family (F9, consolidation wave A).
//
// F9 deletes the CLI's src/Verification.h near-duplicate and points its users
// at the bridge's include/common/Verification.h. No test previously exercised
// these macros (audit 2026-09-07: only consumer is src/config/CLIMain.cpp,
// six call sites, all 2-arg). This net pins the CURRENT macro semantics
// through the SAME include spelling CLIMain uses (<Verification.h> with src/
// on the include path), so the header swap cannot silently change behaviour:
// when F9 lands, repoint this include alongside CLIMain's and these
// assertions must still pass — that is the proof the move preserved the
// contract.
//
// What is pinned (behaviour, not implementation):
//   - ASSERT(cond, msg): false -> throws std::runtime_error whose what() is
//     EXACTLY msg (verbatim pass-through — CLIMain's fail-fast diagnostics
//     surface these strings to the operator); true -> no throw.
//   - ASSERT(cond, msg, onError): false -> onError() runs first, then the
//     same throw with the exact message; if onError() itself throws, the
//     thrown message is "<msg> + <inner>". true -> onError NOT called.
//
// The exact-message assertions are deliberate here: the macro's entire job is
// verbatim message transport, so the exact bytes ARE the contract.

#include <gtest/gtest.h>

#include "common/Verification.h"

#include <stdexcept>

namespace {

// Lambdas passed to ASSERT must contain no top-level commas (the macro's
// ASSERT_GET_MACRO arity dispatch splits on them), hence comma-free captures.

}  // namespace

TEST(AssertMacroTest, TwoArg_TrueCondition_DoesNotThrow) {
    ASSERT(1 + 1 == 2, "a true condition must not throw");
    SUCCEED();
}

TEST(AssertMacroTest, TwoArg_FalseCondition_ThrowsRuntimeErrorWithExactMessage) {
    try {
        ASSERT(false, "session must exist after the run loop");
        FAIL() << "ASSERT(false, msg) must throw";
    } catch (const std::runtime_error& e) {
        // Verbatim transport: the operator sees the exact diagnostic text.
        EXPECT_STREQ(e.what(), "session must exist after the run loop");
    } catch (...) {
        FAIL() << "ASSERT must throw std::runtime_error, not another type";
    }
}

TEST(AssertMacroTest, ThreeArg_FalseCondition_RunsOnErrorThenThrowsExactMessage) {
    int onErrorCalls = 0;
    try {
        ASSERT(false, "Interactive mode requires an input provider",
              [&onErrorCalls] { ++onErrorCalls; });
        FAIL() << "ASSERT(false, msg, onError) must throw";
    } catch (const std::runtime_error& e) {
        EXPECT_EQ(onErrorCalls, 1) << "onError must run exactly once before the throw";
        EXPECT_STREQ(e.what(), "Interactive mode requires an input provider");
    }
}

TEST(AssertMacroTest, ThreeArg_FailingOnError_AppendsInnerMessage) {
    try {
        ASSERT(false, "outer failure", [] { throw std::runtime_error("inner failure"); });
        FAIL() << "ASSERT(false, msg, throwingOnError) must throw";
    } catch (const std::runtime_error& e) {
        // The chained format "<msg> + <inner>" is the observable contract.
        EXPECT_STREQ(e.what(), "outer failure + inner failure");
    }
}

TEST(AssertMacroTest, ThreeArg_TrueCondition_DoesNotCallOnError) {
    int onErrorCalls = 0;
    ASSERT(true, "must not throw", [&onErrorCalls] { ++onErrorCalls; });
    EXPECT_EQ(onErrorCalls, 0) << "onError must not run when the condition holds";
}
