#pragma once
//
// each.hpp — generic “map-over-range” helper
// Works with any C++20 std::ranges::input_range.
//
// Updated: members now use the m_ prefix.
//
#include <concepts>
#include <functional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace each {

//───────────────────────────────────────────────────────────────
//  Proxy — light wrapper that maps over every element
//───────────────────────────────────────────────────────────────
template <std::ranges::input_range Rng>
class Proxy {
    Rng m_range;                       // could be a view, vector, …

  public:
    explicit Proxy(Rng range) : m_range(std::move(range)) {}

    // ——— EAGER HELPERS ————————————————————————————————

    /// Call any invocable `f(elem)` and collect results.
    template <typename F>
    auto map(F&& f) const {
        using Res = std::invoke_result_t<F&, decltype(*std::begin(m_range))>;
        std::vector<Res> out;
        for (auto&& v : m_range) out.emplace_back(std::invoke(f, v));
        return out;
    }

    /// Invoke a member-function pointer on every element.
    template <typename MemFn, typename... Args>
        requires std::is_member_function_pointer_v<MemFn>
    auto call(MemFn mf, Args&&... args) const {
        using Res =
            std::invoke_result_t<MemFn, decltype(*std::begin(m_range)), Args...>;
        std::vector<Res> out;
        for (auto&& v : m_range)
            out.emplace_back(std::invoke(mf, v, std::forward<Args>(args)...));
        return out;
    }

    /// Collect a pointer-to-data-member from every element.
    template <typename MemPtr>
        requires std::is_member_object_pointer_v<MemPtr>
    auto get(MemPtr mp) const {
        using Res = std::remove_cvref_t<
            decltype(std::declval<decltype(*std::begin(m_range))>().*mp)>;
        std::vector<Res> out;
        for (auto&& v : m_range) out.emplace_back(v.*mp);
        return out;
    }

    // ——— LAZY HELPERS ————————————————————————————————————

    template <typename F>
    auto transform(F&& f) const {
        return m_range | std::views::transform(std::forward<F>(f));
    }

    template <typename MemFn, typename... Args>
        requires std::is_member_function_pointer_v<MemFn>
    auto transform_call(MemFn mf, Args&&... args) const {
        return m_range | std::views::transform(
                             [=](auto&& v) { return std::invoke(mf, v, args...); });
    }
};

// Factory so you can write `each::over(rng)` tersely.
template <std::ranges::input_range Rng>
auto over(Rng&& rng) {
    return Proxy<std::decay_t<Rng>>(std::forward<Rng>(rng));
}

}  // namespace each
