#pragma once
// pathlike/adapter/adapter.h — the engine dispatchers, generic over the registry.
//
// Nothing in this file names an engine. `Engine` completes the descriptor
// contract with the Python-facing half (load/write); load(), write() and
// engines_record() are folds over whatever engine_list the build discovered
// (engine_list.h); the typed path classes are pathview.h's fold. Adding a format touches exactly one file:
//
//   adapter/engines/<name>.h — a struct `engines::<name>` with
//       static constexpr std::array<std::string_view, K> exts{".<ext>", ...};
//       static constexpr std::array<std::string_view, M> aliases{...};   // may be empty
//       static constexpr engine_info info{.name, .label, .doc, .exts = exts, .aliases = aliases};
//       static py::object load(const file&, detail::KeyCache&);
//       static void write(const file&, py::handle);
//
// Shared machinery: scalars.h (the compile-time-proven YAML 1.2 scalar rules),
// materialize.h (scalars -> Python values, KeyCache) and common.h (UTF-8 gate,
// file output). core.h and engine_list.h stay free of pybind11 and every parser.

#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <pybind11/pybind11.h>

#include "../core.h"
#include "../engine_list.h"
#include "materialize.h"

namespace pygim::pathlike {

// The full engine contract: metadata (engine_list.h) plus the two I/O entry points.
template <class E>
concept Engine = EngineMeta<E> && requires(const file& f, detail::KeyCache& keys, py::handle obj) {
    { E::load(f, keys) } -> std::same_as<py::object>;
    { E::write(f, obj) } -> std::same_as<void>;
};

// Instantiated per pack element by all_implemented(): a descriptor that is not
// a complete Engine fails HERE, with the diagnostic naming that descriptor
// rather than a fold expression.
template <Engine E>
struct implemented {
    static constexpr bool ok = true;
};

template <class... Es>
[[nodiscard]] constexpr bool all_implemented(engine_list<Es...>) noexcept {
    return (implemented<Es>::ok && ... && true);
}

// Read `f` with engine `e` (from engine_list::resolve), all native C++;
// `key_cache_capacity` bounds the per-read key-interning cache (0 disables it).
// Dispatch is an or-fold on the engine's identity (the address of its info):
// the first matching engine runs and short-circuits the rest. Plain folds
// rather than visit() with capturing generic lambdas — the construct set
// every compiler in the matrix handles at run time (MSVC included).
template <Engine E>
bool load_if(const engine_info* e, const file& f, detail::KeyCache& keys, py::object& out) {
    if (e != &E::info) return false;
    out = E::load(f, keys);
    return true;
}

template <Engine... Es>
[[nodiscard]] py::object load(engine_list<Es...>, const engine_info* e, const file& f,
                              std::size_t key_cache_capacity = 256) {
    detail::KeyCache keys(key_cache_capacity);
    py::object out;
    const bool hit = (load_if<Es>(e, f, keys, out) || ...);
    if (!hit) throw std::invalid_argument("no engine resolved for " + f.fspath());
    return out;
}

template <Engine E>
bool write_if(const engine_info* e, const file& f, py::handle obj) {
    if (e != &E::info) return false;
    E::write(f, obj);
    return true;
}

// Serialise `obj` to `f` with engine `e`. Each engine enforces its own format
// constraints (TOML: mapping root, no null; JSONL: list root; ...).
template <Engine... Es>
void write(engine_list<Es...>, const engine_info* e, const file& f, py::handle obj) {
    const bool hit = (write_if<Es>(e, f, obj) || ...);
    if (!hit) throw std::invalid_argument("no engine resolved for " + f.fspath());
}

// Python str / bytes / os.PathLike -> the internal path text, without the
// fs::path round trip pybind11's caster would take: str is encoded with the
// filesystem encoding on POSIX (os.fsencode, so non-UTF-8 names round-trip)
// and as UTF-8 on Windows (what text_from_fs yields there); bytes are taken as
// they are; anything else goes through os.fspath (TypeError when it cannot).
// Whether the filesystem encoding is UTF-8 (set once at module init): then a
// str's UTF-8 buffer IS os.fsencode(str) whenever it has no lone surrogates,
// and the encode step (a bytes object per call) can be skipped.
inline bool& filesystem_is_utf8() {
    static bool utf8 = false;
    return utf8;
}

// The argument's text as a view plus the object that keeps it alive.
struct text_arg {
    py::object keep;
    std::string_view view;
};
inline text_arg text_view_of_arg(py::handle obj) {
#ifndef _WIN32
    if (filesystem_is_utf8() && PyUnicode_Check(obj.ptr())) {
        py::ssize_t n = 0;
        if (const char* s = PyUnicode_AsUTF8AndSize(obj.ptr(), &n)) {
            return {py::reinterpret_borrow<py::object>(obj), std::string_view(s, static_cast<std::size_t>(n))};
        }
        PyErr_Clear();   // lone surrogates: os.fsencode's surrogateescape below is the answer
    }
#endif
    py::object p = py::reinterpret_steal<py::object>(PyOS_FSPath(obj.ptr()));   // str or bytes
    if (!p) throw py::error_already_set();
    if (PyBytes_Check(p.ptr())) {
        char* s = nullptr;
        py::ssize_t n = 0;
        if (PyBytes_AsStringAndSize(p.ptr(), &s, &n) < 0) throw py::error_already_set();
        return {std::move(p), std::string_view(s, static_cast<std::size_t>(n))};
    }
#ifdef _WIN32
    py::ssize_t n = 0;
    const char* s = PyUnicode_AsUTF8AndSize(p.ptr(), &n);
    if (!s) throw py::error_already_set();
    return {std::move(p), std::string_view(s, static_cast<std::size_t>(n))};
#else
    py::object b = py::reinterpret_steal<py::object>(PyUnicode_EncodeFSDefault(p.ptr()));
    if (!b) throw py::error_already_set();
    char* s = nullptr;
    py::ssize_t n = 0;
    if (PyBytes_AsStringAndSize(b.ptr(), &s, &n) < 0) throw py::error_already_set();
    return {std::move(b), std::string_view(s, static_cast<std::size_t>(n))};
#endif
}
inline std::string text_from_arg(py::handle obj) {
    const text_arg t = text_view_of_arg(obj);
    return std::string(t.view);
}

// Internal text -> Python str: the native byte string is decoded with the
// filesystem encoding on POSIX (os.fsdecode, so non-UTF-8 names round-trip);
// on Windows the internal text is UTF-8 already.
inline py::str str_from_text(std::string_view text) {
#ifdef _WIN32
    return py::str(text.data(), text.size());
#else
    return py::reinterpret_steal<py::str>(
        PyUnicode_DecodeFSDefaultAndSize(text.data(), static_cast<py::ssize_t>(text.size())));
#endif
}

template <Engine E>
void record_one(const py::object& record, py::list& out) {
    py::list exts, aliases;
    for (std::string_view x : E::info.exts) exts.append(std::string(x));
    for (std::string_view a : E::info.aliases) aliases.append(std::string(a));
    out.append(record(std::string(E::info.name), std::string(E::info.label), py::tuple(exts),
                      py::tuple(aliases), std::string(E::info.doc)));
}

// The module-level ENGINES record: an immutable, introspectable view of the
// registry for Python tooling (the .pyi generator, the tests, users).
template <Engine... Es>
[[nodiscard]] py::tuple engines_record(engine_list<Es...>) {
    py::object record = py::module_::import("collections")
                            .attr("namedtuple")("EngineInfo", "name label extensions aliases doc");
    py::list out;
    (record_one<Es>(record, out), ...);
    return py::tuple(out);
}

}  // namespace pygim::pathlike
