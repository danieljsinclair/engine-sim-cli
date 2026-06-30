// SignalStopController.h - Concrete ISignalStopController (cpp:S5421, design d1:
// self-pipe + reader thread).
//
// DESIGN INVARIANT (the reason this class exists): the signal handler must NEVER
// appear in a call stack that dereferences the session. The handler does exactly
// ONE async-signal-safe thing -- write a single byte to a pipe -- and returns.
// A dedicated reader thread drains the pipe and invokes the currently-attached
// session's stop() on the controller's own thread, never the signal-handler
// thread.
//
// The pipe write-fd is init-once immutable for the controller's lifetime (set in
// the constructor, never reassigned); it is the only state reachable from the
// handler path. The mutable per-run session pointer is held in an atomic and is
// touched ONLY by the reader thread (and attach/detach from the main thread) --
// never by requestStop()/the handler.

#ifndef CLI_SIGNAL_STOP_CONTROLLER_H
#define CLI_SIGNAL_STOP_CONTROLLER_H

#include "ISignalStopController.h"

#include <atomic>
#include <thread>

class SignalStopController final : public ISignalStopController {
public:
    SignalStopController();
    ~SignalStopController() override;

    SignalStopController(const SignalStopController&) = delete;
    SignalStopController& operator=(const SignalStopController&) = delete;
    SignalStopController(SignalStopController&&) = delete;
    SignalStopController& operator=(SignalStopController&&) = delete;

    // ISignalStopController
    void attachSession(ISimulatorSession* session) override;
    void requestStop() override;  // async-signal-safe: pipe write only
    void detach() override;

private:
    // Reader-thread loop: blocks on read(pipeReadFd_), on each byte snapshots
    // the current session pointer and, if non-null, calls stop(). Loops until
    // the pipe is closed (shutdown_ set + write end closed in the destructor).
    void readerLoop();

    int pipeReadFd_{};   // drained by the reader thread
    int pipeWriteFd_{};  // written by requestStop() (signal-handler safe)

    // The live session, if any. Touched by attach/detach (main thread) and read
    // by the reader thread. Atomic for safe publication across threads.
    std::atomic<ISimulatorSession*> session_{nullptr};

    std::atomic<bool> shutdown_{false};
    std::thread reader_;
};

#endif  // CLI_SIGNAL_STOP_CONTROLLER_H
