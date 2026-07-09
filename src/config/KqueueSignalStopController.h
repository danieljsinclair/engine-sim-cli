// KqueueSignalStopController.h - The macOS provider of ISignalStopController
// (cpp:S5421: kqueue EVFILT_SIGNAL, zero file-scope statics).
//
// PROVIDER / Open-Closed structure (the reason this is a named provider, not a
// generic "SignalStopController"): ISignalStopController is the closed interface
// (attachSession/requestStop/detach + the createSignalStopController() factory).
// Each platform supplies its own concrete provider as a NEW class, selected by
// the factory -- adding a platform does NOT modify the interface or any caller:
//   - KqueueSignalStopController   (this file) -- macOS, via kqueue EVFILT_SIGNAL.
//   - (future) SignalfdSignalStopController   -- Linux, via signalfd.
//   - (future) a Windows provider              -- via injected platform adapter.
// See createSignalStopController() in KqueueSignalStopController.cpp for the
// platform dispatch seam.
//
// DESIGN INVARIANT: a signal must NEVER drive a call stack that dereferences the
// session, and there is NO file-scope/global pointer wiring the signal to the
// controller. This provider makes the signal a *pollable event* instead of a
// handler-dereferenced global:
//   - sigprocmask(SIG_BLOCK) blocks default SIGINT/SIGTERM delivery so the
//     kernel queues them rather than killing the process.
//   - A kqueue fd (instance member) registers SIGINT/SIGTERM as EVFILT_SIGNAL
//     events (EV_ADD | EV_CLEAR).
//   - A dedicated reader thread blocks on kevent(kq_); on a signal event it
//     calls stopSession() -> the attached session's stop().
//   - requestStop() is the programmatic/test entry: it calls stopSession()
//     directly (no signal needed). Both paths converge on stopSession().
//   - An EVFILT_USER kevent is the destructor's wakeup: triggering it unblocks
//     the reader so it can observe shutdown_ and exit cleanly.
//
// No signal handler function, no file-scope pointer -- the signal is an event on
// this instance's own kq fd, reached via the injected factory.

#ifndef CLI_KQUEUE_SIGNAL_STOP_CONTROLLER_H
#define CLI_KQUEUE_SIGNAL_STOP_CONTROLLER_H

#include "ISignalStopController.h"

#include <array>
#include <atomic>
#include <signal.h>
#include <thread>

// macOS provider: watches SIGINT/SIGTERM via a kqueue (EVFILT_SIGNAL).
class KqueueSignalStopController final : public ISignalStopController {
public:
    KqueueSignalStopController();
    ~KqueueSignalStopController() override;

    KqueueSignalStopController(const KqueueSignalStopController&) = delete;
    KqueueSignalStopController& operator=(const KqueueSignalStopController&) = delete;
    KqueueSignalStopController(KqueueSignalStopController&&) = delete;
    KqueueSignalStopController& operator=(KqueueSignalStopController&&) = delete;

    // ISignalStopController
    void attachSession(ISimulatorSession* session) override;
    void requestStop() override;  // programmatic/test stop; calls stopSession()
    void detach() override;

private:
    // Shared stop path for both the kqueue reader (signal) and requestStop()
    // (programmatic). Loads the currently-attached session and, if non-null,
    // calls its stop(). Null-guard kept: a stop before attach / after detach is
    // a legit inert no-op.
    void stopSession() const;

    // Reader-thread loop: blocks on kevent(kq_). On a SIGINT/SIGTERM event it
    // calls stopSession(); on the EVFILT_USER wakeup it observes shutdown_ and
    // returns. Loops until shutdown.
    void readerLoop() const;

    int kq_{-1};  // kqueue fd owning the signal + wakeup filters

    // Signal mask saved at construction so the destructor can restore it.
    sigset_t savedMask_{};

    // The live session, if any. Touched by attach/detach (main thread) and read
    // by the reader thread / stopSession(). Atomic for safe publication.
    std::atomic<ISimulatorSession*> session_{nullptr};

    std::atomic<bool> shutdown_{false};
    std::thread reader_;
};

#endif  // CLI_KQUEUE_SIGNAL_STOP_CONTROLLER_H
