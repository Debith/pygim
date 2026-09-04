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

#include <cmath>
#include <array>
#include <string>
#include <string_view>

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

// File I/O and parsing run with the GIL RELEASED (pure C++; the throwing ryml
// callback is GIL-free too), so reads scale across Python threads. Only the
// materialisation into Python objects reacquires the GIL.
[[nodiscard]] inline py::object load_yaml(const file& f, KeyCache& keys) {
    ryml::Tree tree;
    {
        py::gil_scoped_release nogil;
        ensure_throwing_callbacks();
        const std::string bytes = f.read_bytes();
        require_utf8(bytes, f.fspath());
        try {
            tree = ryml::parse_in_arena(ryml::csubstr(bytes.data(), bytes.size()));
        } catch (const std::runtime_error& e) {
            throw std::runtime_error(std::string(e.what()) + " in " + f.fspath());
        }
        tree.resolve();                             // expand anchors / *aliases
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

inline void py_to_node(ryml::Tree& tree, ryml::NodeRef node, py::handle obj, bool json_mode) {
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
        set_scalar(s, json_mode || !scalar_is_string(s) || s.empty());
        return;
    }
    if (py::isinstance<py::dict>(obj)) {
        node |= ryml::MAP;
        for (auto item : obj.cast<py::dict>()) {
            if (!py::isinstance<py::str>(item.first)) {
                throw std::invalid_argument("write: mapping keys must be str, got " +
                                            py::str(py::type::of(item.first)).cast<std::string>());
            }
            const std::string k = item.first.cast<std::string>();
            ryml::NodeRef child = node.append_child();
            child.set_key(arena_sv(tree, k));
            if (json_mode || !scalar_is_string(k) || k.empty()) {
                child |= ryml::KEY_DQUO;
            }
            py_to_node(tree, child, item.second, json_mode);
        }
        return;
    }
    if (py::isinstance<py::list>(obj) || py::isinstance<py::tuple>(obj)) {
        node |= ryml::SEQ;
        for (auto item : obj.cast<py::sequence>()) {
            ryml::NodeRef child = node.append_child();
            py_to_node(tree, child, item, json_mode);
        }
        return;
    }
    throw std::invalid_argument("write: unsupported type " +
                                py::str(py::type::of(obj)).cast<std::string>());
}

// The document model is built under the GIL (it reads Python objects); emit
// and the file write run with the GIL released.
inline void write_ryml(const file& f, py::handle obj, bool json_mode) {
    ryml::Tree tree;
    ryml::NodeRef root = tree.rootref();
    py_to_node(tree, root, obj, json_mode);
    {
        py::gil_scoped_release nogil;
        std::string text;
        if (json_mode) {
            ryml::emitrs_json(tree, tree.root_id(), &text);
        } else {
            ryml::emitrs_yaml(tree, tree.root_id(), &text);
        }
        write_text_file(f, text);
    }
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
};

}  // namespace pygim::pathlike::engines
