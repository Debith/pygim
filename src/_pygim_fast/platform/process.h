#pragma once
// platform/process.h — the process's calls: a thread's name, the signal mask the threads a thread
// starts inherit, hooks around a fork, the process's id. Infrastructure behind the platform
// boundary (docs/design/platform_boundary.md, D77): the pools (utils/parallel.h), the kept blocks
// (utils/kept_blocks.h) and similarity's streams call these and never ask which system they run on.
// Where a system has no such thing (Windows has no fork, no POSIX signals, no thread names here),
// the call does nothing.
//
// CORE layer: pybind-free.

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace pygim::platform {

/// Names the calling thread, as `top -H`, a debugger and /proc/<pid>/task/<tid>/comm show it (at
/// most 15 characters on Linux): pygim-pool for a run's threads, joined when it ends, pygim-parked
/// for the workers parallel::for_each keeps. Where threads have no names (Windows here), nothing.
inline void name_this_thread(const char* name) noexcept {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), name);
#elif defined(__APPLE__)
    pthread_setname_np(name);
#else
    (void)name;
#endif
}

/// While it lives, the calling thread blocks every asynchronous signal, so the threads it starts
/// inherit the mask (a thread starts with its creator's). A process's signal goes to a thread that
/// does not block it: Ctrl-C then reaches the thread that can act on it — Python's main thread,
/// whose blocking call it cuts short — never a pool's worker, which would take it and leave that
/// call running (Linux offers it to the main thread first; macOS to any). A fault (SIGSEGV, SIGBUS,
/// SIGFPE, SIGILL) stays the faulting thread's. Where there are no POSIX signals, nothing.
class signals_blocked {
public:
    signals_blocked() noexcept {
#if defined(__unix__) || defined(__APPLE__)
        sigset_t all;
        sigfillset(&all);   // unqualified: macOS defines sigfillset and sigdelset as macros
        for (int fault : {SIGSEGV, SIGBUS, SIGFPE, SIGILL}) sigdelset(&all, fault);
        m_restore = ::pthread_sigmask(SIG_BLOCK, &all, &m_before) == 0;
#endif
    }
    ~signals_blocked() {
#if defined(__unix__) || defined(__APPLE__)
        if (m_restore) ::pthread_sigmask(SIG_SETMASK, &m_before, nullptr);
#endif
    }
    signals_blocked(const signals_blocked&) = delete;
    signals_blocked& operator=(const signals_blocked&) = delete;

private:
#if defined(__unix__) || defined(__APPLE__)
    sigset_t m_before{};
    bool m_restore = false;
#endif
};

/// Calls `prepare` before a fork, and `parent` and `child` after it in each process: the hooks
/// pthread_atfork sets, for a lock that must not be held across a fork. Whether they were set;
/// where there is no fork, they are not and need not be.
inline bool at_fork(void (*prepare)(), void (*parent)(), void (*child)()) noexcept {
#if defined(__unix__) || defined(__APPLE__)
    return ::pthread_atfork(prepare, parent, child) == 0;
#else
    (void)prepare, (void)parent, (void)child;
    return false;
#endif
}

/// The process's id: a fork's child has a new one. 0 where there is no fork, so it never changes.
[[nodiscard]] inline long process_id() noexcept {
#if defined(__unix__) || defined(__APPLE__)
    return static_cast<long>(::getpid());
#else
    return 0;
#endif
}

}  // namespace pygim::platform
