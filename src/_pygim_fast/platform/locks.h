#pragma once
// platform/locks.h — a file locked across processes: open it, take it exclusively, release it, close
// it. Infrastructure behind the platform boundary (docs/design/platform_boundary.md, D77): a writer
// that must be the only one at a time, such as a file store's commit lock, takes it around each write
// and never asks which system it runs on.
//
//     const int fd = native_lock_calls::open(path);   // -1 when the file cannot be opened
//     native_lock_calls::lock(fd);                    // false when the lock cannot be taken
//     native_lock_calls::unlock(fd); native_lock_calls::close(fd);
//
// The calls are one thin wrapper each, in a file of the system's own that asks nothing (D106):
// posix/locks.h (open, flock, close) and windows/locks.h (_wsopen_s, LockFileEx through the
// descriptor's handle, _close). Both answer for an int descriptor, so a lock above them holds one
// type on every system. Nothing here is logic above the calls, so there is nothing to prove over
// fakes (D138); tests/static/platform_proofs.cpp includes this header so that every build compiles
// the running system's calls against the concept below.
//
// CORE layer: pybind-free.

#include <concepts>
#include <filesystem>

namespace pygim::platform {

/// What every system's lock calls answer: a descriptor (or -1), and whether the lock was taken.
template <class L>
concept file_lock_calls = requires(const std::filesystem::path& p, int fd) {
    { L::open(p) } noexcept -> std::same_as<int>;
    { L::lock(fd) } noexcept -> std::same_as<bool>;
    { L::unlock(fd) } noexcept -> std::same_as<void>;
    { L::close(fd) } noexcept -> std::same_as<void>;
};

}  // namespace pygim::platform

// The one choice: each system's calls file is included only where its headers exist.
#if defined(_WIN32)
#include "windows/locks.h"
namespace pygim::platform { using native_lock_calls = win32_lock_calls; }
#else
#include "posix/locks.h"
namespace pygim::platform { using native_lock_calls = posix_lock_calls; }
#endif

namespace pygim::platform {
static_assert(file_lock_calls<native_lock_calls>);
}  // namespace pygim::platform
