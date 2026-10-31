#pragma once
// utils/pieces.h — how many pieces of at most `per` items cover `items`: tasks, blocks, batches.
//
// CORE layer: pybind-free. Written as (items + per - 1) / per it overflows for a `per` near the
// type's top — a batch_size of 2**64 - 1 from Python made pairs() of three pairs return no batch at
// all (2026-10-29). The quotient and a remainder never do.

#include <cstddef>
#include <limits>

namespace pygim {

/// ⌈items / per⌉ for per ≥ 1, without the overflow of (items + per - 1) / per.
[[nodiscard]] constexpr std::size_t pieces(std::size_t items, std::size_t per) noexcept {
    return items / per + (items % per != 0);
}
static_assert(pieces(0, 4) == 0 && pieces(8, 4) == 2 && pieces(9, 4) == 3 && pieces(3, std::numeric_limits<std::size_t>::max()) == 1);

}  // namespace pygim
