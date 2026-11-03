#pragma once
// utils/memory.h — the process's resident memory, as one syscall.
//
// CORE layer: pybind-free. resident_bytes() is what the OS holds in physical
// memory for this process right now; peak_resident_bytes() the high-water
// mark. Both are process-wide numbers (every object, the interpreter, memory
// the allocator keeps after a free): the right probe for a benchmark's
// before/after delta on a fresh heap, never a per-object size — components
// report their own exact bytes() for that (docs/design/mapping_toolkit.md).
// Bound to Python as utils.rss_bytes / peak_rss_bytes / rss_mb / peak_rss_mb
// (utils/bindings.cpp).
//
// Cost: one system call (platform/memory.h picks the system's class:
// /proc/self/statm on Linux, the Mach task on macOS, the working set on
// Windows) — about a microsecond on Linux. Cheap enough to sit inside a
// measured loop without moving the number. A probe whose call fails reads 0,
// and the peak is never below the present reading (Linux samples its peak at
// scheduling events, so platform/linux/memory.h returns the larger).
//
// A worked example, used in the comments below — what
// tests/unittests/test_utils.py::test_rss_grows_with_a_large_allocation does:
//
//     before = rss_bytes()
//     block  = bytearray(64 * 2**20)          # 64 MiB reserved, not yet resident
//     block[::4096] = b"x" * ...              # one byte per page: now it is
//     after  = rss_bytes()
//
//     after - before        -> about 64 MiB (16384 pages of 4 KiB; the test accepts > 32 MiB)
//     peak_rss_bytes()      -> >= after
//     del block; rss_bytes()-> not necessarily back to `before`
//
// Only the delta means anything. The absolute reading is the whole process;
// and once `block` is freed the allocator may keep the pages, so a second
// measurement of the same allocation in the same process can show no growth
// at all — a benchmark takes its delta on a fresh heap, once.

#include <cstdint>

#include "../platform/memory.h"

namespace pygim::memory {

/// What the system holds in physical memory for the process now, in bytes.
[[nodiscard]] inline std::uint64_t resident_bytes() noexcept { return platform::native_memory::resident_bytes(); }
/// The process's high-water mark, in bytes; >= resident_bytes().
[[nodiscard]] inline std::uint64_t peak_resident_bytes() noexcept { return platform::native_memory::peak_resident_bytes(); }

// ── in mebibytes ──────────────────────────────────────────────────────────

/// resident_bytes() / 2^20, as a double — for a printed figure or a
/// benchmark's JSONL row, not for arithmetic.        after - before -> ~64.0
[[nodiscard]] inline double resident_mb() noexcept { return static_cast<double>(resident_bytes()) / (1024.0 * 1024.0); }
/// peak_resident_bytes() / 2^20, as a double.        >= resident_mb() at the same moment
[[nodiscard]] inline double peak_resident_mb() noexcept { return static_cast<double>(peak_resident_bytes()) / (1024.0 * 1024.0); }

}  // namespace pygim::memory
