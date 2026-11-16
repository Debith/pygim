#pragma once
// platform/linux/process.h — the process's calls on Linux, behind platform/process.h's process_calls:
// POSIX's (posix/process.h), and a thread named as Linux names one. Only that header includes it,
// and only where the system is neither Windows nor macOS: nothing here asks which system it runs on
// (D106).
//
// CORE layer: pybind-free.

#include <pthread.h>

#include "../posix/process.h"

namespace pygim::platform {

class linux_process : public posix_process {
public:
    /// pthread_setname_np on the calling thread. Linux keeps at most 15 characters and refuses a
    /// longer name, leaving the thread's name as it was.
    static void name_this_thread(const char* name) noexcept { (void)::pthread_setname_np(::pthread_self(), name); }
};

}  // namespace pygim::platform
