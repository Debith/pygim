#pragma once
// core/primitives/scope_exit.h — a call made when the scope it was set up in ends, however it ends:
// returned, thrown out of or broken out of. std::scope_exit (Library Fundamentals TS v3) has not
// reached the standard; this is the part of it pygim uses.
//
//     const on_scope_exit returned([&] { busy.store(false); });   // whatever follows does
//
// The call runs in a destructor, so it must not throw.
//
// CORE layer: pybind-free.

#include <concepts>
#include <type_traits>
#include <utility>

namespace pygim::primitives {

/// Calls f() when it goes out of scope.
template <std::invocable F>
class [[nodiscard]] on_scope_exit {
public:
    constexpr explicit on_scope_exit(F f) noexcept(std::is_nothrow_move_constructible_v<F>) : m_f(std::move(f)) {}
    constexpr ~on_scope_exit() { m_f(); }
    on_scope_exit(const on_scope_exit&) = delete;
    on_scope_exit& operator=(const on_scope_exit&) = delete;

private:
    F m_f;
};

static_assert([] {
    int calls = 0;
    for (int i = 0; i < 3; ++i) {
        const on_scope_exit counted([&] { ++calls; });
        if (i == 1) continue;   // left early: called all the same
    }
    return calls == 3;
}());

}  // namespace pygim::primitives
