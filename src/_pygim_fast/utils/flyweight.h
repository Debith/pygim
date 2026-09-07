#pragma once
// utils/flyweight.h — the identity token a flyweight store stamps on a value.
//
// CORE layer: pybind-free. A store that hands out ONE object per interned
// value marks each object it made with (owner, slot): which store, which row.
// The token is not part of the value, its equality or its hash; a copy of the
// value carries it along, so a reader must validate it against the owner
// (utils/flyweight_adapter.h: the slot's referent must be the asking object)
// before trusting it. Kept as a plain struct so a literal value type
// (pathlike's basic_file) can carry it through constant evaluation.

#include <concepts>
#include <cstdint>

namespace pygim::flyweight {

struct token {
    std::uint64_t owner = 0;               // 0 = never stamped
    std::uint32_t slot = 0xFFFF'FFFFu;

    [[nodiscard]] constexpr bool stamped() const noexcept { return owner != 0; }
    [[nodiscard]] constexpr bool operator==(const token&) const noexcept = default;
};

// A value type a store can stamp.
template <class T>
concept stamped = requires(T t, const T ct, token k) {
    { ct.interned() } -> std::same_as<token>;
    t.set_interned(k);
};

}  // namespace pygim::flyweight
