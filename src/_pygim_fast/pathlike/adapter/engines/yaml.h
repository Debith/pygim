#pragma once
// pathlike/adapter/engines/yaml.h — the rapidyaml engine: YAML read, and the
// shared ryml-tree write path (rapidyaml emits both YAML and JSON text).
//
// One file IS one engine: the descriptor at the bottom is what the build
// discovers into the registry (see ../../registry.h).
//
// rapidyaml aborts the process on a parse error by default; we install a
// throwing error callback so malformed input surfaces as a Python exception.
//
// Free-threaded CPython note: this module does not yet declare
// py::mod_gil_not_used, so on 3.13t/3.14t the interpreter re-enables the GIL
// when importing it — safe by construction, but no free-threaded scaling
// until the shared-state audit (ryml global callbacks, cached class objects)
// is done and the declaration lands.

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <pybind11/pybind11.h>

#include "../third_party/rapidyaml/ryml_all.hpp"
#include "../../core.h"
#include "../common.h"
#include "../materialize.h"

namespace pygim::pathlike::detail {

[[nodiscard]] inline std::string_view to_sv(ryml::csubstr s) noexcept {
    return s.len ? std::string_view(s.str, s.len) : std::string_view{};
}

// rapidyaml calls this instead of aborting; rethrow as a std::exception so pybind11
// converts it to a Python RuntimeError.
[[noreturn]] inline void throw_on_error(const char* msg, size_t len, ryml::Location loc,
                                        void* /*user_data*/) {
    std::string where;
    if (loc.line) where = " (line " + std::to_string(loc.line) + ")";
    throw std::runtime_error("YAML parse error" + where + ": " + std::string(msg, len));
}

// Install the throwing error callback exactly once, process-wide.
inline void ensure_throwing_callbacks() {
    static const bool installed = [] {
        ryml::Callbacks cb = ryml::get_callbacks();
        cb.m_error = &throw_on_error;
        ryml::set_callbacks(cb);
        return true;
    }();
    (void)installed;
}

// ── Read side ──────────────────────────────────────────────────────────────

// Recursively materialise a rapidyaml node as a native Python object.
py::object node_to_py(ryml::ConstNodeRef node, KeyCache& keys);  // fwd (mutual recursion)

[[nodiscard]] inline py::object map_to_py(ryml::ConstNodeRef node, KeyCache& keys) {
    py::dict out;
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string_view k = to_sv(child.key());
        // String keys (quoted, or unquoted-but-plain) go through the interning
        // cache; typed keys (ints, bools, ...) resolve like any scalar.
        py::object key = (child.is_key_quoted() || scalar_is_string(k))
                             ? py::object(keys.get(k))
                             : scalar_to_py(k, false);
        out[key] = node_to_py(child, keys);
    }
    return out;
}

[[nodiscard]] inline py::object seq_to_py(ryml::ConstNodeRef node, KeyCache& keys) {
    py::list out;
    for (ryml::ConstNodeRef child : node.children()) out.append(node_to_py(child, keys));
    return out;
}

inline py::object node_to_py(ryml::ConstNodeRef node, KeyCache& keys) {
    if (node.is_stream()) return seq_to_py(node, keys);   // multi-doc stream -> list of docs
    if (node.is_map()) return map_to_py(node, keys);
    if (node.is_seq()) return seq_to_py(node, keys);
    if (node.has_val()) return scalar_to_py(to_sv(node.val()), node.is_val_quoted());
    return py::none();                                    // empty document
}

// ── Hostile YAML: bounded before it can cost the process ───────────────────
// rapidyaml parses, resolves and emits recursively, and so do the materialiser
// and the writer here; and resolving aliases COPIES what they name, so 400
// bytes of nested anchors become 10^7 nodes (2.5 GB). Every YAML document,
// a file or markdown front matter, passes three checks first.

/// The deepest a YAML document may nest, read or written: well inside what
/// rapidyaml's parser survives on a 1 MB thread stack (it dies between 5,000
/// and 10,000 flow levels), near simdjson's 1,024 and above toml++'s 256.
inline constexpr std::size_t yaml_max_depth = 1000;

