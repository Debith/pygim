#pragma once
// utils/hooks.h — callbacks per named event; or, with the same API, none at all.
//
// An EVENT is a tag type naming a moment and what its callbacks receive:
//
//     struct lap_stopped { using signature = void(std::string_view timer, std::string_view lap, duration took); };
//
// hooks<Events...> keeps any number of callbacks per event and runs them in the order they were
// added; no_hooks<Events...> has the same API and compiles to nothing, so its owner stays a
// literal type, usable in constant evaluation. An owner takes either as a policy (the registry's
// HooksBundle and NoHooks, QuickTimer's timer_hooks and silent report).
//
//     hooks<lap_stopped> h;
//     h.add<lap_stopped>([](std::string_view t, std::string_view lap, duration took) { ... });
//     h.run<lap_stopped>("job", "read", 2ms);     // every callback, in order
//
// An event not in the list is a compile error, so a registry's event cannot reach a timer.
// CORE layer: pybind-free.

#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace pygim {

/// A moment callbacks can be attached to: a type naming it, with the signature they receive.
template <class E>
concept event = requires { typename E::signature; };

/// `E` is one of `Events`.
template <class E, class... Events>
concept one_of = (std::is_same_v<E, Events> || ...);

namespace detail {

template <class E, class First, class... Rest>
[[nodiscard]] consteval std::size_t index_of() {
    if constexpr (std::is_same_v<E, First>) return 0;
    else return 1 + index_of<E, Rest...>();
}
static_assert(index_of<long, int, long, char>() == 1 && index_of<int, int>() == 0);

}  // namespace detail

/// Any number of callbacks per event, run in the order they were added.
template <event... Events>
class hooks {
public:
    /// Adds `callback` to the callbacks of `E`.
    template <one_of<Events...> E, class F>
    void add(F&& callback) {
        callbacks_of<E>().emplace_back(std::forward<F>(callback));
    }

    /// Runs every callback of `E` with `args`, in the order they were added. A callback may add
    /// callbacks: they run from the next event on — the loop counts what was there when it began,
    /// by index, so a vector the add reallocates is never read through a stale iterator.
    template <one_of<Events...> E, class... Args>
    void run(Args&&... args) const {
        const auto& callbacks = callbacks_of<E>();
        for (std::size_t i = 0, n = callbacks.size(); i < n; ++i) callbacks[i](args...);
    }

    /// Whether anything listens to `E`.
    template <one_of<Events...> E>
    [[nodiscard]] bool has() const noexcept {
        return !callbacks_of<E>().empty();
    }

private:
    std::tuple<std::vector<std::function<typename Events::signature>>...> m_callbacks;

    template <class E>
    [[nodiscard]] auto& callbacks_of() noexcept { return std::get<detail::index_of<E, Events...>()>(m_callbacks); }
    template <class E>
    [[nodiscard]] const auto& callbacks_of() const noexcept { return std::get<detail::index_of<E, Events...>()>(m_callbacks); }
};

/// The same API as hooks<Events...>, compiling to nothing: what an owner takes when it is not observed.
template <event... Events>
class no_hooks {
public:
    template <one_of<Events...> E, class F>
    constexpr void add(F&&) const noexcept {}

    template <one_of<Events...> E, class... Args>
    constexpr void run(Args&&...) const noexcept {}

    template <one_of<Events...> E>
    [[nodiscard]] constexpr bool has() const noexcept { return false; }
};

namespace detail {
struct proof_event { using signature = void(int); };
struct other_event { using signature = void(int); };
}  // namespace detail

static_assert(event<detail::proof_event> && one_of<detail::proof_event, detail::other_event, detail::proof_event>);
static_assert(!one_of<detail::proof_event, detail::other_event>);
static_assert(!no_hooks<detail::proof_event>{}.has<detail::proof_event>());
static_assert(std::is_empty_v<no_hooks<detail::proof_event>>);

}  // namespace pygim
