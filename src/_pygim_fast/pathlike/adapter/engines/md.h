#pragma once
// pathlike/adapter/engines/md.h — the markdown engine: `.md` reads into a markdown.Document.
//
// One file IS one engine: the descriptor below is what the build discovers
// into the registry (../../engine_list.h). Unlike the data formats, markdown
// is a document, not a value: read() returns `pathlike.markdown.Document`
// (adapter/markdown.h over the pybind-free core in ../../markdown/) — the
// exact text with its block tree, lines and spans — rather than dicts and
// lists, and write() takes that Document (or a str) and writes its text, so
// a read and a write leave the file byte for byte as it was. The engine
// also binds the `markdown` submodule (its optional `bind` hook), where the
// Document type and the builders live. Front matter is read and written by
// the YAML and TOML engines (their text half, adapter.h TextEngine).
//
// Lifetime and threading: a read parses with the GIL released (the bytes and
// the parse are owned C++ values) and returns a Document that owns its text; a
// write copies the text out of the Document or str first, then writes with the
// GIL released. Nothing here is shared between calls.
//
//     pygim.path("notes.md").read()             -> Document(gfm, 11 blocks, 29 lines, notes.md)
//     pygim.path("notes.md").write(doc)         -> the document's text, unchanged
//     pygim.path("notes.md").write("# T\n")     -> that text

#include <array>
#include <string>
#include <string_view>
#include <variant>

#include <pybind11/pybind11.h>

#include "../../core.h"
#include "../common.h"
#include "../markdown.h"
#include "../materialize.h"

namespace pygim::pathlike::detail {

[[nodiscard]] inline py::object load_markdown(const file& f) {
    std::string bytes;
    {
        py::gil_scoped_release nogil;
        bytes = f.read_bytes();
        require_utf8(bytes, f.fspath());
    }
    return py::cast(markdown_py::document_ref{markdown_py::parse(std::move(bytes), "gfm", "github", true, f.fspath())});
}

/// What a .md file is written from, and the text each kind writes.
using md_content = std::variant<markdown_py::document_ref, py::str>;
struct md_text {
    std::string operator()(const markdown_py::document_ref& d) const { return d.doc->core().source(); }
    std::string operator()(const py::str& s) const { return markdown_py::utf8(s, "md write"); }
};

inline void write_markdown(const file& f, py::handle obj) {
    md_content content;
    try {
        content = obj.cast<md_content>();
    } catch (const py::cast_error&) {
        throw py::type_error("md write: content must be a markdown Document or str, got " +
                             py::str(py::type::of(obj).attr("__name__")).cast<std::string>());
    }
    const std::string text = std::visit(md_text{}, content);
    py::gil_scoped_release nogil;
    write_text_file(f, text);
}

}  // namespace pygim::pathlike::detail

// ── Registry entry ─────────────────────────────────────────────────────────
namespace pygim::pathlike::engines {

struct md {
    static constexpr std::array<std::string_view, 2> exts{".md", ".markdown"};
    static constexpr std::array<std::string_view, 1> aliases{"markdown"};
    static constexpr engine_info info{
        .name = "md",
        .label = "pygim-md",
        .doc = "Markdown (CommonMark 0.31.2 with GFM tables, strikethrough and task items) via pygim's own "
               "parser: read() returns a markdown.Document (blocks with lines and spans, sections, front matter, "
               "HTML) and write() takes a Document or str, so the file round-trips byte for byte.",
        .exts = exts,
        .aliases = aliases,
    };

    static py::object load(const file& f, detail::KeyCache&) { return detail::load_markdown(f); }
    static void write(const file& f, py::handle obj) { detail::write_markdown(f, obj); }
    /// The Document type and the builders, as `pathlike.markdown` (adapter.h bind_extras).
    static void bind(py::module_& m) { markdown_py::bind(m); }
};

}  // namespace pygim::pathlike::engines
