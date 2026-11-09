#pragma once
// platform/windows/process.h — the process's calls on Windows, behind platform/process.h's
// process_calls. Only that header includes it, and only on Windows: nothing here asks which system
// it runs on (D106).
//
// Windows has no fork and no POSIX signals (Ctrl-C reaches a handler on a thread of its own), so the
// signal mask and the fork hooks do nothing; the pools' threads go unnamed here.
//
// CORE layer: pybind-free.

namespace pygim::platform {

class windows_process {
public:
    /// Nothing to block: no POSIX signals reach a thread here.
    class signals_blocked {
    public:
        signals_blocked() noexcept {}   // user-provided: a const one needs no initializer
        signals_blocked(const signals_blocked&) = delete;
        signals_blocked& operator=(const signals_blocked&) = delete;
    };

    /// Nothing: threads go unnamed here.
    static void name_this_thread(const char*) noexcept {}

    /// No fork, so no hooks: never set, and never needed.
    static bool at_fork(void (*)(), void (*)(), void (*)()) noexcept { return false; }
};

}  // namespace pygim::platform
