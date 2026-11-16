#pragma once
// platform/macos/process.h — the process's calls on macOS, behind platform/process.h's process_calls:
// POSIX's (posix/process.h), and a thread named as macOS names one. Only that header includes it,
// and only on macOS: nothing here asks which system it runs on (D106).
//
// CORE layer: pybind-free.

#include <pthread.h>

#include "../posix/process.h"

namespace pygim::platform {

class macos_process : public posix_process {
public:
    /// pthread_setname_np(name): macOS names only the calling thread, and takes no thread argument.
    static void name_this_thread(const char* name) noexcept { (void)::pthread_setname_np(name); }
};

}  // namespace pygim::platform
