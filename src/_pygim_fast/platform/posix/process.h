#pragma once
// platform/posix/process.h — the process's calls every POSIX system makes alike: the signal mask a
// thread's new threads inherit, and the hooks around a fork. linux/process.h and macos/process.h
// build on it, adding how each names a thread; only they include it, and nothing here asks which
// system it runs on (D106).
//
// CORE layer: pybind-free.

#include <pthread.h>
#include <signal.h>

namespace pygim::platform {

/// pthread_sigmask over every asynchronous signal while it lives; the faults stay the thread's own.
class posix_signals_blocked {
public:
    posix_signals_blocked() noexcept {
        sigset_t all;
        sigfillset(&all);   // unqualified: macOS defines sigfillset and sigdelset as macros
        for (int fault : {SIGSEGV, SIGBUS, SIGFPE, SIGILL}) sigdelset(&all, fault);
        m_restore = ::pthread_sigmask(SIG_BLOCK, &all, &m_before) == 0;
    }
    ~posix_signals_blocked() {
        if (m_restore) ::pthread_sigmask(SIG_SETMASK, &m_before, nullptr);
    }
    posix_signals_blocked(const posix_signals_blocked&) = delete;
    posix_signals_blocked& operator=(const posix_signals_blocked&) = delete;

private:
    sigset_t m_before{};
    bool m_restore = false;
};

class posix_process {
public:
    using signals_blocked = posix_signals_blocked;

    /// pthread_atfork(prepare, parent, child); whether the hooks were set.
    static bool at_fork(void (*prepare)(), void (*parent)(), void (*child)()) noexcept {
        return ::pthread_atfork(prepare, parent, child) == 0;
    }
};

}  // namespace pygim::platform
