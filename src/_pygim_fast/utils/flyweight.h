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
//
// The one store today is weak_slots<T> (utils/flyweight_adapter.h): owners are
// numbered from 1 as stores are made, so owner 0 can never be a live store,
// which is what lets 0 mean "never stamped". The one value type is basic_file
// (pathlike/core.h: interned() / set_interned()).
//
// A worked example, used in the comments below:
//
//     weak_slots<file> store;           // the third store made: owner() == 3
//     store.remember(70, obj);          // obj now carries token {3, 70}
//     file copy = obj_value;            // a copy of the value: also {3, 70}
//
//     token{}.stamped()                 -> false        ({0, 0xFFFFFFFF})
//     obj.interned().stamped()          -> true
//     obj.interned() == token{3, 70}    -> true
//     store.id_of(obj, obj_value)       -> 70           (slot 70's referent IS obj)
//     store.id_of(other, copy)          -> none         (slot 70's referent is not `other`)
//     another_store.id_of(obj, value)   -> none         (owner 3 is not that store)
//
// The token says where the value CAME from; only the store named in it can
// say whether the object asking is still the one it made.

#include <concepts>
#include <cstdint>

namespace pygim::flyweight {

/// Which store made an object, and which row of it: {owner, slot}. Two
/// words, trivially copyable, constexpr — a value carries it as a member and
/// never reads it itself.
struct token {
    /// The store's number, unique for the process (weak_slots::owner()).   0 = never stamped
    std::uint64_t owner = 0;               // 0 = never stamped
    /// The row in that store; weak_slots::none (all ones) until stamped.
    std::uint32_t slot = 0xFFFF'FFFFu;

    /// Whether some store has stamped this value: owner != 0. Says nothing
    /// about whether that store still exists or still holds the same object
    /// in the slot — for that, ask the store (weak_slots::id_of).
    ///
    ///     token{}.stamped()            // false
    ///     token{3, 70}.stamped()       // true
    [[nodiscard]] constexpr bool stamped() const noexcept { return owner != 0; }
    /// Member-wise equality: same owner and same slot. This is how a store
    /// recognises its own token (t.owner == m_owner in id_of) and how a test
    /// checks a stamp landed; it is NOT the value's equality — two files with
    /// the same value from different stores compare equal as files and
    /// unequal as tokens.
    ///
    ///     token{3, 70} == token{3, 70}   // true
    ///     token{3, 70} == token{4, 70}   // false
    [[nodiscard]] constexpr bool operator==(const token&) const noexcept = default;
};

// ── what a store needs from a value ───────────────────────────────────────

/// A value type a store can stamp: `interned()` on a const value returns its
/// token (exactly `token`, not something convertible), and `set_interned(k)`
/// on a mutable one replaces it. weak_slots<T> requires this of T: remember()
/// calls set_interned({owner, id}) on the object it hands out, and id_of()
/// reads interned() back. basic_file (pathlike/core.h) models it with a
/// single `token m_intern{}` member that starts unstamped.
///
/// A token may be INTERPRETED in one place only, weak_slots::id_of
/// (utils/flyweight_adapter.h). A value copied out of a stamped object keeps
/// the stamp, and the store's slot may since have been refilled with a new
/// object for the same row, so the token alone proves nothing: id_of checks
/// that the owner is this store, the slot is in range, and the slot's weak
/// referent IS the asking object — then, and only then, the slot is the id.
template <class T>
concept stamped = requires(T t, const T ct, token k) {
    { ct.interned() } -> std::same_as<token>;
    t.set_interned(k);
};

}  // namespace pygim::flyweight
