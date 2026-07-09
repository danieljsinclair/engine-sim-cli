// SignalStopControllerTest.cpp - Blind behavior tests for the CLI's
// signal-driven session stop (cpp:S5421, design d1: self-pipe + reader thread).
//
// BLIND TDD: these tests are authored BEFORE the production SignalStopController
// exists. They target the ISignalStopController seam (see
// test/mocks/ISignalStopController.h) and the MockSimulatorSession. They
// compile now; they LINK (and go GREEN) once the implementer provides
// createSignalStopController().
//
// WHAT WE ASSERT (the OBSERVABLE contract, impl-agnostic):
//   1. A requestStop() against an attached running session causes that
//      session's stop() to be invoked (the whole point of the feature).
//   2. Shutdown is graceful — the session's run() returns (exit 0), no crash.
//   3. requestStop() before any session is attached does not crash (no
//      dereference of a null/absent session).
//   4. After detach(), a requestStop() must not touch the previously-attached
//      session (no use-after-free / no late stop on a cleaned-up session).
//   5. Re-attaching a new session redirects requestStop() to the new one.
//
// We deliberately do NOT assert on pipe fds, thread handles, or any internal
// mechanism — only the behavior, so the tests survive any implementation that
// honors the contract.

#include <gtest/gtest.h>

#include "config/ISignalStopController.h"
#include "MockSimulatorSession.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace {

// Polling helper: wait until predicate is true, with a hard timeout so a buggy
// controller (that never stops the session) fails the test instead of hanging
// the suite.
template <typename Predicate>
bool waitFor(Predicate pred,
             std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

}  // namespace

// ============================================================================
// 1. requestStop() stops the attached running session
//    THE core behavior: a stop request reaches the session via stop().
//    Exercises the full path the old global-pointer dereference used to serve.
// ============================================================================
TEST(SignalStopController, RequestStop_StopsAttachedSession) {
    auto controller = createSignalStopController();
    ASSERT_NE(controller, nullptr);

    test::MockSimulatorSession session;
    controller->attachSession(&session);

    // requestStop() must arrange for session.stop() to be called.
    controller->requestStop();

    EXPECT_TRUE(waitFor([&] { return session.stopCount() > 0; }))
        << "requestStop() did not result in session.stop() being called";
}

// ============================================================================
// 2. Graceful shutdown: run() returns (clean exit) once stop is requested
//    Asserts the shutdown is orderly — the run loop terminates, no crash/kill.
// ============================================================================
TEST(SignalStopController, RequestStop_LetsRunReturnGracefully) {
    auto controller = createSignalStopController();
    ASSERT_NE(controller, nullptr);

    test::MockSimulatorSession session;
    controller->attachSession(&session);

    // run() blocks until stop() arrives. Drive it on a worker, request stop
    // from the main thread, and confirm run() returns 0 in bounded time.
    std::atomic<int> runResult{-1};
    std::thread worker([&] { runResult.store(session.run()); });

    controller->requestStop();

    // run() must return (the session was stopped), not hang.
    ASSERT_TRUE(waitFor([&] { return runResult.load() >= 0; }))
        << "session.run() did not return after requestStop()";
    EXPECT_EQ(runResult.load(), 0) << "Graceful stop should yield exit code 0";

    worker.join();
}

// ============================================================================
// 3. requestStop() before any session is attached must not crash
//    The handler can fire before/after a session exists; no dereference.
//    This is the "no session in the handler path" safety property.
// ============================================================================
TEST(SignalStopController, RequestStop_WithNoSession_DoesNotCrash) {
    auto controller = createSignalStopController();
    ASSERT_NE(controller, nullptr);

    // Fresh controller, never attached: requestStop() must be a no-op-safe call.
    EXPECT_NO_FATAL_FAILURE(controller->requestStop());

    // Explicitly attached to nullptr (equivalent state): also safe.
    controller->attachSession(nullptr);
    EXPECT_NO_FATAL_FAILURE(controller->requestStop());
}

// ============================================================================
// 4. After detach(), requestStop() must not touch the prior session
//    Guards the use-after-free the global design flirted with: once a session
//    is cleaned up and detached, a late/stray requestStop() must be inert
//    against it.
// ============================================================================
TEST(SignalStopController, RequestStop_AfterDetach_DoesNotStopOldSession) {
    auto controller = createSignalStopController();
    ASSERT_NE(controller, nullptr);

    test::MockSimulatorSession session;
    controller->attachSession(&session);
    controller->detach();

    const int stopsBefore = session.stopCount();
    controller->requestStop();

    // Give any (buggy) deferred stop a chance to land, then assert none did.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(session.stopCount(), stopsBefore)
        << "requestStop() after detach must not invoke the old session's stop()";
}

// ============================================================================
// 5. Re-attach redirects requestStop() to the new session
//    The preset-cycling loop attaches a fresh session each iteration; the
//    controller must target whichever session is currently attached.
// ============================================================================
TEST(SignalStopController, RequestStop_TargetsCurrentlyAttachedSession) {
    auto controller = createSignalStopController();
    ASSERT_NE(controller, nullptr);

    test::MockSimulatorSession first;
    test::MockSimulatorSession second;

    controller->attachSession(&first);
    controller->requestStop();
    ASSERT_TRUE(waitFor([&] { return first.stopCount() > 0; }));

    // Swap to a new session (mirrors a preset hot-swap), then request stop.
    controller->attachSession(&second);
    controller->requestStop();

    EXPECT_TRUE(waitFor([&] { return second.stopCount() > 0; }))
        << "requestStop() must target the currently-attached session";
}
