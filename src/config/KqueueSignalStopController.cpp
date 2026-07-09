// KqueueSignalStopController.cpp - macOS provider of ISignalStopController
// (cpp:S5421, kqueue EVFILT_SIGNAL).
//
// See KqueueSignalStopController.h for the provider / Open-Closed structure and
// the design. macOS-only (the CLI's CMake FATAL_ERRORs off non-Apple builds).
// No signal handler, no file-scope pointer: the signal is an EVFILT_SIGNAL event
// on this provider's own kq fd.

#include "KqueueSignalStopController.h"

#include "session/ISimulatorSession.h"

#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

namespace {

// Identifier for the EVFILT_USER wakeup kevent. Must not collide with a signal
// number used as an EVFILT_SIGNAL ident (SIGINT=2, SIGTERM=15); 1 is safe.
constexpr uintptr_t kWakeupIdent = 1;

}  // namespace

KqueueSignalStopController::KqueueSignalStopController() {
    // Block default SIGINT/SIGTERM delivery so the kernel queues them as
    // kevents instead of terminating the process. Save the prior mask so the
    // destructor can restore it (sigprocmask is process-global; tests share it).
    sigset_t blockSet;
    sigemptyset(&blockSet);
    sigaddset(&blockSet, SIGINT);
    sigaddset(&blockSet, SIGTERM);
    sigprocmask(SIG_BLOCK, &blockSet, &savedMask_);

    kq_ = ::kqueue();
    if (kq_ < 0) {
        // kqueue failure is an anticipated, recoverable init error -- the
        // provider simply has no signal-stop capability. requestStop() still
        // works (programmatic path); the reader is not started.
        return;
    }

    // Register SIGINT and SIGTERM as pollable signal events (EV_CLEAR so each
    // delivery coalesces to one event and is auto-rearmed).
    struct kevent sigEvt;
    EV_SET(&sigEvt, SIGINT, EVFILT_SIGNAL, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    ::kevent(kq_, &sigEvt, 1, nullptr, 0, nullptr);
    EV_SET(&sigEvt, SIGTERM, EVFILT_SIGNAL, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    ::kevent(kq_, &sigEvt, 1, nullptr, 0, nullptr);

    // Register an EVFILT_USER kevent the destructor can trigger (NOTE_TRIGGER)
    // to wake the blocked reader for shutdown.
    struct kevent wakeEvt;
    EV_SET(&wakeEvt, kWakeupIdent, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    ::kevent(kq_, &wakeEvt, 1, nullptr, 0, nullptr);

    reader_ = std::thread([this] { readerLoop(); });
}

KqueueSignalStopController::~KqueueSignalStopController() {
    shutdown_.store(true);

    if (kq_ >= 0) {
        // Trigger the EVFILT_USER kevent to unblock the reader so it can observe
        // shutdown_ and exit, making the join below bounded.
        struct kevent trigger;
        EV_SET(&trigger, kWakeupIdent, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
        ::kevent(kq_, &trigger, 1, nullptr, 0, nullptr);
    }

    if (reader_.joinable()) reader_.join();

    if (kq_ >= 0) {
        ::close(kq_);
        kq_ = -1;
    }

    // Restore the signal mask saved at construction.
    sigprocmask(SIG_SETMASK, &savedMask_, nullptr);
}

void KqueueSignalStopController::attachSession(ISimulatorSession* session) {
    session_.store(session);
}

void KqueueSignalStopController::requestStop() {
    // Programmatic/test stop path. Drives the same stop logic as the kqueue
    // reader; no signal is sent. The blind tests exercise this entry point.
    stopSession();
}

void KqueueSignalStopController::detach() {
    session_.store(nullptr);
}

void KqueueSignalStopController::stopSession() const {
    ISimulatorSession* s = session_.load();
    if (s != nullptr) s->stop();
}

void KqueueSignalStopController::readerLoop() const {
    std::array<struct kevent, 4> events{};
    while (!shutdown_.load()) {
        // Block until a signal event or the EVFILT_USER wakeup arrives.
        int n = ::kevent(kq_, nullptr, 0, events.data(), std::size(events), nullptr);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;  // error or kq closed: stop draining
        }
        for (int i = 0; i < n; ++i) {
            if (events[i].filter == EVFILT_SIGNAL) {
                stopSession();
            }
            // EVFILT_USER is the shutdown wakeup; the loop check handles it.
        }
    }
}

// ============================================================================
// Platform dispatch seam (Open/Closed)
// ============================================================================
// createSignalStopController() is the single injection point. Callers (main()
// and tests) depend only on ISignalStopController; this factory selects the
// concrete provider for the build platform. ADDING A PLATFORM = ADDING A
// PROVIDER CLASS + a branch here -- the interface and all callers stay closed.
//
//   macOS  -> KqueueSignalStopController            (kqueue EVFILT_SIGNAL)
//   Linux  -> SignalfdSignalStopController          (signalfd, future)
//   Win32  -> a Windows provider                     (injected adapter, future)
std::unique_ptr<ISignalStopController> createSignalStopController() {
#if defined(__APPLE__)
    return std::make_unique<KqueueSignalStopController>();
#else
    #error "No ISignalStopController provider for this platform. Add one (see seam above)."
#endif
}
