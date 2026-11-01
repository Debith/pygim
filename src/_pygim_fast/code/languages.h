#pragma once
// code/languages.h — the languages pygim.code parses, and which of them one run parses (D93).
//
//     languages[0].name() -> "python"          languages[0].owns(".pyi") -> true
//     language_set::all().parses("ast.py")     -> true: a file of a language the set holds
//     language_set().parses("ast.py")          -> false: the empty set parses nothing
//
// One table: similarity's Python reads its suffixes from here (similarity/adapter/languages/
// python.h), so pygim.code and pygim.similarity cannot disagree on what a Python file is. A file
// belongs to a language by its suffix, as pathlib reads one (pathlike's suffix_of).
//
// CORE layer: pybind-free.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "../pathlike/path_rules.h"
#include "../utils/string_list.h"

namespace pygim::code {

/// A language pygim.code parses: its name, one sentence on how it is parsed, the suffixes it owns.
class language {
public:
    constexpr language(std::string_view name, std::string_view doc, string_list exts) noexcept : m_name(name), m_doc(doc), m_exts(exts) {}

    [[nodiscard]] constexpr std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] constexpr std::string_view doc() const noexcept { return m_doc; }
    [[nodiscard]] constexpr string_list exts() const noexcept { return m_exts; }
    [[nodiscard]] constexpr bool owns(std::string_view ext) const noexcept { return m_exts.contains(ext); }

private:
    std::string_view m_name;
    std::string_view m_doc;
    string_list m_exts;
};
static_assert(language("none", "", string_list()).name() == "none" && !language("none", "", string_list()).owns(".py"));

/// Python's suffixes: a module and a stub are both Python 3.14 to the parser.
inline constexpr std::array<std::string_view, 2> python_extensions{".py", ".pyi"};

/// Every language pygim.code parses, in the order code.LANGUAGES lists them.
inline constexpr std::array<language, 1> languages{
    language("python", "Python 3.14, through pygim.code's own parser in C++.", python_extensions),
};
static_assert(languages[0].owns(".py") && languages[0].owns(".pyi") && !languages[0].owns(".pyc"));

/// The place in `languages` of the one named `name`; nullopt for a name no language has.
[[nodiscard]] constexpr std::optional<std::size_t> language_named(std::string_view name) noexcept {
    for (std::size_t i = 0; i < languages.size(); ++i) {
        if (languages[i].name() == name) return i;
    }
    return std::nullopt;
}
static_assert(language_named("python") == 0 && !language_named("Python") && !language_named(""));

/// Which languages one run parses: a bit for each entry of `languages`.
class language_set {
public:
    constexpr language_set() noexcept = default;
    [[nodiscard]] static constexpr language_set all() noexcept { return language_set((std::uint32_t{1} << languages.size()) - 1); }

    constexpr void add(std::size_t i) noexcept { m_bits |= std::uint32_t{1} << i; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_bits == 0; }

    /// Whether a file named `file_name` is in a language of the set, by its suffix.
    [[nodiscard]] constexpr bool parses(std::string_view file_name) const noexcept {
        const std::string_view ext = pathlike::detail::suffix_of(file_name);
        for (std::size_t i = 0; i < languages.size(); ++i) {
            if ((m_bits >> i & 1u) != 0 && languages[i].owns(ext)) return true;
        }
        return false;
    }

private:
    explicit constexpr language_set(std::uint32_t bits) noexcept : m_bits(bits) {}

    std::uint32_t m_bits{0};
};
static_assert(languages.size() < 32);   // a bit each
static_assert(language_set::all().parses("ast.py") && language_set::all().parses("x.pyi") && !language_set::all().parses("x.txt") &&
              !language_set::all().parses(".py") && !language_set().parses("ast.py") && language_set().empty());

}  // namespace pygim::code
