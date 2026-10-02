#pragma once
// pathlike/adapter/markdown.h — `pygim.pathlike.markdown`: Document, Block, Section and the builders.
//
// The core (../markdown/) is pybind-free and templated on its policies —
// dialect (gfm, commonmark) and slug rule (github, toc). Python picks them by
// argument, `Document(text, dialect="commonmark", slugs="toc")`, so this
// adapter type-erases the four instantiations behind `doc_base` and every
// Python object holds a shared pointer to one immutable document:
//
//     Document  -> shared_ptr<const doc_base>          the document
//     Block     -> (that pointer, block index)         a view: no copy
//     Section   -> (that pointer, section index)       a view: no copy
//
// Example (Python):
//
//     doc = pygim.path("notes.md").read()              # markdown.Document; parsed with the GIL released
//     doc.find("heading", level=2)[0].slug             # 'usage'
//     doc.section("usage").span                        # (41, 97): character offsets into doc.text
//     doc.replace(doc.section("usage"), "## Usage\n\nNew.\n")   # a new Document; the rest unchanged
//
// Spans are CHARACTER offsets into `Document.text` (what Python slices with),
// translated from the core's byte offsets through a per-line table built on
// first use. Threading: a document is immutable; its lazily filled caches
// (headings, sections, the character table) fill while the GIL is held.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "../markdown/document.h"
#include "../markdown/writer.h"
#include "common.h"
#include "engines/toml.h"
#include "engines/yaml.h"
#include "materialize.h"

namespace pygim::pathlike::markdown_py {

namespace py = pybind11;
namespace mk = pygim::pathlike::markdown;

/// One document, whatever its policies: what Block and Section reach through.
class doc_base {
public:
    virtual ~doc_base() = default;
    std::string origin;   // the file, or "<string>": what errors name

    [[nodiscard]] virtual const std::string& source() const = 0;
    [[nodiscard]] virtual const mk::tree& structure() const = 0;
    [[nodiscard]] virtual std::string_view dialect() const = 0;
    [[nodiscard]] virtual std::string_view slugs() const = 0;
    [[nodiscard]] virtual bool reads_front_matter() const = 0;
    [[nodiscard]] virtual std::string plain(std::uint32_t) const = 0;
    [[nodiscard]] virtual std::string inline_source(std::uint32_t) const = 0;
    [[nodiscard]] virtual std::string code(std::uint32_t) const = 0;
    [[nodiscard]] virtual std::string info(std::uint32_t) const = 0;
    [[nodiscard]] virtual std::string raw(std::uint32_t) const = 0;
    [[nodiscard]] virtual std::string html(std::uint32_t) const = 0;
    [[nodiscard]] virtual std::string cell_plain(std::uint32_t, std::uint32_t, std::uint32_t) const = 0;
    [[nodiscard]] virtual const std::vector<mk::heading_info>& headings() const = 0;
    [[nodiscard]] virtual const std::vector<mk::section>& sections() const = 0;
    [[nodiscard]] virtual std::string splice(std::uint32_t, std::uint32_t, std::string_view) const = 0;
    /// The same policies over new source text.
    [[nodiscard]] virtual std::shared_ptr<const doc_base> reparse(std::string source) const = 0;

    [[nodiscard]] const mk::block& at(std::uint32_t i) const { return structure().blocks[i]; }

    /// The character offset of byte `b` (the core counts bytes, Python characters).
    [[nodiscard]] std::size_t chars(std::uint32_t b) const {
        const std::string& s = source();
        const auto& starts = structure().line_starts;
        if (!m_chars) {
            std::vector<std::size_t> table;
            table.reserve(starts.size());
            std::size_t n = 0, at = 0;
            for (std::uint32_t ls : starts) {
                n += count(std::string_view(s).substr(at, ls - at));
                at = ls;
                table.push_back(n);
            }
            m_total = n + count(std::string_view(s).substr(at));
            m_chars = std::move(table);
        }
        if (b >= s.size()) return m_total;
        const auto it = std::upper_bound(starts.begin(), starts.end(), b);
        if (it == starts.begin()) return count(std::string_view(s).substr(0, b));
        const std::size_t line = static_cast<std::size_t>(it - starts.begin()) - 1;
        return (*m_chars)[line] + count(std::string_view(s).substr(starts[line], b - starts[line]));
    }