/// How deep `text`'s flow collections ([ ], { }) nest, before any parse: the
/// parser itself would overflow the stack on the input it is protected from.
/// Read as YAML reads it — in a flow collection a quote opens a quoted scalar
/// only where a token starts, and `#` opens a comment only after whitespace —
/// so brackets cannot be hidden from the count; brackets inside block-context
/// strings are counted too, an overcount that can only refuse.
[[nodiscard]] constexpr std::size_t yaml_flow_depth(std::string_view text) noexcept {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    const auto token_start = [&](std::size_t i) {
        while (i > 0 && space(text[i - 1])) --i;
        return i == 0 || text[i - 1] == '[' || text[i - 1] == '{' || text[i - 1] == ',' || text[i - 1] == ':';
    };
    std::size_t depth = 0, deepest = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '#' && (i == 0 || space(text[i - 1]))) {
            while (i + 1 < text.size() && text[i + 1] != '\n' && text[i + 1] != '\r') ++i;
        } else if (depth > 0 && (c == '\'' || c == '"') && token_start(i)) {
            for (++i; i < text.size(); ++i) {   // to the closing quote
                if (c == '"' && text[i] == '\\') {
                    ++i;
                } else if (text[i] == c) {
                    if (c == '\'' && i + 1 < text.size() && text[i + 1] == '\'') ++i;   // '' is a quote inside
                    else break;
                }
            }
        } else if (c == '[' || c == '{') {
            deepest = std::max(deepest, ++depth);
        } else if ((c == ']' || c == '}') && depth > 0) {
            --depth;
        }
    }
    return deepest;
}

/// A parsed tree's nesting depth, and how many nodes resolving its aliases
/// would make (saturating at `cap`) — counted without resolving, and without
/// recursion: a reference weighs what its anchor's subtree weighs, the anchor
/// being the latest of that name before it in document order.
struct yaml_shape {
    std::size_t depth = 0;
    std::uint64_t resolved = 0;
    bool recursive_alias = false;
};
[[nodiscard]] inline yaml_shape yaml_shape_of(const ryml::Tree& t, std::uint64_t cap) {
    yaml_shape out;
    if (t.empty()) return out;
    std::vector<std::uint64_t> weight(t.capacity(), 0);
    std::vector<bool> done(t.capacity(), false);
    std::unordered_map<std::string_view, ryml::id_type> anchors;
    const auto name = [](ryml::csubstr s) { return std::string_view(s.data(), s.size()); };
    struct frame { ryml::id_type node; ryml::id_type next_child; std::size_t depth; };
    std::vector<frame> stack{{t.root_id(), t.first_child(t.root_id()), 1}};
    const auto enter = [&](ryml::id_type n) {   // pre-order: an anchor is defined where it appears
        if (t.has_key_anchor(n)) anchors[name(t.key_anchor(n))] = n;
        if (t.has_val_anchor(n)) anchors[name(t.val_anchor(n))] = n;
    };
    enter(t.root_id());
    while (!stack.empty()) {
        frame& f = stack.back();
        out.depth = std::max(out.depth, f.depth);
        if (f.next_child != ryml::NONE) {
            const ryml::id_type c = f.next_child;
            f.next_child = t.next_sibling(c);
            enter(c);
            stack.push_back({c, t.first_child(c), f.depth + 1});
            continue;
        }
        const ryml::id_type n = f.node;
        std::uint64_t w = 1;
        for (ryml::id_type c = t.first_child(n); c != ryml::NONE; c = t.next_sibling(c)) w = std::min(cap, w + weight[c]);
        for (const bool key : {true, false}) {   // a reference weighs its anchor's subtree
            if (!(key ? t.is_key_ref(n) : t.is_val_ref(n))) continue;
            const auto it = anchors.find(name(key ? t.key_ref(n) : t.val_ref(n)));
            if (it == anchors.end()) continue;   // unknown: resolve() reports it
            if (!done[it->second]) {             // an alias inside what it names
                out.recursive_alias = true;
                continue;
            }
            w = std::min(cap, w + weight[it->second]);
        }
        weight[n] = w;
        done[n] = true;
        stack.pop_back();
    }
    out.resolved = weight[t.root_id()];
    return out;
}

