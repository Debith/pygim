#pragma once
// utils/huge_pages.h — ask for 2 MB pages under a large buffer before it is first written.
//
// CORE layer: pybind-free. A buffer of megabytes that is written once and read soon — a sort's
// output, an index's arrays, a batch of result columns — pays one page fault and one kernel-zeroed
// page per 4 KB on first write. Backed by huge pages it pays one per 2 MB. Linux gives them to a
// region advised MADV_HUGEPAGE (transparent huge pages, in "madvise" mode on most distributions);
// the advice covers the 2 MB-aligned interior of the buffer and is only advice, never an error.
// Elsewhere — macOS, Windows (large pages need a privilege), regions under 2 MB — a no-op.
//
//     std::vector<token_entry> tokens;
//     tokens.reserve(total);                                   // mapped, not yet touched
//     prefer_huge_pages(tokens.data(), total * sizeof(token_entry));
//
// Measured 2026-10-27 (WSL2, Ryzen 7 5800X): writing 50 MB of fresh memory 20-21 ms on 4 KB
// pages, 4.0-4.3 ms on huge pages (the first time the kernel compacts memory for them, 20 ms).
// Random reads over the same 50 MB were unchanged (33-37 ms): the gain is in the faults. Written
// by 16 threads at once the same 50 MB took 3.4 ms on 4 KB pages and 7-8 ms on huge pages (the
// faults on one 2 MB page wait for each other, 2026-10-28): a buffer many threads fill first is
// better on 4 KB pages (utils/unfilled.h); advise the ones one thread writes.

#include <cstddef>
#include <cstdint>

#if defined(__linux__)
#include <sys/mman.h>
#endif

namespace pygim {

/// The buffers worth advising: below this, the 2 MB-aligned interior is small or empty.
inline constexpr std::size_t huge_page_threshold = std::size_t{4} << 20;

/// Advises [p, p + bytes) to be backed by huge pages when first written; see the file comment.
inline void prefer_huge_pages(const void* p, std::size_t bytes) noexcept {
#if defined(__linux__) && defined(MADV_HUGEPAGE)
    if (p == nullptr || bytes < huge_page_threshold) return;
    constexpr std::uintptr_t huge = std::uintptr_t{2} << 20;
    const auto start = reinterpret_cast<std::uintptr_t>(p);
    const std::uintptr_t begin = (start + huge - 1) & ~(huge - 1), end = (start + bytes) & ~(huge - 1);
    if (end > begin) ::madvise(reinterpret_cast<void*>(begin), end - begin, MADV_HUGEPAGE);
#else
    (void)p;
    (void)bytes;
#endif
}

}  // namespace pygim
