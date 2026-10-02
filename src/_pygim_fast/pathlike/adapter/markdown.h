#pragma once
// pathlike/adapter/markdown.h — `pygim.pathlike.markdown`: Document, the block classes, Section, the builders.
//
// The core (../markdown/) is pybind-free and does the work — including
// choosing the dialect and slug rule by name (any_document.h). This file only
// turns it into Python objects, and what varies is chosen by type, never by a
// chain of comparisons:
//
//   kinds     each block kind is a descriptor in a pack; one fold binds a
//             Python class per kind (Heading, Code, Table, ...) that holds
//             only its kind's properties, and a table indexed by the kind
//             wraps a block in its class — no property asks which kind it is.
//   formats   front matter formats (YAML `---`, TOML `+++`) are a pack over
//             the engines: reading picks one by the fence's marker, writing by name.
//   targets   `replace` takes a Block or a Section as a std::variant and
//             visits an overload set: each says what bytes it covers.
//
// Example (Python):
//
//     doc = pygim.path("notes.md").read()          # a Document; parsed with the GIL released
//     h = doc.find(markdown.Heading)[1]             # a Heading: .level .title .slug .content
//     h.slug, h.span                                # ('usage', (41, 50)): characters into doc.text
//     doc.replace(doc.section("usage"), "## Usage\n\nNew.\n")   # a new Document; the rest unchanged
//
// Lifetime and threading: a Document is immutable and shared — every Block
// and Section holds the same shared pointer — so a view never dangles. The
// core's lazily filled caches fill while the GIL is held. Probes for tests and
// benchmarks exist only in a build with the opt-in flag PYGIM_MARKDOWN_PROBES=1
// (setup.py); a release exports none.

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "../../wiring/registry/core.h"
#include "../markdown/any_document.h"
#include "../markdown/writer.h"
#include "common.h"
#include "engines/toml.h"
#include "engines/yaml.h"
#include "materialize.h"

#ifndef PYGIM_MARKDOWN_PROBES
#define PYGIM_MARKDOWN_PROBES 0   // opt-in build flag (environment-independent default): see `probes`
#endif

namespace pygim::pathlike::markdown_py {

namespace py = pybind11;
namespace mk = pygim::pathlike::markdown;

/// Whether this build binds the test and benchmark probes (`_stops`, `_scan`).
inline constexpr bool probes = PYGIM_MARKDOWN_PROBES != 0;

using mk::names_of;
using mk::position;
using mk::type_list;

[[nodiscard]] inline py::str str_of(std::string_view s) { return py::str(s.data(), s.size()); }

/// What Python's Document, Block and Section share: the parsed document and
/// the name errors give it (the file, or "<string>").
struct document {
    mk::any_document doc;
    std::string origin;

