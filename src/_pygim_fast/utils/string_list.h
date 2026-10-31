#pragma once
// utils/string_list.h — a borrowed list of constexpr strings: what a descriptor names in a list,
// such as an engine's extensions and aliases (pathlike/core.h's engine_info) or a language's
// extensions (similarity/core.h's language_info).
//
//     static constexpr std::array<std::string_view, 2> exts{".json", ".jsonl"};
//     string_list l = exts;      l.contains(".jsonl") -> true      l.size() -> 2
//
// A minimal stand-in for std::span<const std::string_view>: MSVC's std::span in constant
// expressions has a history of defects (docs/design/pathlike_engine_registry.md, "Why
// string_list"); GCC's serves (checked 2026-10-30), and a Windows CI build with std::span in its
// place would tell whether MSVC's does now. It views the array it was made from, which must
// outlive it — a descriptor's static constexpr member does.
//
// CORE layer: pybind-free.

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace pygim {

class string_list {
public:
    constexpr string_list() = default;
    template <std::size_t N>
    constexpr string_list(const std::array<std::string_view, N>& items) noexcept  // implicit by design: `.exts = exts`
        : m_first(items.data()), m_count(N) {}

    [[nodiscard]] constexpr const std::string_view* begin() const noexcept { return m_first; }
    [[nodiscard]] constexpr const std::string_view* end() const noexcept { return m_first + m_count; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_count; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_count == 0; }
    [[nodiscard]] constexpr bool contains(std::string_view s) const noexcept {
        for (std::string_view x : *this) {
            if (x == s) return true;
        }
        return false;
    }

private:
    const std::string_view* m_first = nullptr;
    std::size_t m_count = 0;
};

namespace detail {
inline constexpr std::array<std::string_view, 2> example_exts{".json", ".jsonl"};
}  // namespace detail
static_assert(string_list(detail::example_exts).contains(".jsonl") && string_list(detail::example_exts).size() == 2 &&
              !string_list(detail::example_exts).contains(".yaml") && string_list().empty());   // an engine's exts, read by owns()

/// `words` with `sep` between each two: what a message lists.
///
///     joined(std::array<std::string_view, 3>{"a", "b", "c"}, ", ")   -> "a, b, c"
template <class Words>
[[nodiscard]] constexpr std::string joined(const Words& words, std::string_view sep) {
    std::string out;
    bool first = true;
    for (std::string_view w : words) {
        if (!first) out += sep;
        out += w;
        first = false;
    }
    return out;
}
static_assert(joined(std::array<std::string_view, 3>{"a", "b", "c"}, ", ") == "a, b, c" && joined(string_list(), " ").empty() &&
              joined(detail::example_exts, " ") == ".json .jsonl");

}  // namespace pygim