// Text -> tree, with no Python involved, so callers run it with the GIL
// released. A parse error names `origin` (the file, or what the text is).
// Refused before they cost anything: nesting deeper than yaml_max_depth, an
// alias inside what it names, and aliases that would resolve to more than
// 100,000 nodes or 100 times the document's own, whichever is more.
[[nodiscard]] inline ryml::Tree parse_yaml(std::string_view text, std::string_view origin) {
    ensure_throwing_callbacks();
    const auto refuse = [&](const std::string& why) {
        throw std::runtime_error("YAML refused: " + why + " in " + std::string(origin));
    };
    if (yaml_flow_depth(text) > yaml_max_depth) {
        refuse("its flow collections nest deeper than " + std::to_string(yaml_max_depth) + " levels");
    }
    ryml::Tree tree;
    try {
        tree = ryml::parse_in_arena(ryml::csubstr(text.data(), text.size()));
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(std::string(e.what()) + " in " + std::string(origin));
    }
    const std::uint64_t budget = std::max<std::uint64_t>(100'000, 100 * static_cast<std::uint64_t>(tree.size()));
    const yaml_shape shape = yaml_shape_of(tree, budget + 1);
    if (shape.depth > yaml_max_depth) refuse("it nests deeper than " + std::to_string(yaml_max_depth) + " levels");
    if (shape.recursive_alias) refuse("an alias names a node that contains it");
    if (shape.resolved > budget) {
        refuse("its aliases would resolve to more than " + std::to_string(budget) + " nodes (it has " +
               std::to_string(tree.size()) + "): an alias bomb");
    }
    try {
        tree.resolve();   // expand anchors / *aliases: an unknown alias is a parse error too
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(std::string(e.what()) + " in " + std::string(origin));
    }
    return tree;
}

// File I/O and parsing run with the GIL RELEASED (pure C++; the throwing ryml
// callback is GIL-free too), so reads scale across Python threads. Only the
// materialisation into Python objects reacquires the GIL.
[[nodiscard]] inline py::object load_yaml(const file& f, KeyCache& keys) {
    ryml::Tree tree;
    {
        py::gil_scoped_release nogil;
        const std::string bytes = f.read_bytes();
        require_utf8(bytes, f.fspath());
        tree = parse_yaml(bytes, f.fspath());
    }
    return node_to_py(tree.crootref(), keys);
}

// The same from text in memory (front matter inside a markdown file).
[[nodiscard]] inline py::object loads_yaml(std::string_view text, std::string_view origin, KeyCache& keys) {
    ryml::Tree tree;
    {
        py::gil_scoped_release nogil;
        tree = parse_yaml(text, origin);
    }
    return node_to_py(tree.crootref(), keys);
}

// ── Write side: Python object -> ryml tree -> YAML / JSON text ─────────────
// Strings are double-quoted exactly when an unquoted spelling would read back
// typed (scalar_is_string() is false) — the same constexpr classifiers that
// gate reading also guarantee the round-trip.

// Serialise a scalar into the tree arena, returning the stored csubstr.
[[nodiscard]] inline ryml::csubstr arena_sv(ryml::Tree& tree, std::string_view s) {
    return tree.to_arena(ryml::csubstr(s.data(), s.size()));
}

inline void py_to_node(ryml::Tree& tree, ryml::NodeRef node, py::handle obj, bool json_mode, std::size_t depth = 1) {
    if (depth > yaml_max_depth) {   // the same bound reading has: what is written reads back
        throw py::value_error(std::string(json_mode ? "json" : "yaml") + " write: the value nests deeper than " +
                              std::to_string(yaml_max_depth) + " levels");
    }
    auto set_scalar = [&](std::string_view text, bool quote) {
        node.set_val(arena_sv(tree, text));
        if (quote) node |= ryml::VAL_DQUO;
    };

    if (obj.is_none()) {
        node.set_val("null");
        return;
    }
    if (py::isinstance<py::bool_>(obj)) {   // before int: bool subclasses int
        node.set_val(obj.cast<bool>() ? "true" : "false");
        return;
    }
    if (py::isinstance<py::int_>(obj)) {
        set_scalar(py::str(obj).cast<std::string>(), false);
        return;
    }
    if (py::isinstance<py::float_>(obj)) {
        const double d = obj.cast<double>();
        if (std::isinf(d) || std::isnan(d)) {
            if (json_mode) {
                throw std::invalid_argument("json cannot represent non-finite floats");
            }
            node.set_val(std::isnan(d) ? ryml::csubstr(".nan") :
                         d > 0 ? ryml::csubstr(".inf") : ryml::csubstr("-.inf"));
            return;
        }
        set_scalar(py::repr(obj).cast<std::string>(), false);
        return;
    }
    if (py::isinstance<py::str>(obj)) {
        const std::string s = obj.cast<std::string>();
        // A control character other than tab and line feed — a CR above all — is only kept
        // double-quoted (written escaped): plain and single-quoted scalars fold or drop it.
        const bool control = std::any_of(s.begin(), s.end(), [](char ch) {
            const auto c = static_cast<unsigned char>(ch);
            return (c < 0x20 && c != '\t' && c != '\n') || c == 0x7F;
        });
        set_scalar(s, json_mode || !scalar_is_string(s) || s.empty() || control);
        return;
    }
    if (py::isinstance<py::dict>(obj)) {
        node |= ryml::MAP;
        for (auto item : obj.cast<py::dict>()) {
            if (!py::isinstance<py::str>(item.first)) {
                throw py::type_error(std::string(json_mode ? "json" : "yaml") + " write: mapping keys must be str, got " +
                                     py::str(py::type::of(item.first).attr("__name__")).cast<std::string>());
            }
            const std::string k = item.first.cast<std::string>();
            ryml::NodeRef child = node.append_child();
            child.set_key(arena_sv(tree, k));
            if (json_mode || !scalar_is_string(k) || k.empty()) {
                child |= ryml::KEY_DQUO;
            }
            py_to_node(tree, child, item.second, json_mode, depth + 1);
        }
        return;
    }
    if (py::isinstance<py::list>(obj) || py::isinstance<py::tuple>(obj)) {
        node |= ryml::SEQ;
        for (auto item : obj.cast<py::sequence>()) {
            ryml::NodeRef child = node.append_child();
            py_to_node(tree, child, item, json_mode, depth + 1);
        }
        return;
    }
    throw py::type_error(std::string(json_mode ? "json" : "yaml") + " write: cannot write a value of type " +
                         py::str(py::type::of(obj).attr("__name__")).cast<std::string>());
}

// The document model is built under the GIL (it reads Python objects); emit
// runs with the GIL released.
[[nodiscard]] inline std::string dumps_ryml(py::handle obj, bool json_mode) {
    ryml::Tree tree;
    ryml::NodeRef root = tree.rootref();
    py_to_node(tree, root, obj, json_mode);
    py::gil_scoped_release nogil;
    std::string text;
    const auto opts = ryml::EmitOptions().max_depth(static_cast<ryml::id_type>(yaml_max_depth + 1));   // ryml's own default is 64
    if (json_mode) {
        ryml::emitrs_json(tree, tree.root_id(), opts, &text);
    } else {
        ryml::emitrs_yaml(tree, tree.root_id(), opts, &text);
    }
    return text;
}

inline void write_ryml(const file& f, py::handle obj, bool json_mode) {
    const std::string text = dumps_ryml(obj, json_mode);
    py::gil_scoped_release nogil;
    write_text_file(f, text);
}

}  // namespace pygim::pathlike::detail

