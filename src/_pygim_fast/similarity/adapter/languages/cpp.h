#pragma once
// similarity/adapter/languages/cpp.h — the C++ language.
//
// One file IS one language: the build discovers this directory into the
// registry (ext.similarity.toml, [extension.typelist]). The frontend itself
// is pybind-free and lives in the core (../../cpp_source.h), where its laws
// are proven; this descriptor only names it and forwards.

#include <array>
#include <string_view>

#include "../../core.h"
#include "../../cpp_source.h"

namespace pygim::similarity::languages {

struct cpp {
    static constexpr std::array<std::string_view, 12> exts{
        ".c", ".cc", ".cpp", ".cxx", ".c++", ".h", ".hh", ".hpp", ".hxx", ".h++", ".ipp", ".inl",
    };
    static constexpr language_info info{
        "cpp",
        "C and C++, through pygim's own tokenizer and bracket-structure reader (no compiler involved).",
        exts,
    };

    /// The units `text` defines. C++ has no module path: only `ctx.origin`
    /// is used. Touches no Python object.
    [[nodiscard]] static parsed_source<cpp> parse(std::string_view text, const source_context& ctx) {
        return parsed_source<cpp>{similarity::cpp::parse_cpp(text, ctx.origin())};
    }
};

}  // namespace pygim::similarity::languages
