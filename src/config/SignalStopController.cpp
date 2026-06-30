// SignalStopController.cpp - Concrete ISignalStopController (cpp:S5421, d1).
//
// See SignalStopController.h for the design invariant. requestStop() is the
// ONLY method reachable from a signal handler; it performs a single
// async-signal-safe pipe write and never touches the session.

#include "SignalStopController.h"

#include "session/ISimulatorSession.h"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace {

// Async-signal-safe write: retries only on EINTR (the signal-handler-safe
// retry condition). Any other error is intentionally ignored -- a stop request
// is best-effort; if the pipe is full or closed there is nothing useful to do
// from the handler, and the reader thread keeps running regardless.
void asyncSafeWrite(int fd, const void* buf, size_t n) {
    const auto* p = static_cast<const char*>(buf);
    size_t written = 0;
    while (written < n) {
        ssize_t r = ::write(fd, p + written, n - written);
        if (r < 0) {
            if (errno == EINTR) continue;
            return;  // best-effort; ignore all other errors
        }
        written += static_cast<size_t>(r);
    }
}

}  // namespace

SignalStopController::SignalStopController() {
    int fds[2] = {-1, -1};
    // pipe2 would let us set CLOEXEC atomically, but pipe() + the consumer
    // (a single long-lived CLI process) is portable and sufficient here.
    if (::pipe(fds) != 0) {
        // Pipe creation failure is an anticipated, recoverable init error -- the
        // controller simply has no signal-stop capability. Leave both fds == -1;
        // requestStop()/readerLoop() handle the "no pipe" state safely.
        pipeReadFd_ = -1;
        pipeWriteFd_ = -1;
        return;
    }
    pipeReadFd_ = fds[0];
    pipeWriteFd_ = fds[1];

    // Reader thread starts last, once all members are initialized.
    reader_ = std::thread([this] { readerLoop(); });
}

SignalStopController::~SignalStopController() {
    shutdown_.store(true);

    // Closing the write end makes the reader's blocking read() return 0 (EOF),
    // which exits readerLoop() so the join below completes in bounded time.
    if (pipeWriteFd_ >= 0) {
        ::close(pipeWriteFd_);
        pipeWriteFd_ = -1;
    }

    if (reader_.joinable()) reader_.join();

    if (pipeReadFd_ >= 0) {
        ::close(pipeReadFd_);
        pipeReadFd_ = -1;
    }
}

void SignalStopController::attachSession(ISimulatorSession* session) {
    session_.store(session);
}

void SignalStopController::requestStop() {
    if (pipeWriteFd_ < 0) return;  // no pipe (init failed): inert, safe
    const char byte = 'x';
    asyncSafeWrite(pipeWriteFd_, &byte, 1);
}

void SignalStopController::detach() {
    session_.store(nullptr);
}

void SignalStopController::readerLoop() {
    if (pipeReadFd_ < 0) return;

    char buf[64];
    while (!shutdown_.load()) {
        ssize_t r = ::read(pipeReadFd_, buf, sizeof(buf));
        if (r <= 0) {
            if (r < 0 && errno == EINTR) continue;
            break;  // 0 == EOF (write end closed), other errors: stop draining
        }

        // Service one stop per byte written. Multiple requestStop() calls may
        // coalesce into a single read(); each byte is one independent stop
        // request and maps to one stop() on the currently-attached session.
        for (ssize_t i = 0; i < r; ++i) {
            // Snapshot the session AFTER the byte is observed so a concurrent
            // attach/detach can't hand us a stale pointer; the atomic load gives
            // safe publication. If null (no session / detached), this request
            // is inert -- the core "no session in the stop path" safety.
            ISimulatorSession* s = session_.load();
            if (s != nullptr) s->stop();
        }
    }
}

// Factory: the single injection point. Production (main()) constructs here;
// tests construct here too and exercise the behavior through the interface.
std::unique_ptr<ISignalStopController> createSignalStopController() {
    return std::make_unique<SignalStopController>();
}