// ── Registry entry ─────────────────────────────────────────────────────────
// Discovered by the build from this file's location (adapter/engines/*.h); the
// struct name must equal the file stem. Everything Python-facing — the
// `yamlfile` class, `.engine == "rapidyaml"`, `engine="yaml"|"yml"|"rapidyaml"`,
// the error inventories and docstrings — is derived from `info`.
namespace pygim::pathlike::engines {

struct yaml {
    static constexpr std::array<std::string_view, 2> exts{".yaml", ".yml"};
    static constexpr std::array<std::string_view, 1> aliases{"yml"};
    static constexpr engine_info info{
        .name = "yaml",
        .label = "rapidyaml",
        .doc = "YAML 1.2 (core schema) via rapidyaml: anchors, aliases and merge keys are resolved; "
               "strings that would read back typed are quoted on write.",
        .exts = exts,
        .aliases = aliases,
    };

    static py::object load(const file& f, detail::KeyCache& keys) { return detail::load_yaml(f, keys); }
    static void write(const file& f, py::handle obj) { detail::write_ryml(f, obj, /*json_mode=*/false); }
    // The text half (adapter.h TextEngine): what markdown front matter parses and writes through.
    static py::object loads(std::string_view text, std::string_view origin, detail::KeyCache& keys) {
        return detail::loads_yaml(text, origin, keys);
    }
    static std::string dumps(py::handle obj) { return detail::dumps_ryml(obj, /*json_mode=*/false); }
};

}  // namespace pygim::pathlike::engines