    [[nodiscard]] const mk::document_core& core() const noexcept { return doc.core(); }
    [[nodiscard]] const mk::block& at(std::uint32_t i) const { return core().at(i); }
    template <class F>
    decltype(auto) visit(F&& f) const {
        return doc.visit(std::forward<F>(f));
    }
    [[nodiscard]] std::string plain(std::uint32_t i) const { return visit([&](const auto& d) { return d.plain(i); }); }
    [[nodiscard]] const std::vector<mk::heading_info>& headings() const {
        return visit([](const auto& d) -> const std::vector<mk::heading_info>& { return d.headings(); });
    }
    [[nodiscard]] const std::vector<mk::section>& sections() const {
        return visit([](const auto& d) -> const std::vector<mk::section>& { return d.sections(); });
    }
    [[nodiscard]] const mk::heading_info& heading(std::uint32_t i) const {
        return visit([&](const auto& d) -> const mk::heading_info& { return d.heading(i); });
    }
    [[nodiscard]] std::string cell(std::uint32_t t, std::uint32_t r, std::uint32_t c) const {
        return visit([&](const auto& d) { return d.cell_plain(t, r, c); });
    }
};
using doc_ptr = std::shared_ptr<const document>;

/// Parses with the GIL released; ValueError for a dialect or slug rule that does not exist.
[[nodiscard]] inline doc_ptr parse(std::string source, std::string_view dialect, std::string_view slugs, bool front_matter,
                                   std::string origin) {
    std::optional<mk::any_document> doc;
    {
        py::gil_scoped_release nogil;
        doc.emplace(mk::any_document::parse(std::move(source), dialect, slugs, front_matter));
    }
    return std::make_shared<const document>(document{std::move(*doc), std::move(origin)});
}
/// An edit's result: the same policies over new text.
[[nodiscard]] inline doc_ptr reparse(const document& d, std::string source) {
    std::optional<mk::any_document> next;
    {
        py::gil_scoped_release nogil;
        next.emplace(d.doc.reparse(std::move(source)));
    }
    return std::make_shared<const document>(document{std::move(*next), d.origin});
}

// ── front matter formats ────────────────────────────────────────────────────

template <class E, char Marker>
struct front_matter_format {
    using engine = E;
    static constexpr char marker = Marker;
    static constexpr std::string_view name = E::info.name;
};
using front_matter_formats = type_list<front_matter_format<engines::yaml, '-'>, front_matter_format<engines::toml, '+'>>;

// Plain function templates folded over the pack (the construct set every
// compiler in the matrix handles, as adapter.h's load_if/write_if).
template <class F>
bool load_if(char marker, std::string_view body, std::string_view origin, py::object& out) {
    if (F::marker != marker) return false;
    detail::KeyCache keys(256);
    out = F::engine::loads(body, origin, keys);
    return true;
}
template <class F>
bool dump_if(std::string_view name, py::handle data, std::string& out) {
    if (F::name != name) return false;
    out = mk::write::front_matter(F::engine::dumps(data), F::marker);
    return true;
}
template <class F>
bool name_if(char marker, std::string_view& out) {
    if (F::marker != marker) return false;
    out = F::name;
    return true;
}
template <class... Fs>
py::object load_front_matter(type_list<Fs...>, char marker, std::string_view body, std::string_view origin) {
    py::object out = py::none();
    (load_if<Fs>(marker, body, origin, out) || ...);
    return out;
}
template <class... Fs>
std::string dump_front_matter(type_list<Fs...> formats, py::handle data, std::string_view name) {
    position(names_of(formats), name, "front matter engine");
    std::string out;
    (dump_if<Fs>(name, data, out) || ...);
    return out;
}
template <class... Fs>
std::string_view format_named_by(type_list<Fs...>, char marker) {
    std::string_view out;
    (name_if<Fs>(marker, out) || ...);
    return out;
}

// ── the Python objects ──────────────────────────────────────────────────────

struct document_ref {
    doc_ptr doc;
};
/// A block: a view (the document and an index), never a copy.
struct block_ref {
    doc_ptr doc;
    std::uint32_t index = 0;
    [[nodiscard]] const mk::block& at() const { return doc->at(index); }
};
/// A block of kind K: what the class K binds receives.
template <class K>
struct typed_block : block_ref {};
struct section_ref {
    doc_ptr doc;
    std::uint32_t index = 0;
    [[nodiscard]] const mk::section& at() const { return doc->sections()[index]; }
};

[[nodiscard]] inline std::string_view align_name(mk::align a) {
    static constexpr std::array<std::string_view, 4> names{"", "left", "center", "right"};   // indexed by mk::align
    return names[static_cast<std::size_t>(a)];
}
[[nodiscard]] inline mk::align align_from(py::handle a) {
    static constexpr std::array<std::string_view, 3> names{"left", "center", "right"};
    if (a.is_none()) return mk::align::none;
    return static_cast<mk::align>(1 + position(names, py::str(a).cast<std::string>(), "table align"));
}
[[nodiscard]] inline py::object none_if_empty(std::string_view s) {
    return s.empty() ? py::object(py::none()) : py::object(str_of(s));
}
[[nodiscard]] inline std::string first_word(std::string_view s) {
    return std::string(s.substr(0, std::min(s.find_first_of(" \t\n\v\f\r"), s.size())));
}

// ── one descriptor per block kind ───────────────────────────────────────────
// `tag` is the kind, `doc` the class docstring, `bind` adds the properties
// only that kind has. The Python class is named after the kind (heading ->
// Heading); bind_kinds() folds over the pack; wrap() picks the class by kind.

namespace kinds {

struct front_matter {
    static constexpr mk::kind tag = mk::kind::front_matter;
    static constexpr const char* doc = "Front matter: a ``---`` (YAML) or ``+++`` (TOML) block at the very start. "
                                       "Document.front_matter has its data.";
    template <class C>
    static void bind(C& c) {
        c.def_property_readonly("raw", [](const block_ref& b) { return str_of(b.doc->core().raw(b.index)); }, "The text between the fences.")
         .def_property_readonly("engine", [](const block_ref& b) { return str_of(format_named_by(front_matter_formats{}, b.at().marker)); },
                                "The engine its fence names: 'yaml' (---) or 'toml' (+++).");
    }
};
struct heading {
    static constexpr mk::kind tag = mk::kind::heading;
    static constexpr const char* doc = "An ATX (``# Title``) or setext (underlined) heading.";
    template <class C>
    static void bind(C& c) {
        c.def_property_readonly("level", [](const block_ref& b) { return b.at().level; }, "1-6.")
         .def_property_readonly("title", [](const block_ref& b) { return str_of(b.doc->heading(b.index).title); },
                                "Its text, inline markup resolved, line breaks as spaces.")
         .def_property_readonly("slug", [](const block_ref& b) { return str_of(b.doc->heading(b.index).slug); },
                                "Its anchor, unique in the document (the document's slug rule).")
         .def_property_readonly("content", [](const block_ref& b) { return str_of(b.doc->core().inline_source(b.index)); },
                                "Its inline markdown, without the ``#`` marks or the underline.");
    }
};
struct paragraph {
    static constexpr mk::kind tag = mk::kind::paragraph;
    static constexpr const char* doc = "A paragraph.";
    template <class C>
    static void bind(C& c) {
        c.def_property_readonly("content", [](const block_ref& b) { return str_of(b.doc->core().inline_source(b.index)); },
                                "Its inline markdown, lines joined by newlines.");
    }
};
struct code {
    static constexpr mk::kind tag = mk::kind::code;
    static constexpr const char* doc = "A fenced or indented code block.";
    template <class C>
    static void bind(C& c) {
        c.def_property_readonly("fenced", [](const block_ref& b) { return b.at().fenced; })
         .def_property_readonly("info", [](const block_ref& b) { return none_if_empty(b.doc->core().info(b.index)); },
                                "A fenced block's info string ('py title=\"x\"'), unescaped; None when there is none.")
         .def_property_readonly("lang", [](const block_ref& b) { return none_if_empty(first_word(b.doc->core().info(b.index))); },
                                "The info string's first word, its language; None when there is none.")
         .def_property_readonly("code", [](const block_ref& b) {
             return str_of(b.doc->core().code(b.index));
         }, "The code, each line ended by a newline.");
    }
};
struct html {
    static constexpr mk::kind tag = mk::kind::html;
    static constexpr const char* doc = "An HTML block.";
    template <class C>
    static void bind(C& c) {
        c.def_property_readonly("raw", [](const block_ref& b) { return str_of(b.doc->core().raw(b.index)); }, "Its HTML, as written.");
    }
};
struct thematic_break {
    static constexpr mk::kind tag = mk::kind::thematic_break;
    static constexpr const char* doc = "A thematic break (``---``, ``***``, ``___``).";
    template <class C>
    static void bind(C&) {}
};
struct quote {
    static constexpr mk::kind tag = mk::kind::quote;
    static constexpr const char* doc = "A block quote; its blocks are its children.";
    template <class C>
    static void bind(C&) {}
};
struct list {
    static constexpr mk::kind tag = mk::kind::list;
    static constexpr const char* doc = "A bullet or ordered list; its items are its children.";
    template <class C>
    static void bind(C& c) {
        c.def_property_readonly("ordered", [](const block_ref& b) { return b.at().ordered; })
         .def_property_readonly("start", [](const block_ref& b) -> py::object {
             return b.at().ordered ? py::object(py::int_(b.at().start)) : py::object(py::none());
         }, "An ordered list's first number; None for a bullet list.")
         .def_property_readonly("tight", [](const block_ref& b) { return b.at().tight; },
                                "Whether no blank line separates its items or the blocks inside them.");
    }
};
struct item {
    static constexpr mk::kind tag = mk::kind::item;
    static constexpr const char* doc = "A list item; its blocks are its children.";
    template <class C>
    static void bind(C& c) {
        c.def_property_readonly("checked", [](const block_ref& b) -> py::object {
            const std::int8_t t = b.at().task;
            return t < 0 ? py::object(py::none()) : py::object(py::bool_(t == 1));
        }, "A task item's state ([x] True, [ ] False); None when it is not a task.");
    }
};
struct table {
    static constexpr mk::kind tag = mk::kind::table;
    static constexpr const char* doc = "A GFM table.";
    template <class C>
    static void bind(C& c) {
        c.def_property_readonly("header", [](const block_ref& b) { return row(b, 0); }, "The header cells, as plain text.")
         .def_property_readonly("rows", [](const block_ref& b) {
             py::list out;
             for (std::uint32_t r = 1; r < b.at().rows; ++r) out.append(row(b, r));
             return out;
         }, "The body rows, as plain text, each as wide as the header.")
         .def_property_readonly("align", [](const block_ref& b) {
             py::list out;
             for (std::uint32_t c = 0; c < b.at().columns; ++c) {
                 out.append(none_if_empty(align_name(b.doc->core().structure().aligns[b.at().align + c])));
             }
             return out;
         }, "Each column's alignment: 'left', 'center', 'right' or None.");
    }
    static py::list row(const block_ref& b, std::uint32_t r) {
        py::list out;
        for (std::uint32_t c = 0; c < b.at().columns; ++c) out.append(str_of(b.doc->cell(b.index, r, c)));
        return out;
    }
};
struct definition {
    static constexpr mk::kind tag = mk::kind::definition;
    static constexpr const char* doc = "A link reference definition: ``[label]: /url \"title\"``.";
    template <class C>
    static void bind(C& c) {
        c.def_property_readonly("label", [](const block_ref& b) { return str_of(def(b).label); }, "Case-folded, as references match it.")
         .def_property_readonly("destination", [](const block_ref& b) { return str_of(def(b).destination); })
         .def_property_readonly("title", [](const block_ref& b) { return str_of(def(b).title); });
    }
    static const mk::definition& def(const block_ref& b) { return b.doc->core().structure().definitions[b.at().def]; }
};

}  // namespace kinds

using block_kinds = type_list<kinds::front_matter, kinds::heading, kinds::paragraph, kinds::code, kinds::html,
                              kinds::thematic_break, kinds::quote, kinds::list, kinds::item, kinds::table,
                              kinds::definition>;

/// Every kind but the document has exactly one class: the pack is complete.
template <class... Ks>
consteval bool covers_every_kind(type_list<Ks...>) {
    std::array<int, mk::kind_count> seen{};
    ((++seen[static_cast<std::size_t>(Ks::tag)]), ...);
    if (seen[0] != 0) return false;
    for (std::size_t k = 1; k < mk::kind_count; ++k) {
        if (seen[k] != 1) return false;
    }
    return true;
}
static_assert(covers_every_kind(block_kinds{}), "every block kind needs exactly one descriptor in block_kinds");

/// "front_matter" -> "FrontMatter", as a NUL-terminated buffer with static storage.
template <class K>
inline constexpr auto class_name = [] {
    constexpr std::string_view n = mk::kind_name(K::tag);
    std::array<char, n.size() + 1> out{};
    std::size_t j = 0;
    bool upper = true;
    for (char c : n) {
        if (c == '_') {
            upper = true;
            continue;
        }
        out[j++] = upper ? static_cast<char>(c - 'a' + 'A') : c;
        upper = false;
    }
    return out;
}();

template <class K>
py::object wrap_as(const block_ref& b) {
    return py::cast(typed_block<K>{b});
}
using wrapper = py::object (*)(const block_ref&);
template <class... Ks>
constexpr std::array<wrapper, mk::kind_count> wrappers(type_list<Ks...>) {
    std::array<wrapper, mk::kind_count> out{};
    ((out[static_cast<std::size_t>(Ks::tag)] = &wrap_as<Ks>), ...);
    return out;
}
/// A block as the Python object of its kind's class: one table lookup.
[[nodiscard]] inline py::object wrap(const block_ref& b) {
    static constexpr auto table = wrappers(block_kinds{});
    return table[static_cast<std::size_t>(b.at().type)](b);
}

/// The Python class of each kind -> the kind, for find(cls).
inline ::pygim::core::DynamicRegistryCore<PyTypeObject*, mk::kind>& class_kinds() {
    static auto* registry = new ::pygim::core::DynamicRegistryCore<PyTypeObject*, mk::kind>();
    return *registry;
}

template <class K>
void bind_kind(py::module_& m) {
    py::class_<typed_block<K>, block_ref> cls(m, class_name<K>.data(), K::doc);
    K::bind(cls);
    class_kinds().register_value(reinterpret_cast<PyTypeObject*>(cls.ptr()), K::tag);
}
template <class... Ks>
void bind_kinds(type_list<Ks...>, py::module_& m) {
    (bind_kind<Ks>(m), ...);
}

// ── helpers of the bindings ─────────────────────────────────────────────────

[[nodiscard]] inline py::list wrapped(const doc_ptr& d, const std::vector<std::uint32_t>& blocks) {
    py::list out;
    for (std::uint32_t i : blocks) out.append(wrap(block_ref{d, i}));
    return out;
}
[[nodiscard]] inline py::tuple span(const document& d, std::uint32_t begin, std::uint32_t end) {
    return py::make_tuple(d.core().code_points(begin), d.core().code_points(end));
}
[[nodiscard]] inline py::str text(const document& d, std::uint32_t begin, std::uint32_t end) {
    return str_of(std::string_view(d.core().source()).substr(begin, end - begin));
}

/// What an edit replaces, by what it was given: the overload set `replace` visits.
inline void same_document(const doc_ptr& a, const doc_ptr& b, std::string_view what) {
    if (a != b) throw std::invalid_argument("replace: that " + std::string(what) + " belongs to another document");
}
[[nodiscard]] inline std::pair<std::uint32_t, std::uint32_t> region(const doc_ptr& d, const block_ref& b) {
    same_document(d, b.doc, "block");
    return d->core().region(b.index);
}
[[nodiscard]] inline std::pair<std::uint32_t, std::uint32_t> region(const doc_ptr& d, const section_ref& s) {
    same_document(d, s.doc, "section");
    return d->visit([&](const auto& x) { return x.section_region(s.index); });
}
using edit_target = std::variant<block_ref, section_ref>;

[[nodiscard]] inline std::vector<std::string> strings_of(py::handle seq) {
    std::vector<std::string> out;
    for (py::handle x : seq) out.push_back(py::str(x).cast<std::string>());
    return out;
}

/// Test and benchmark probes, bound only in a PYGIM_MARKDOWN_PROBES=1 build:
/// the stops a scan policy finds (or with count=True only how many), and the
/// name of the policy this build scans with.
inline void bind_probes(py::module_& m) {
    m.def("_stops", [](const std::string& text, const std::string& scan, bool count) -> py::object {
        static constexpr std::array<std::string_view, 2> scans{"scalar", "simd"};
        const bool simd = position(scans, scan, "scan") == 1;
        std::vector<std::size_t> out;
        std::size_t n = 0;
        const auto collect = [&](const auto& ix) {
            for (std::size_t i = ix.next(0); i != mk::stop_index::npos; i = ix.next(i + 1)) {
                ++n;
                if (!count) out.push_back(i);
            }
        };
        {
            py::gil_scoped_release nogil;
            if (simd) collect(mk::basic_stop_index<mk::default_scan>(text));
            else collect(mk::basic_stop_index<mk::scalar_scan>(text));
        }
        return count ? py::object(py::int_(n)) : py::object(py::cast(out));
    }, py::arg("text"), py::arg("scan") = "simd", py::arg("count") = false);
    m.attr("_scan") = std::string(mk::default_scan::name);
}

/// Binds the `markdown` submodule of `pathlike`.
inline void bind(py::module_& parent) {
    py::module_ m = parent.def_submodule(
        "markdown",
        "Markdown documents: parse (CommonMark 0.31.2, GFM tables/strikethrough/task items, front matter), "
        "query (blocks of their own classes, sections, headings), edit losslessly, render HTML, and build markdown text.");

    py::class_<block_ref> block_cls(m, "Block",
        "One block of a Document: a view (the document and an index), never a copy. Its class is its kind "
        "(Heading, Code, Table, ...), and holds the properties only that kind has.");
    py::class_<section_ref> section_cls(m, "Section",
        "A top-level heading and everything up to the next top-level heading of the same or a higher level.");
    py::class_<document_ref> doc_cls(m, "Document",
        "A parsed markdown document: the exact source text and its block tree. Immutable — "
        "replace() and with_front_matter() return a new Document and leave every other byte as it was.");
    bind_kinds(block_kinds{}, m);

    block_cls
        .def_property_readonly("lines", [](const block_ref& b) { return py::make_tuple(b.at().first_line, b.at().last_line); },
                               "(first, last) source lines, 1-based and inclusive.")
        .def_property_readonly("span", [](const block_ref& b) { return span(*b.doc, b.at().begin, b.at().end); },
                               "(start, end) character offsets into Document.text: whole lines, the last line ending included.")
        .def_property_readonly("text", [](const block_ref& b) { return text(*b.doc, b.at().begin, b.at().end); },
                               "Its source lines, exactly (markers and indentation included).")
        .def_property_readonly("plain", [](const block_ref& b) { return str_of(b.doc->plain(b.index)); },
                               "The text a reader sees: inline markup resolved; a code block's code; a table's cells.")
        .def_property_readonly("children", [](const block_ref& b) { return wrapped(b.doc, b.doc->core().children(b.index)); })
        .def_property_readonly("parent", [](const block_ref& b) -> py::object {
            const std::uint32_t p = b.at().parent;
            return p == 0 || p == mk::none ? py::object(py::none()) : wrap(block_ref{b.doc, p});
        }, "The block holding this one, or None at the top level.")
        .def("html", [](const block_ref& b) { return str_of(b.doc->visit([&](const auto& d) { return d.html(b.index); })); },
             "This block as HTML.")
        .def("__repr__", [](py::handle self) {
            const auto& b = self.cast<const block_ref&>();
            std::string preview = b.doc->plain(b.index);
            preview = preview.substr(0, preview.find('\n'));
            if (mk::write::width(preview) > 40) {
                std::size_t i = 0;
                for (int n = 0; n < 39 && i < preview.size(); ++n) i += mk::decode(preview, i).len;
                preview = preview.substr(0, i) + "\xE2\x80\xA6";   // …
            }
            return py::str(self.get_type().attr("__name__")).cast<std::string>() + "(lines " + std::to_string(b.at().first_line) +
                   "-" + std::to_string(b.at().last_line) + ", " + py::repr(str_of(preview)).cast<std::string>() + ")";
        })
        .def("__eq__", [](const block_ref& a, py::handle o) {
            return py::isinstance<block_ref>(o) && o.cast<const block_ref&>().doc == a.doc && o.cast<const block_ref&>().index == a.index;
        })
        .def("__hash__", [](const block_ref& b) {
            return static_cast<py::ssize_t>(std::hash<const void*>{}(b.doc.get()) ^ (b.index * 0x9E3779B9u));
        });

    section_cls
        .def_property_readonly("heading", [](const section_ref& s) { return wrap(block_ref{s.doc, s.at().heading}); })
        .def_property_readonly("level", [](const section_ref& s) { return s.at().level; })
        .def_property_readonly("title", [](const section_ref& s) { return str_of(s.doc->headings()[s.at().slug_index].title); })
        .def_property_readonly("slug", [](const section_ref& s) { return str_of(s.doc->headings()[s.at().slug_index].slug); })
        .def_property_readonly("lines", [](const section_ref& s) { return py::make_tuple(s.at().first_line, s.at().last_line); })
        .def_property_readonly("span", [](const section_ref& s) { return span(*s.doc, s.at().begin, s.at().end); },
                               "(start, end) character offsets into Document.text; the end is where the next section begins.")
        .def_property_readonly("text", [](const section_ref& s) { return text(*s.doc, s.at().begin, s.at().end); })
        .def_property_readonly("blocks", [](const section_ref& s) {
            std::vector<std::uint32_t> out;
            for (std::uint32_t c = s.doc->at(s.at().heading).next; c != mk::none && s.doc->at(c).begin < s.at().end; c = s.doc->at(c).next) {
                out.push_back(c);
            }
            return wrapped(s.doc, out);
        }, "The top-level blocks after the heading, up to the section's end (subsection headings included).")
        .def_property_readonly("subsections", [](const section_ref& s) {
            py::list out;
            const auto& ss = s.doc->sections();
            for (std::uint32_t i = 0; i < ss.size(); ++i) {
                if (ss[i].parent == s.index) out.append(section_ref{s.doc, i});
            }
            return out;
        })
        .def_property_readonly("parent", [](const section_ref& s) -> py::object {
            return s.at().parent == mk::none ? py::object(py::none()) : py::cast(section_ref{s.doc, s.at().parent});
        })
        .def("__repr__", [](const section_ref& s) {
            return "Section(" + std::to_string(s.at().level) + ", " +
                   py::repr(str_of(s.doc->headings()[s.at().slug_index].title)).cast<std::string>() + ", lines " +
                   std::to_string(s.at().first_line) + "-" + std::to_string(s.at().last_line) + ")";
        })
        .def("__eq__", [](const section_ref& a, py::handle o) {
            return py::isinstance<section_ref>(o) && o.cast<const section_ref&>().doc == a.doc && o.cast<const section_ref&>().index == a.index;
        })
        .def("__hash__", [](const section_ref& s) {
            return static_cast<py::ssize_t>(std::hash<const void*>{}(s.doc.get()) ^ (s.index * 0x85EBCA6Bu));
        });

    doc_cls
        .def(py::init([](const std::string& text, const std::string& dialect, const std::string& slugs, bool front_matter) {
                 return document_ref{parse(text, dialect, slugs, front_matter, "<string>")};
             }),
             py::arg("text"), py::kw_only(), py::arg("dialect") = "gfm", py::arg("slugs") = "github",
             py::arg("front_matter") = true,
             "Parse markdown text (with the GIL released). dialect: 'gfm' (CommonMark plus tables, "
             "strikethrough, task items) or 'commonmark'; slugs: 'github' or 'toc' (Python-Markdown's "
             "anchors); front_matter: read a leading ---/+++ block as YAML/TOML front matter.")
        .def_property_readonly("text", [](const document_ref& d) { return str_of(d.doc->core().source()); }, "The source text, exactly.")
        .def("__str__", [](const document_ref& d) { return str_of(d.doc->core().source()); })
        .def("__repr__", [](const document_ref& d) {
            std::string r = "Document(" + std::string(d.doc->doc.dialect()) + ", " + std::to_string(d.doc->core().children(0).size()) +
                            " blocks, " + std::to_string(d.doc->core().structure().line_starts.size()) + " lines";
            if (d.doc->origin != "<string>") r += ", " + d.doc->origin;
            return r + ")";
        })
        .def_property_readonly("dialect", [](const document_ref& d) { return str_of(d.doc->doc.dialect()); })
        .def_property_readonly("slugs", [](const document_ref& d) { return str_of(d.doc->doc.slugs()); })
        .def_property_readonly("front_matter", [](const document_ref& d) -> py::object {
            const std::uint32_t fm = d.doc->core().front_matter();
            if (fm == mk::none) return py::none();
            const mk::block& b = d.doc->at(fm);
            return load_front_matter(front_matter_formats{}, b.marker, d.doc->core().raw(fm),
                                     "front matter (lines " + std::to_string(b.first_line) + "-" + std::to_string(b.last_line) +
                                         ") of " + d.doc->origin);
        }, "The front matter as data (YAML for ---, TOML for +++, through pathlike's engines), or None. "
           "A parse error names the lines and the file.")
        .def_property_readonly("blocks", [](const document_ref& d) { return wrapped(d.doc, d.doc->core().children(0)); },
                               "The top-level blocks, in order, each of its kind's class.")
        .def("walk", [](const document_ref& d) { return wrapped(d.doc, d.doc->core().walk()); },
             "Every block, depth-first (a container before what it holds).")
        .def("find", [block_type = block_cls.ptr()](const document_ref& d, py::handle cls) {
            if (cls.ptr() == block_type) return wrapped(d.doc, d.doc->core().walk());
            const mk::kind* k = PyType_Check(cls.ptr()) ? class_kinds().try_get_const(reinterpret_cast<PyTypeObject*>(cls.ptr())) : nullptr;
            if (!k) {
                throw py::type_error("find: give a Block class (Heading, Code, Table, ...), got " + py::repr(cls).cast<std::string>());
            }
            return wrapped(d.doc, d.doc->core().find(*k));
        }, py::arg("cls"), "The blocks of a class (markdown.Heading, markdown.Code, ...), depth-first; Block gives every block.")
        .def_property_readonly("sections", [](const document_ref& d) {
            py::list out;
            for (std::uint32_t i = 0; i < d.doc->sections().size(); ++i) out.append(section_ref{d.doc, i});
            return out;
        }, "The sections of the top-level headings, in order (nested ones listed too).")
        .def("section", [](const document_ref& d, const std::string& key) {
            const std::uint32_t i = d.doc->visit([&](const auto& x) { return x.section_of(key); });
            if (i != mk::none) return section_ref{d.doc, i};
            std::string known;
            for (const auto& s : d.doc->sections()) (known += known.empty() ? "" : ", ") += d.doc->headings()[s.slug_index].slug;
            throw py::key_error("no section '" + key + "' (sections: " + known + ")");
        }, py::arg("key"), "The section whose slug, else whose title, is key. KeyError names the slugs there are.")
        .def("html", [](const document_ref& d) {
            std::string out;
            {
                py::gil_scoped_release nogil;
                out = d.doc->visit([](const auto& x) { return x.html(0); });
            }
            return str_of(out);
        }, "The document as HTML, as CommonMark's reference renderer writes it (front matter and definitions render nothing).")
        .def_property_readonly("plain", [](const document_ref& d) { return str_of(d.doc->plain(0)); },
                               "The text a reader sees, inline markup resolved, blocks one blank line apart.")
        .def("replace", [](const document_ref& d, const edit_target& target, const std::string& text) {
            const auto [begin, end] = std::visit([&](const auto& t) { return region(d.doc, t); }, target);
            return document_ref{reparse(*d.doc, d.doc->core().splice(begin, end, text))};
        }, py::arg("target"), py::arg("text"),
           "A new Document with a top-level block or a section replaced by text; every other byte, and the "
           "blank lines that separated the target from what follows, unchanged. Empty text deletes it.")
        .def("with_front_matter", [](const document_ref& d, py::handle data, std::optional<std::string> engine) {
            if (!d.doc->core().recognises_front_matter()) {
                throw std::invalid_argument("with_front_matter: this document was parsed with front_matter=False");
            }
            const std::uint32_t fm = d.doc->core().front_matter();
            const std::string name = engine ? *engine
                                   : fm != mk::none ? std::string(format_named_by(front_matter_formats{}, d.doc->at(fm).marker))
                                                    : std::string(names_of(front_matter_formats{})[0]);
            const std::string text = data.is_none() ? std::string() : dump_front_matter(front_matter_formats{}, data, name);
            return document_ref{reparse(*d.doc, d.doc->core().with_front_matter(text))};
        }, py::arg("data"), py::kw_only(), py::arg("engine") = py::none(),
           "A new Document with its front matter set to data (None removes it), written by the YAML or TOML "
           "engine (engine= picks; default: the one already there, else yaml). The body is unchanged.")
        .def("stats", [](const document_ref& d) {
            py::dict out;
            out["source"] = d.doc->core().source().size();
            out["blocks"] = d.doc->core().walk().size();
            out["lines"] = d.doc->core().structure().line_starts.size();
            out["bytes"] = d.doc->visit([](const auto& x) { return x.bytes(); }) + d.doc->origin.capacity();
            return out;
        }, "Sizes: 'source' bytes, 'blocks', 'lines', and 'bytes' — every heap byte behind the document, exactly.");

    // ── builders: markdown in, markdown text out ──
    m.def("escape", [](const std::string& text) { return str_of(mk::write::escape(text)); }, py::arg("text"),
          "Plain text made safe as markdown: what could start markup is backslash-escaped, line breaks kept.");
    m.def("heading", [](int level, const std::string& text) { return str_of(mk::write::heading(level, text)); },
          py::arg("level"), py::arg("text"), "An ATX heading line ('## text'); level 1-6. text is markdown (escape() plain text).");
    m.def("code", [](const std::string& text, const std::string& lang) { return str_of(mk::write::code(text, lang)); },
          py::arg("text"), py::arg("lang") = "", "A fenced code block whose fence no line of text can close; lang is its info string.");
    m.def("table", [](py::handle header, py::handle rows, py::object align) {
        std::vector<std::vector<std::string>> body;
        for (py::handle r : rows) body.push_back(strings_of(r));
        std::vector<mk::align> al;
        if (!align.is_none()) {
            for (py::handle a : align) al.push_back(align_from(a));
        }
        return str_of(mk::write::table(strings_of(header), body, al));
    }, py::arg("header"), py::arg("rows"), py::kw_only(), py::arg("align") = py::none(),
       "A GFM table, columns padded to line up. Cells are str() of the values and are markdown; a '|' "
       "in a cell is escaped and a line break becomes <br>. align: 'left', 'center', 'right' or None per column.");
    m.def("bullets", [](py::handle items, bool numbered, std::uint32_t start) {
        return str_of(mk::write::items(strings_of(items), numbered, start));
    }, py::arg("items"), py::kw_only(), py::arg("numbered") = false, py::arg("start") = 1,
       "A bullet list (or numbered from start); an item's later lines are indented under it.");
    m.def("quote", [](const std::string& text) { return str_of(mk::write::quote(text)); }, py::arg("text"),
          "A block quote: every line prefixed with '> '.");
    m.def("front_matter", [](py::handle data, const std::string& engine) {
        return str_of(dump_front_matter(front_matter_formats{}, data, engine));
    }, py::arg("data"), py::kw_only(), py::arg("engine") = "yaml",
       "Front matter text: data written by the YAML (---) or TOML (+++) engine.");
    m.def("join", [](py::handle blocks) { return str_of(mk::write::join(strings_of(blocks))); }, py::arg("blocks"),
          "Blocks joined into a document: one blank line between, empty ones dropped, one final line ending.");

    if constexpr (probes) bind_probes(m);
}

}  // namespace pygim::pathlike::markdown_py
