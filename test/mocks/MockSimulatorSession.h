// MockSimulatorSession.h - Test double for ISimulatorSession.
//
// Records lifecycle calls (stop/close) so signal-stop controller tests can
// OBSERVE that a stop request reached the session — without coupling to the
// controller's pipe/thread internals.
//
// Part of the blind TDD coverage for the g_sessionForSignal elimination
// (cpp:S5421, design d1: self-pipe + reader thread). The production
// SignalStopController must NEVER appear in the signal-handler call stack;
// instead the handler does an async-signal-safe pipe write and a reader thread
// calls session->stop(). These tests assert that observable contract.

#ifndef MOCK_SIMULATOR_SESSION_H
#define MOCK_SIMULATOR_SESSION_H

#include "session/ISimulatorSession.h"

#include <atomic>
#include <stdexcept>
#include <thread>

namespace test {

// A minimal ISimulatorSession double. run() blocks until stop() is invoked
// (so a controller under test can be observed actually stopping a "running"
// session), and records stop()/close() counts.
class MockSimulatorSession : public ISimulatorSession {
public:
    MockSimulatorSession() = default;
    ~MockSimulatorSession() override = default;

    // Blocks until stopCount_ > 0 (i.e. until stop() has been called at least
    // once since run() started), then returns 0. This models a real session's
    // "run() blocks until stop()" contract and lets tests deterministically
    // observe the stop arriving. Returns non-zero if the run was cancelled by
    // destruction (kept simple: we don't need that path in these tests).
    int run() override {
        while (stopCount_.load(std::memory_order_acquire) == 0) {
            std::this_thread::yield();
        }
        return 0;
    }

    void stop() override {
        stopCount_.fetch_add(1, std::memory_order_release);
    }

    void close() override {
        closeCount_.fetch_add(1, std::memory_order_release);
    }

    ISimulator* getSimulator() const override { return nullptr; }

    // ---- observability for tests ----
    int stopCount() const { return stopCount_.load(std::memory_order_acquire); }
    int closeCount() const { return closeCount_.load(std::memory_order_acquire); }

private:
    std::atomic<int> stopCount_{0};
    std::atomic<int> closeCount_{0};
};

}  // namespace test

#endif  // MOCK_SIMULATOR_SESSION_H
