// ISignalStopController.h - Injectable seam for the CLI's signal-driven
// session stop (cpp:S5421: elimination of g_sessionForSignal).
//
// WHY THIS INTERFACE EXISTS (the contract the tests target):
//   The old design reached the live session from a signal handler via a global
//   pointer (g_sessionForSignal) and called session->stop() directly from the
//   handler. That violates async-signal-safety and the "no globals for thread
//   signalling" rule. The replacement (design d1) keeps the session OUT of the
//   signal path: the handler performs only an async-signal-safe pipe write, and
//   a reader thread consumes it and calls session->stop().
//
//   This abstract interface is the testable, injectable surface of that design.
//   It decouples the contract (requestStop -> the attached session stops) from
//   the mechanism (pipe fds, reader thread). Tests assert behavior against this
//   interface; the production SignalStopController is the concrete
//   implementation. createSignalStopController() is the single injection point
//   the production code supplies.
//
// CONTRACT:
//   - attachSession(session): declare the currently-running session that
//     requestStop() should target. Pass nullptr to indicate "no session".
//   - requestStop(): async-signal-safe entry point. May be called from a signal
//     handler, any thread, before any session is attached, or after detach. It
//     must NEVER dereference the session from the calling thread; it arranges
//     for the attached session's stop() to be invoked (via a reader thread
//     draining a self-pipe). Must be safe to call when no session is attached.
//   - detach(): clear the attached session. After detach, requestStop() must
//     not touch any previously-attached session (no use-after-free).
//
// THREAD-SAFETY EXPECTATIONS:
//   attachSession/detach/requestStop may be called concurrently. A requestStop()
//   racing with attach/detach must not crash or dereference a stale session.

#ifndef CLI_I_SIGNAL_STOP_CONTROLLER_H
#define CLI_I_SIGNAL_STOP_CONTROLLER_H

class ISimulatorSession;

#include <memory>

class ISignalStopController {
public:
    virtual ~ISignalStopController() = default;

    /// Attach the live session that requestStop() should stop.
    /// nullptr means "no session currently running".
    virtual void attachSession(ISimulatorSession* session) = 0;

    /// Async-signal-safe stop request. Safe to call from a signal handler,
    /// any thread, before a session is attached, or after detach.
    virtual void requestStop() = 0;

    /// Detach the session. After this, requestStop() must not touch the
    /// previously-attached session.
    virtual void detach() = 0;
};

/// Factory supplied by the production implementer. Returns a controller whose
/// signal path performs only an async-signal-safe pipe write (never touches the
/// session); a reader thread drains the pipe and calls the attached session's
/// stop().
std::unique_ptr<ISignalStopController> createSignalStopController();

#endif  // CLI_I_SIGNAL_STOP_CONTROLLER_H
