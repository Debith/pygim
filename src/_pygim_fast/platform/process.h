#pragma once
// platform/process.h — the process's calls the pools make: naming a thread, the signal mask the
// threads a thread starts inherit, hooks around a fork. Infrastructure behind the platform boundary
// (docs/design/platform_boundary.md): the pools (core/primitives/parallel/crew.h and
// for_each.h) call these and never ask which system they run on. Where a system has no such thing
// (Windows has no fork and no POSIX signals), the call does nothing.
//
// One class per system, each in a file of its own that asks nothing: POSIX's signal mask and
// fork hooks in posix/process.h, and naming a thread, which Linux and macOS spell differently, in
// linux/process.h and macos/process.h over it; windows/process.h. This header states what every one
// of them answers and picks the running system's once:
//
//     platform::native_process::name_this_thread("pygim-pool");    // as top -H and a debugger show it
//     const platform::native_process::signals_blocked quiet;       // threads started while it lives inherit the mask
//     platform::native_process::at_fork(prepare, parent, child);   // pthread_atfork, where there is a fork
//
// No class puts logic above its calls, so none is a template on its calls: a fake would prove
// only itself. They are their calls, proven by CI on their own systems and by the threads stress
// (tests/static/threads_stress.cpp) on Linux.
//
// CORE layer: pybind-free.

#include <concepts>
#include <type_traits>

namespace pygim::platform {

/// What every system's process class answers:
///
/// name_this_thread(name) names the calling thread, as `top -H`, a debugger and
/// /proc/<pid>/task/<tid>/comm show it (at most 15 characters on Linux): pygim-pool for a pool's
/// threads, pygim-parked for the workers for_each keeps. Where threads have no names (Windows),
/// nothing.
///
/// While a signals_blocked lives, the calling thread blocks every asynchronous signal, so the
/// threads it starts inherit the mask (a thread starts with its creator's). A process's signal
/// goes to a thread that does not block it: Ctrl-C then reaches the thread that can act on it —
/// Python's main thread, whose blocking call it cuts short — never a pool's worker, which would
/// take it and leave that call running (Linux offers it to the main thread first; macOS to any).
/// A fault (SIGSEGV, SIGBUS, SIGFPE, SIGILL) stays the faulting thread's. Where there are no POSIX
/// signals, nothing.
///
/// at_fork(prepare, parent, child) calls `prepare` before a fork, and `parent` and `child` after it
/// in each process: the hooks pthread_atfork sets, for a lock that must not be held across a fork.
/// Whether they were set; where there is no fork, they are not and need not be.
template <class P>
concept process_calls = std::is_nothrow_default_constructible_v<typename P::signals_blocked> &&
                        requires(const char* name, void (*hook)()) {
                            { P::name_this_thread(name) } noexcept;
                            { P::at_fork(hook, hook, hook) } noexcept -> std::same_as<bool>;
                        };

}  // namespace pygim::platform

// The one choice: each system's file is included only where its headers exist.
#if defined(_WIN32)
#include "windows/process.h"
namespace pygim::platform { using native_process = windows_process; }
#elif defined(__APPLE__)
#include "macos/process.h"
namespace pygim::platform { using native_process = macos_process; }
#else
#include "linux/process.h"   // and any other POSIX system that names a thread as Linux does
namespace pygim::platform { using native_process = linux_process; }
#endif

static_assert(pygim::platform::process_calls<pygim::platform::native_process>);
