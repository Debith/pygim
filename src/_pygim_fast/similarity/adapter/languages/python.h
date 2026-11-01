#pragma once
// similarity/adapter/languages/python.h — the Python language.
//
// One file IS one language: the build discovers this directory into the registry
// (ext.similarity.toml, [extension.typelist]). The frontend itself is pybind-free and lives in
// the core (../../python_source.h): pygim.code's C++ parser, then the trees the ast frontend
// emitted, rule for rule. This descriptor only names it and forwards.

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "../../../code/languages.h"
#include "../../core.h"
#include "../../python_source.h"

namespace pygim::similarity::languages {

struct python {
    static constexpr const auto& exts = pygim::code::python_extensions;   // pygim.code's: one table of what a Python file is (D93)
    static constexpr language_info info{
        "python",
        "Python 3.14, through pygim.code's own parser in C++; imports resolved to qualified names.",
        exts,
    };

    /// The units `text` defines (a file's bytes: a coding cookie is honoured). A source that is
    /// not Python 3.14 is a parse_error naming the origin and the line. `ctx.module` /
    /// `ctx.package` qualify module-level names and resolve relative imports. Touches no Python
    /// object; each thread keeps its parser's buffers between files.
    [[nodiscard]] static parsed_source<python> parse(std::string_view text, const source_context& ctx) { return parse(arena_string(text), ctx); }
    /// The same, the bytes handed over: a file read for it becomes the tree's text, uncopied.
    [[nodiscard]] static parsed_source<python> parse(arena_string&& bytes, const source_context& ctx) {
        thread_local python_source::workspace work;
        return parsed_source<python>{python_source::parse_python(std::move(bytes), ctx, work)};
    }
};

}  // namespace pygim::similarity::languages
