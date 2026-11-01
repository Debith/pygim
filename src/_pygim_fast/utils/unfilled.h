#pragma once
// utils/unfilled.h — vectors of values that are each written before they are read: resized without
// being zeroed, so their pages fault in the threads that fill them.
//
// CORE layer: pybind-free. A vector's resize writes every new value — zeroes, for integers — in the
// thread that resizes, and a fresh buffer's every page faults in that thread too: 50 MB took 20 ms
// so, against 3.4 ms when 16 threads write their own parts first (2026-10-28, WSL2, Ryzen 7 5800X).
// An unfilled_vector's resize leaves the values unwritten, so the tasks that fill it fault its pages
// in on every core — once: its blocks of a megabyte or more are kept for the next vector of their
// size (utils/kept_blocks.h), which finds them faulted in. It asks for no huge pages: 16 threads
// faulting 2 MB pages at once took 7-8 ms for the same 50 MB, and LLVM's token index
// (similarity/core.h) 59 ms on them against 47 ms on 4 KB pages — huge pages pay where one thread
// writes a buffer (utils/huge_pages.h). The values are integers and trivial classes —
// implicit-lifetime types, alive in their storage once allocated — and in a constant evaluation
// they are zeroed, as std::allocator does.
//
//     unfilled_vector<std::uint32_t> rank_at(tokens);       // room, unwritten
//     build.for_each(tasks, [&](std::size_t k) { ... rank_at[place] = rank; });

#include "kept_blocks.h"

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace pygim {

/// kept_allocator whose value-construction without arguments — a resize's — writes nothing at run
/// time; see the file comment.
template <class T>
class unfilled_allocator : public kept_allocator<T> {
public:
    using value_type = T;

    constexpr unfilled_allocator() noexcept = default;
    template <class U>
    constexpr unfilled_allocator(const unfilled_allocator<U>&) noexcept {}

    template <class U, class... Args>
    constexpr void construct(U* p, Args&&... args) {
        if constexpr (sizeof...(Args) == 0 && std::is_trivially_default_constructible_v<U>) {
            if consteval {
                std::construct_at(p);
            }
        } else {
            std::construct_at(p, std::forward<Args>(args)...);
        }
    }

    template <class U>
    [[nodiscard]] friend constexpr bool operator==(const unfilled_allocator&, const unfilled_allocator<U>&) noexcept { return true; }
};

/// A vector whose resize leaves its new values unwritten (see the file comment).
template <class T>
using unfilled_vector = std::vector<T, unfilled_allocator<T>>;

static_assert([] {
    unfilled_vector<int> v(3);
    v[0] = 1, v[1] = 2, v[2] = 3;
    const unfilled_vector<int> w{1, 2, 3};
    return v == w && v.size() == 3 && std::is_same_v<std::allocator_traits<unfilled_allocator<int>>::rebind_alloc<long>, unfilled_allocator<long>>;
}());

}  // namespace pygim