    /// The heading_info of heading block `i`.
    [[nodiscard]] const mk::heading_info& heading_of(std::uint32_t i) const {
        if (m_heading_index.empty()) {
            const auto& hs = headings();
            for (std::size_t k = 0; k < hs.size(); ++k) m_heading_index.emplace(hs[k].block, k);
        }
        return headings()[m_heading_index.at(i)];
    }

private:
    mutable std::optional<std::vector<std::size_t>> m_chars;
    mutable std::size_t m_total = 0;
    mutable std::unordered_map<std::uint32_t, std::size_t> m_heading_index;

    [[nodiscard]] static std::size_t count(std::string_view v) noexcept {
        std::size_t n = 0;
        for (char c : v) {
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
        }
        return n;
    }
};

template <mk::Dialect D, mk::SlugPolicy S>
class doc_impl final : public doc_base {
public:
    doc_impl(std::string source, bool front_matter) : m_doc(std::move(source), front_matter) {}

    const std::string& source() const override { return m_doc.source(); }
    const mk::tree& structure() const override { return m_doc.structure(); }
    std::string_view dialect() const override { return D::name; }
    std::string_view slugs() const override { return S::name; }
    bool reads_front_matter() const override { return m_doc.recognises_front_matter(); }
    std::string plain(std::uint32_t i) const override { return m_doc.plain(i); }
    std::string inline_source(std::uint32_t i) const override { return m_doc.inline_source(i); }
    std::string code(std::uint32_t i) const override { return m_doc.code(i); }
    std::string info(std::uint32_t i) const override { return m_doc.info(i); }
    std::string raw(std::uint32_t i) const override { return m_doc.raw(i); }
    std::string html(std::uint32_t i) const override { return m_doc.html(i); }
    std::string cell_plain(std::uint32_t t, std::uint32_t r, std::uint32_t c) const override { return m_doc.cell_plain(t, r, c); }
    const std::vector<mk::heading_info>& headings() const override { return m_doc.headings(); }
    const std::vector<mk::section>& sections() const override { return m_doc.sections(); }
    std::string splice(std::uint32_t b, std::uint32_t e, std::string_view t) const override { return m_doc.splice(b, e, t); }
    std::shared_ptr<const doc_base> reparse(std::string source) const override;

private:
    mk::basic_document<D, S> m_doc;
};

using doc_ptr = std::shared_ptr<const doc_base>;

/// Parses `source` under the named policies, with the GIL released.
/// ValueError for a dialect or slug rule that does not exist.
template <mk::Dialect D>
[[nodiscard]] doc_ptr make_with(std::string source, std::string_view slugs, bool front_matter) {
    if (slugs == mk::github_slug::name) return std::make_shared<const doc_impl<D, mk::github_slug>>(std::move(source), front_matter);
    if (slugs == mk::toc_slug::name) return std::make_shared<const doc_impl<D, mk::toc_slug>>(std::move(source), front_matter);
    throw std::invalid_argument("markdown slugs must be github or toc, got '" + std::string(slugs) + "'");
}
[[nodiscard]] inline doc_ptr make_doc(std::string source, std::string_view dialect, std::string_view slugs,
                                      bool front_matter, std::string origin) {
    if (dialect != mk::gfm::name && dialect != mk::commonmark::name) {
        throw std::invalid_argument("markdown dialect must be gfm or commonmark, got '" + std::string(dialect) + "'");
    }
    if (slugs != mk::github_slug::name && slugs != mk::toc_slug::name) {
        throw std::invalid_argument("markdown slugs must be github or toc, got '" + std::string(slugs) + "'");
    }
    std::shared_ptr<doc_base> d;
    {
        py::gil_scoped_release nogil;
        doc_ptr p = dialect == mk::gfm::name ? make_with<mk::gfm>(std::move(source), slugs, front_matter)
                                             : make_with<mk::commonmark>(std::move(source), slugs, front_matter);
        d = std::const_pointer_cast<doc_base>(std::move(p));
    }
    d->origin = std::move(origin);
    return d;
}

template <mk::Dialect D, mk::SlugPolicy S>
std::shared_ptr<const doc_base> doc_impl<D, S>::reparse(std::string source) const {
    return make_doc(std::move(source), D::name, S::name, reads_front_matter(), origin);
}

// ── the Python objects ──────────────────────────────────────────────────────

struct document_ref {
    doc_ptr doc;
};
struct block_ref {
    doc_ptr doc;
    std::uint32_t index = 0;
};
struct section_ref {
    doc_ptr doc;
    std::uint32_t index = 0;
};

[[nodiscard]] inline py::str str_of(std::string_view s) { return py::str(s.data(), s.size()); }

[[nodiscard]] inline py::object block_or_none(const doc_ptr& d, std::uint32_t i) {
    if (i == mk::none || i == 0) return py::none();
    return py::cast(block_ref{d, i});
}

/// The top-level blocks, or the children of block `parent`.
[[nodiscard]] inline py::list children_of(const doc_ptr& d, std::uint32_t parent) {
    py::list out;
    for (std::uint32_t c = d->at(parent).first; c != mk::none; c = d->at(c).next) out.append(block_ref{d, c});
    return out;
}

/// Every block below the document, pre-order.
[[nodiscard]] inline std::vector<std::uint32_t> walk_of(const doc_base& d) {
    std::vector<std::uint32_t> out;
    std::uint32_t cur = d.at(0).first;
    while (cur != mk::none) {
        out.push_back(cur);
        if (d.at(cur).first != mk::none) {
            cur = d.at(cur).first;
            continue;
        }
        while (cur != mk::none && d.at(cur).next == mk::none) {
            cur = d.at(cur).parent;
            if (cur == 0) cur = mk::none;
        }
        if (cur != mk::none) cur = d.at(cur).next;
    }
    return out;
}

/// The front matter as Python data, through the YAML or TOML engine; None when there is none.
[[nodiscard]] inline py::object front_matter_of(const doc_base& d) {
    const std::uint32_t first = d.at(0).first;
    if (first == mk::none || d.at(first).type != mk::kind::front_matter) return py::none();
    const mk::block& b = d.at(first);
    const std::string body = d.raw(first);
    const std::string origin = "front matter (lines " + std::to_string(b.first_line) + "-" + std::to_string(b.last_line) +
                               ") of " + d.origin;
    detail::KeyCache keys(256);
    if (b.marker == '+') return engines::toml::loads(body, origin, keys);
    return engines::yaml::loads(body, origin, keys);
}

[[nodiscard]] inline std::string_view align_name(mk::align a) {
    switch (a) {
        case mk::align::left: return "left";
        case mk::align::center: return "center";
        case mk::align::right: return "right";
        default: return {};
    }
}

[[nodiscard]] inline std::string lang_of(const std::string& info) {
    std::size_t w = 0;
    while (w < info.size() && !mk::is_ascii_whitespace(info[w])) ++w;
    return info.substr(0, w);
}

/// "Block(heading, lines 5-5, 'Notes')": the first line of its text, cut at 40 characters.
[[nodiscard]] inline std::string block_repr(const block_ref& b) {
    const mk::block& x = b.doc->at(b.index);
    std::string preview = x.type == mk::kind::heading ? b.doc->heading_of(b.index).title : b.doc->plain(b.index);
    preview = preview.substr(0, preview.find('\n'));
    if (mk::write::width(preview) > 40) {
        std::size_t n = 0, i = 0;
        while (i < preview.size() && n < 39) {
            i += mk::decode(preview, i).len;
            ++n;
        }
        preview = preview.substr(0, i) + "\xE2\x80\xA6";   // …
    }
    return "Block(" + std::string(mk::kind_name(x.type)) + ", lines " + std::to_string(x.first_line) + "-" +
           std::to_string(x.last_line) + ", " + py::repr(str_of(preview)).cast<std::string>() + ")";
}

[[nodiscard]] inline const std::string& checked_engine(const std::string& engine) {
    if (engine != "yaml" && engine != "toml") {
        throw std::invalid_argument("front matter engine must be yaml or toml, got '" + engine + "'");
    }
    return engine;
}

[[nodiscard]] inline std::string front_matter_text(py::handle data, const std::string& engine) {
    return engine == "toml" ? mk::write::front_matter(engines::toml::dumps(data), '+')
                            : mk::write::front_matter(engines::yaml::dumps(data), '-');
}

[[nodiscard]] inline mk::align align_from(py::handle a) {
    if (a.is_none()) return mk::align::none;
    const std::string s = py::str(a);
    if (s.empty()) return mk::align::none;
    if (s == "left") return mk::align::left;
    if (s == "center") return mk::align::center;
    if (s == "right") return mk::align::right;
    throw std::invalid_argument("table align must be left, center, right or None, got '" + s + "'");
}

[[nodiscard]] inline std::vector<std::string> strings_of(py::handle seq) {
    std::vector<std::string> out;
    for (py::handle x : seq) out.push_back(py::str(x).cast<std::string>());
    return out;
}

/// Binds the `markdown` submodule of `pathlike`.
inline void bind(py::module_& parent) {
    py::module_ m = parent.def_submodule(
        "markdown",
        "Markdown documents: parse (CommonMark 0.31.2, GFM tables/strikethrough/task items, front matter), "
        "query (blocks, sections, headings, tables, code), edit losslessly, render HTML, and build markdown text.");

    py::class_<block_ref> block_cls(m, "Block",
        "One block of a Document: a view (the document and an index), never a copy. "
        "`kind` says what it is; properties that do not apply to its kind are None.");
    py::class_<section_ref> section_cls(m, "Section",
        "A top-level heading and everything up to the next top-level heading of the same or a higher level.");
    py::class_<document_ref> doc_cls(m, "Document",
        "A parsed markdown document: the exact source text and its block tree. Immutable — "
        "replace() and with_front_matter() return a new Document and leave every other byte as it was.");

    doc_cls
        .def(py::init([](const std::string& text, const std::string& dialect, const std::string& slugs, bool front_matter) {
                 return document_ref{make_doc(text, dialect, slugs, front_matter, "<string>")};
             }),
             py::arg("text"), py::kw_only(), py::arg("dialect") = "gfm", py::arg("slugs") = "github",
             py::arg("front_matter") = true,
             "Parse markdown text (with the GIL released). dialect: 'gfm' (CommonMark plus tables, "
             "strikethrough, task items) or 'commonmark'; slugs: 'github' or 'toc' (Python-Markdown's "
             "anchors); front_matter: read a leading ---/+++ block as YAML/TOML front matter.")
        .def_property_readonly("text", [](const document_ref& d) { return str_of(d.doc->source()); }, "The source text, exactly.")
        .def("__str__", [](const document_ref& d) { return str_of(d.doc->source()); })
        .def("__repr__", [](const document_ref& d) {
            std::size_t top = 0;
            for (std::uint32_t c = d.doc->at(0).first; c != mk::none; c = d.doc->at(c).next) ++top;
            std::string r = "Document(" + std::string(d.doc->dialect()) + ", " + std::to_string(top) + " blocks, " +
                            std::to_string(d.doc->structure().line_starts.size()) + " lines";
            if (d.doc->origin != "<string>") r += ", " + d.doc->origin;
            return r + ")";
        })
        .def_property_readonly("dialect", [](const document_ref& d) { return std::string(d.doc->dialect()); })
        .def_property_readonly("slugs", [](const document_ref& d) { return std::string(d.doc->slugs()); })
        .def_property_readonly("front_matter", [](const document_ref& d) { return front_matter_of(*d.doc); },
            "The front matter as data (YAML for ---, TOML for +++, through pathlike's engines), or None. "
            "A parse error names the lines and the file.")
        .def_property_readonly("blocks", [](const document_ref& d) { return children_of(d.doc, 0); }, "The top-level blocks, in order.")
        .def("walk", [](const document_ref& d) {
                py::list out;
                for (std::uint32_t i : walk_of(*d.doc)) out.append(block_ref{d.doc, i});
                return out;
            }, "Every block, depth-first (a container before what it holds).")
        .def("find", [](const document_ref& d, py::object kind, py::object level, py::object lang) {
                std::optional<mk::kind> k;
                if (!kind.is_none()) {
                    const std::string want = py::str(kind);
                    std::string known;
                    for (int x = static_cast<int>(mk::kind::front_matter); x <= static_cast<int>(mk::kind::definition); ++x) {
                        const auto kx = static_cast<mk::kind>(x);
                        if (want == mk::kind_name(kx)) k = kx;
                        known += (known.empty() ? "" : ", ") + std::string(mk::kind_name(kx));
                    }
                    if (!k) throw std::invalid_argument("no block kind '" + want + "' (kinds: " + known + ")");
                }
                py::list out;
                for (std::uint32_t i : walk_of(*d.doc)) {
                    const mk::block& b = d.doc->at(i);
                    if (k && b.type != *k) continue;
                    if (!level.is_none() && !(b.type == mk::kind::heading && b.level == level.cast<std::uint32_t>())) continue;
                    if (!lang.is_none() && !(b.type == mk::kind::code && lang_of(d.doc->info(i)) == py::str(lang).cast<std::string>())) continue;
                    out.append(block_ref{d.doc, i});
                }
                return out;
            }, py::arg("kind") = py::none(), py::kw_only(), py::arg("level") = py::none(), py::arg("lang") = py::none(),
            "The blocks of a kind ('heading', 'code', 'table', ...), depth-first; level= narrows headings, lang= code blocks.")
        .def_property_readonly("sections", [](const document_ref& d) {
                py::list out;
                for (std::uint32_t i = 0; i < d.doc->sections().size(); ++i) out.append(section_ref{d.doc, i});
                return out;
            }, "The sections of the top-level headings, in order (nested ones listed too).")
        .def("section", [](const document_ref& d, const std::string& key) {
                const auto& ss = d.doc->sections();
                for (std::uint32_t i = 0; i < ss.size(); ++i) {
                    if (d.doc->headings()[ss[i].slug_index].slug == key) return section_ref{d.doc, i};
                }
                for (std::uint32_t i = 0; i < ss.size(); ++i) {
                    if (d.doc->headings()[ss[i].slug_index].title == key) return section_ref{d.doc, i};
                }
                std::string known;
                for (const auto& s : ss) known += (known.empty() ? "" : ", ") + d.doc->headings()[s.slug_index].slug;
                throw py::key_error("no section '" + key + "' (sections: " + known + ")");
            }, py::arg("key"), "The section whose slug, else whose title, is key. KeyError names the slugs there are.")
        .def("html", [](const document_ref& d) {
                std::string out;
                {
                    py::gil_scoped_release nogil;
                    out = d.doc->html(0);
                }
                return str_of(out);
            }, "The document as HTML, as CommonMark's reference renderer writes it (front matter and definitions render nothing).")
        .def_property_readonly("plain", [](const document_ref& d) { return str_of(d.doc->plain(0)); },
            "The text a reader sees, inline markup resolved, blocks one blank line apart.")
        .def("replace", [](const document_ref& d, py::handle target, const std::string& text) {
                std::uint32_t begin = 0, end = 0;
                if (py::isinstance<block_ref>(target)) {
                    const auto& b = target.cast<const block_ref&>();
                    if (b.doc != d.doc) throw std::invalid_argument("replace: that block belongs to another document");
                    const mk::block& x = d.doc->at(b.index);
                    if (x.parent != 0) {
                        throw std::invalid_argument("replace: only a top-level block or a section can be replaced, not a " +
                                                    std::string(mk::kind_name(x.type)) + " inside a " +
                                                    std::string(mk::kind_name(d.doc->at(x.parent).type)) + " (lines " +
                                                    std::to_string(x.first_line) + "-" + std::to_string(x.last_line) + ")");
                    }
                    begin = x.begin;
                    end = x.end;
                } else if (py::isinstance<section_ref>(target)) {
                    const auto& s = target.cast<const section_ref&>();
                    if (s.doc != d.doc) throw std::invalid_argument("replace: that section belongs to another document");
                    begin = d.doc->sections()[s.index].begin;
                    end = d.doc->sections()[s.index].end;
                } else {
                    throw py::type_error("replace: target must be a Block or a Section of this document");
                }
                return document_ref{d.doc->reparse(d.doc->splice(begin, end, text))};
            }, py::arg("target"), py::arg("text"),
            "A new Document with a top-level block or a section replaced by text; every other byte, and the "
            "blank lines that separated the target from what follows, unchanged. Empty text deletes it.")
        .def("with_front_matter", [](const document_ref& d, py::handle data, std::optional<std::string> engine) {
                if (!d.doc->reads_front_matter()) {
                    throw std::invalid_argument("with_front_matter: this document was parsed with front_matter=False");
                }
                const std::uint32_t first = d.doc->at(0).first;
                const bool has = first != mk::none && d.doc->at(first).type == mk::kind::front_matter;
                const std::string eng = checked_engine(engine ? *engine : has && d.doc->at(first).marker == '+' ? std::string("toml") : std::string("yaml"));
                if (data.is_none()) {
                    if (!has) return document_ref{d.doc};
                    return document_ref{d.doc->reparse(d.doc->splice(d.doc->at(first).begin, d.doc->at(first).end, ""))};
                }
                const std::string text = front_matter_text(data, eng);
                if (has) return document_ref{d.doc->reparse(d.doc->splice(d.doc->at(first).begin, d.doc->at(first).end, text))};
                const std::string& src = d.doc->source();
                const std::size_t at = std::string_view(src).starts_with("\xEF\xBB\xBF") ? 3 : 0;
                return document_ref{d.doc->reparse(src.substr(0, at) + text + src.substr(at))};
            }, py::arg("data"), py::kw_only(), py::arg("engine") = py::none(),
            "A new Document with its front matter set to data (None removes it), written by the YAML or TOML "
            "engine (engine= picks; default: the one already there, else yaml). The body is unchanged.")
        .def("stats", [](const document_ref& d) {
                const mk::tree& t = d.doc->structure();
                std::size_t strings = 0;
                for (const auto& def : t.definitions) strings += def.label.capacity() + def.destination.capacity() + def.title.capacity();
                py::dict out;
                out["source"] = d.doc->source().size();
                out["blocks"] = walk_of(*d.doc).size();
                out["lines"] = t.line_starts.size();
                out["bytes"] = sizeof(doc_base) + d.doc->source().capacity() + t.blocks.capacity() * sizeof(mk::block) +
                               t.segments.capacity() * sizeof(mk::segment) + t.aligns.capacity() * sizeof(mk::align) +
                               t.definitions.capacity() * sizeof(mk::definition) + strings +
                               t.by_label.capacity() * sizeof(std::uint32_t) + t.line_starts.capacity() * sizeof(std::uint32_t);
                return out;
            }, "Sizes: 'source' bytes, 'blocks', 'lines', and 'bytes' — the source plus the tree, exactly.");

    block_cls
        .def_property_readonly("kind", [](const block_ref& b) { return std::string(mk::kind_name(b.doc->at(b.index).type)); })
        .def_property_readonly("lines", [](const block_ref& b) {
            const mk::block& x = b.doc->at(b.index);
            return py::make_tuple(x.first_line, x.last_line);
        }, "(first, last) source lines, 1-based and inclusive.")
        .def_property_readonly("span", [](const block_ref& b) {
            const mk::block& x = b.doc->at(b.index);
            return py::make_tuple(b.doc->chars(x.begin), b.doc->chars(x.end));
        }, "(start, end) character offsets into Document.text: whole lines, the last line ending included.")
        .def_property_readonly("text", [](const block_ref& b) {
            const mk::block& x = b.doc->at(b.index);
            return str_of(std::string_view(b.doc->source()).substr(x.begin, x.end - x.begin));
        }, "The block's source lines, exactly (markers and indentation included).")
        .def_property_readonly("plain", [](const block_ref& b) { return str_of(b.doc->plain(b.index)); },
            "The text a reader sees: inline markup resolved; a code block's code; a table's cells.")
        .def_property_readonly("content", [](const block_ref& b) -> py::object {
            const mk::kind k = b.doc->at(b.index).type;
            if (k != mk::kind::paragraph && k != mk::kind::heading) return py::none();
            return str_of(b.doc->inline_source(b.index));
        }, "A paragraph's or heading's inline markdown, without the block's markers; else None.")
        .def("html", [](const block_ref& b) { return str_of(b.doc->html(b.index)); }, "This block as HTML.")
        .def_property_readonly("children", [](const block_ref& b) { return children_of(b.doc, b.index); })
        .def_property_readonly("parent", [](const block_ref& b) { return block_or_none(b.doc, b.doc->at(b.index).parent); },
            "The block holding this one, or None at the top level.")
        .def_property_readonly("level", [](const block_ref& b) -> py::object {
            const mk::block& x = b.doc->at(b.index);
            return x.type == mk::kind::heading ? py::object(py::int_(x.level)) : py::none();
        }, "A heading's level, 1-6.")
        .def_property_readonly("title", [](const block_ref& b) -> py::object {
            const mk::block& x = b.doc->at(b.index);
            if (x.type == mk::kind::heading) return str_of(b.doc->heading_of(b.index).title);
            if (x.type == mk::kind::definition) return str_of(b.doc->structure().definitions[x.def].title);
            return py::none();
        }, "A heading's text, or a link definition's title.")
        .def_property_readonly("slug", [](const block_ref& b) -> py::object {
            if (b.doc->at(b.index).type != mk::kind::heading) return py::none();
            return str_of(b.doc->heading_of(b.index).slug);
        }, "A heading's anchor, unique in the document (the document's slug rule).")
        .def_property_readonly("info", [](const block_ref& b) -> py::object {
            const mk::block& x = b.doc->at(b.index);
            if (x.type != mk::kind::code || !x.fenced) return py::none();
            const std::string s = b.doc->info(b.index);
            return s.empty() ? py::object(py::none()) : py::object(str_of(s));
        }, "A fenced code block's info string ('py title=\"x\"'), unescaped; None when there is none.")
        .def_property_readonly("lang", [](const block_ref& b) -> py::object {
            const mk::block& x = b.doc->at(b.index);
            if (x.type != mk::kind::code || !x.fenced) return py::none();
            const std::string s = lang_of(b.doc->info(b.index));
            return s.empty() ? py::object(py::none()) : py::object(str_of(s));
        }, "The first word of a code block's info string, its language.")
        .def_property_readonly("code", [](const block_ref& b) -> py::object {
            if (b.doc->at(b.index).type != mk::kind::code) return py::none();
            return str_of(b.doc->code(b.index));
        }, "A code block's code, each line ended by a newline.")
        .def_property_readonly("raw", [](const block_ref& b) -> py::object {
            const mk::kind k = b.doc->at(b.index).type;
            if (k != mk::kind::html && k != mk::kind::front_matter) return py::none();
            return str_of(b.doc->raw(b.index));
        }, "An HTML block's text, or the front matter's body.")
        .def_property_readonly("ordered", [](const block_ref& b) -> py::object {
            const mk::block& x = b.doc->at(b.index);
            return x.type == mk::kind::list ? py::object(py::bool_(x.ordered)) : py::none();
        })
        .def_property_readonly("start", [](const block_ref& b) -> py::object {
            const mk::block& x = b.doc->at(b.index);
            return x.type == mk::kind::list && x.ordered ? py::object(py::int_(x.start)) : py::none();
        }, "An ordered list's first number.")
        .def_property_readonly("tight", [](const block_ref& b) -> py::object {
            const mk::block& x = b.doc->at(b.index);
            return x.type == mk::kind::list ? py::object(py::bool_(x.tight)) : py::none();
        }, "Whether a list has no blank lines between or inside its items.")
        .def_property_readonly("checked", [](const block_ref& b) -> py::object {
            const mk::block& x = b.doc->at(b.index);
            if (x.type != mk::kind::item || x.task < 0) return py::none();
            return py::bool_(x.task == 1);
        }, "A task item's state ([x] True, [ ] False); None for any other block.")
        .def_property_readonly("header", [](const block_ref& b) -> py::object {
            const mk::block& x = b.doc->at(b.index);
            if (x.type != mk::kind::table) return py::none();
            py::list out;
            for (std::uint32_t c = 0; c < x.columns; ++c) out.append(str_of(b.doc->cell_plain(b.index, 0, c)));
            return out;
        }, "A table's header cells, as plain text.")
        .def_property_readonly("rows", [](const block_ref& b) -> py::object {
            const mk::block& x = b.doc->at(b.index);
            if (x.type != mk::kind::table) return py::none();
            py::list out;
            for (std::uint32_t r = 1; r < x.rows; ++r) {
                py::list row;
                for (std::uint32_t c = 0; c < x.columns; ++c) row.append(str_of(b.doc->cell_plain(b.index, r, c)));
                out.append(row);
            }
            return out;
        }, "A table's body rows, as plain text, each as wide as the header.")
        .def_property_readonly("align", [](const block_ref& b) -> py::object {
            const mk::block& x = b.doc->at(b.index);
            if (x.type != mk::kind::table) return py::none();
            py::list out;
            for (std::uint32_t c = 0; c < x.columns; ++c) {
                const std::string_view a = align_name(b.doc->structure().aligns[x.align + c]);
                out.append(a.empty() ? py::object(py::none()) : py::object(str_of(a)));
            }
            return out;
        }, "A table's column alignments: 'left', 'center', 'right' or None.")
        .def_property_readonly("label", [](const block_ref& b) -> py::object {
            const mk::block& x = b.doc->at(b.index);
            if (x.type != mk::kind::definition) return py::none();
            return str_of(b.doc->structure().definitions[x.def].label);
        }, "A link definition's label, case-folded as references match it.")
        .def_property_readonly("destination", [](const block_ref& b) -> py::object {
            const mk::block& x = b.doc->at(b.index);
            if (x.type != mk::kind::definition) return py::none();
            return str_of(b.doc->structure().definitions[x.def].destination);
        }, "A link definition's URL.")
        .def("__repr__", &block_repr)
        .def("__eq__", [](const block_ref& a, py::handle o) {
            return py::isinstance<block_ref>(o) && o.cast<const block_ref&>().doc == a.doc && o.cast<const block_ref&>().index == a.index;
        })
        .def("__hash__", [](const block_ref& b) {
            return static_cast<py::ssize_t>(std::hash<const void*>{}(b.doc.get()) ^ (b.index * 0x9E3779B9u));
        });

    const auto sec = [](const section_ref& s) -> const mk::section& { return s.doc->sections()[s.index]; };
    section_cls
        .def_property_readonly("heading", [sec](const section_ref& s) { return block_ref{s.doc, sec(s).heading}; })
        .def_property_readonly("level", [sec](const section_ref& s) { return sec(s).level; })
        .def_property_readonly("title", [sec](const section_ref& s) { return str_of(s.doc->headings()[sec(s).slug_index].title); })
        .def_property_readonly("slug", [sec](const section_ref& s) { return str_of(s.doc->headings()[sec(s).slug_index].slug); })
        .def_property_readonly("lines", [sec](const section_ref& s) { return py::make_tuple(sec(s).first_line, sec(s).last_line); })
        .def_property_readonly("span", [sec](const section_ref& s) {
            return py::make_tuple(s.doc->chars(sec(s).begin), s.doc->chars(sec(s).end));
        }, "(start, end) character offsets into Document.text; the end is where the next section begins.")
        .def_property_readonly("text", [sec](const section_ref& s) {
            return str_of(std::string_view(s.doc->source()).substr(sec(s).begin, sec(s).end - sec(s).begin));
        })
        .def_property_readonly("blocks", [sec](const section_ref& s) {
            py::list out;
            for (std::uint32_t c = s.doc->at(sec(s).heading).next; c != mk::none && s.doc->at(c).begin < sec(s).end; c = s.doc->at(c).next) {
                out.append(block_ref{s.doc, c});
            }
            return out;
        }, "The top-level blocks after the heading, up to the section's end (subsection headings included).")
        .def_property_readonly("subsections", [](const section_ref& s) {
            py::list out;
            const auto& ss = s.doc->sections();
            for (std::uint32_t i = 0; i < ss.size(); ++i) {
                if (ss[i].parent == s.index) out.append(section_ref{s.doc, i});
            }
            return out;
        })
        .def_property_readonly("parent", [sec](const section_ref& s) -> py::object {
            const std::uint32_t p = sec(s).parent;
            return p == mk::none ? py::object(py::none()) : py::cast(section_ref{s.doc, p});
        })
        .def("__repr__", [sec](const section_ref& s) {
            const mk::section& x = sec(s);
            return "Section(" + std::to_string(x.level) + ", " +
                   py::repr(str_of(s.doc->headings()[x.slug_index].title)).cast<std::string>() + ", lines " +
                   std::to_string(x.first_line) + "-" + std::to_string(x.last_line) + ")";
        })
        .def("__eq__", [](const section_ref& a, py::handle o) {
            return py::isinstance<section_ref>(o) && o.cast<const section_ref&>().doc == a.doc && o.cast<const section_ref&>().index == a.index;
        })
        .def("__hash__", [](const section_ref& s) {
            return static_cast<py::ssize_t>(std::hash<const void*>{}(s.doc.get()) ^ (s.index * 0x85EBCA6Bu));
        });

    // ── builders: text in, markdown text out ──
    m.def("escape", [](const std::string& text) { return str_of(mk::write::escape(text)); }, py::arg("text"),
          "Plain text made safe as markdown: what could start markup is backslash-escaped, line breaks kept.");
    m.def("heading", [](int level, const std::string& text) { return str_of(mk::write::heading(level, text)); },
          py::arg("level"), py::arg("text"), "An ATX heading line ('## text'); level 1-6. text is markdown (escape() plain text).");
    m.def("code", [](const std::string& text, const std::string& lang) { return str_of(mk::write::code(text, lang)); },
          py::arg("text"), py::arg("lang") = "",
          "A fenced code block whose fence no line of text can close; lang is its info string.");
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
            return str_of(front_matter_text(data, checked_engine(engine)));
        }, py::arg("data"), py::kw_only(), py::arg("engine") = "yaml",
        "Front matter text: data written by the YAML (---) or TOML (+++) engine.");
    m.def("join", [](py::handle blocks) { return str_of(mk::write::join(strings_of(blocks))); }, py::arg("blocks"),
          "Blocks joined into a document: one blank line between, empty ones dropped, one final line ending.");

    // Test and benchmark hooks: the stop positions a scan policy finds (tests fuzz the
    // SIMD policy against the scalar one), or with count=True only how many, so a
    // benchmark times the scan rather than building a Python list.
    m.def("_stops", [](const std::string& text, const std::string& scan, bool count) -> py::object {
            std::vector<std::size_t> out;
            std::size_t n = 0;
            const auto collect = [&](const auto& ix) {
                for (std::size_t i = ix.next(0); i != mk::stop_index::npos; i = ix.next(i + 1)) {
                    if (count) ++n;
                    else out.push_back(i);
                }
            };
            {
                py::gil_scoped_release nogil;
                if (scan == "scalar") collect(mk::basic_stop_index<mk::scalar_scan>(text));
                else if (scan == "simd") collect(mk::basic_stop_index<mk::default_scan>(text));
            }
            if (scan != "scalar" && scan != "simd") throw std::invalid_argument("scan must be scalar or simd, got '" + scan + "'");
            if (count) return py::int_(n);
            return py::cast(out);
        }, py::arg("text"), py::arg("scan") = "simd", py::arg("count") = false);
    m.attr("SCAN") = std::string(mk::default_scan::name);
}

}  // namespace pygim::pathlike::markdown_py
