#pragma once
// pathlike/adapter/adapter.h — the engine dispatchers, generic over the registry.
//
// Nothing in this file names an engine. `Engine` completes the descriptor
// contract with the Python-facing half (load/write); load(), write() and
// load_engines() and engines_record() are folds over whatever engine_list the build discovered
// (engine_list.h); the typed path classes are path_bindings.h's fold. Adding a format touches exactly one file:
//
//   adapter/engines/<name>.h — a struct `engines::<name>` with
//       static constexpr std::array<std::string_view, K> exts{".<ext>", ...};
//       static constexpr std::array<std::string_view, M> aliases{...};   // may be empty
//       static constexpr engine_info info{.name, .label, .doc, .exts = exts, .aliases = aliases};
//       static py::object load(const file&, detail::KeyCache&);
//       static void write(const file&, py::handle);
//       static void on_load();   // optional: what it takes from Python while the module loads
//
// Shared machinery: scalars.h (the compile-time-proven YAML 1.2 scalar rules),
// materialize.h (scalars -> Python values, KeyCache) and common.h (UTF-8 gate,
// file output). core.h and engine_list.h stay free of pybind11 and every parser.

#include <cerrno>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <pybind11/pybind11.h>

#include "../core.h"
#include "../engine_list.h"
#include "common.h"
#include "materialize.h"
#include "path_text.h"

namespace pygim::pathlike {

// The full engine contract: metadata (engine_list.h) plus the two I/O entry points.
template <class E>
concept Engine = EngineMeta<E> && requires(std::string bytes, std::string_view origin, detail::KeyCache& keys, py::handle obj) {
    { E::decode(std::move(bytes), origin, keys) } -> std::same_as<py::object>;   // text -> objects; origin names it in an error
    { E::encode(obj) } -> std::same_as<std::string>;                            // objects -> text
};

/// Whether an engine's documents are lines, which write(..., append=True) adds to a file's end.
template <class E>
inline constexpr bool appends = requires { requires E::appends; };

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
    std::string bytes;
    {
        py::gil_scoped_release nogil;
        bytes = detail::read_for_decoding(f);
    }
    out = E::decode(std::move(bytes), f.fspath(), keys);
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

// `text` (str, as UTF-8, or bytes as they are) decoded with engine `e`, in memory.
template <Engine E>
bool decode_if(const engine_info* e, std::string& bytes, detail::KeyCache& keys, py::object& out) {
    if (e != &E::info) return false;
    out = E::decode(std::move(bytes), "<text>", keys);
    return true;
}
template <Engine... Es>
[[nodiscard]] py::object decode(engine_list<Es...>, const engine_info* e, std::string bytes, std::size_t key_cache_capacity = 256) {
    detail::KeyCache keys(key_cache_capacity);
    py::object out;
    if (!(decode_if<Es>(e, bytes, keys, out) || ...)) throw std::invalid_argument("no engine resolved for <text>");
    return out;
}

// `obj` as engine `e`'s text, in memory.
template <Engine E>
bool encode_if(const engine_info* e, py::handle obj, std::string& out) {
    if (e != &E::info) return false;
    out = E::encode(obj);
    return true;
}
template <Engine... Es>
[[nodiscard]] std::string encode(engine_list<Es...>, const engine_info* e, py::handle obj) {
    std::string out;
    if (!(encode_if<Es>(e, obj, out) || ...)) throw std::invalid_argument("no engine resolved for <text>");
    return out;
}

// `obj` as engine `e`'s text, written to `f`: in place or atomically (`mode`), or added to its end
// (`append`, an engine whose documents are lines only), its missing folders made first (`parents`).
// Each engine enforces its own format constraints (TOML: mapping root, no null; JSONL: list root; ...).
template <Engine E>
bool write_if(const engine_info* e, const file& f, py::handle obj, write_mode mode, bool append, bool parents) {
    if (e != &E::info) return false;
    if (append && !appends<E>) {
        throw std::invalid_argument(std::string(E::info.name) + " cannot append: a " + std::string(E::info.name) +
                                    " file is one document (an engine whose documents are lines can: jsonl)");
    }
    const std::string text = E::encode(obj);
    py::gil_scoped_release nogil;
    if (parents) f.parent().mkdir(/*parents=*/true, /*exist_ok=*/true);
    if (append) f.append_lines(text);
    else f.write_bytes(text, mode);
    return true;
}
template <Engine... Es>
void write(engine_list<Es...>, const engine_info* e, const file& f, py::handle obj, write_mode mode = write_mode::in_place,
           bool append = false, bool parents = false) {
    const bool hit = (write_if<Es>(e, f, obj, mode, append, parents) || ...);
    if (!hit) throw std::invalid_argument("no engine resolved for " + f.fspath());
}

// A file operation the OS refused (the core's file_error) raises the OSError pathlib raises for the
// same call, made by Python's own PyErr_SetFromErrnoWithFilenameObject: it picks the subclass by
// errno — FileNotFoundError, IsADirectoryError, FileExistsError, PermissionError — so nothing here
// names them, raises the error as that subclass, and words it as os.strerror does. Set as
// PyErr_SetObject(OSError, (errno, words, filename)), the error was a PermissionError whose raised
// type was OSError, which an `except PermissionError` on Python 3.10 and older does not match, and
// MSVC's words were lowercase. Local to this module: only its own functions throw file_error.
//
// A std::filesystem call's own filesystem_error (iterdir, size, resolve, exists) is raised the same
// way, by the errno its code stands for: its portable condition, which on Windows maps the system's
// error codes to errno's (ERROR_PATH_NOT_FOUND -> ENOENT). It was a RuntimeError until 2026-11-01.
inline void register_file_errors() {
    py::register_local_exception_translator([](std::exception_ptr p) {
        if (!p) return;
        try {
            std::rethrow_exception(p);
        } catch (const file_error& e) {
            const py::object name = str_from_text(e.path);
            errno = e.code().value();   // set last: making the name may change errno
            PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, name.ptr());
        } catch (const std::filesystem::filesystem_error& e) {
            const std::error_condition condition = e.code().default_error_condition();
            const int err = condition.category() == std::generic_category() ? condition.value() : EIO;
            py::object name;   // none when the call named no path
            if (!e.path1().empty()) name = str_from_text(text_from_fs(e.path1()));
            errno = err;       // set last: making the name may change errno
            PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, name.ptr());
        } catch (const glob_unsupported& e) {   // a pattern that is not relative, as pathlib refuses it
            PyErr_SetString(PyExc_NotImplementedError, e.what());
        }
    });
}

template <Engine E>
void record_one(const py::object& record, py::list& out) {
    py::list exts, aliases;
    for (std::string_view x : E::info.exts) exts.append(std::string(x));
    for (std::string_view a : E::info.aliases) aliases.append(std::string(a));
    out.append(record(std::string(E::info.name), std::string(E::info.label), py::tuple(exts),
                      py::tuple(aliases), std::string(E::info.doc)));
}

// Each engine's on_load(), for an engine that has one: what it needs from Python before its first
// read or write (toml's: datetime's C API), taken while the module loads, never while running (#248).
template <Engine... Es>
void load_engines(engine_list<Es...>) {
    ([] {
        if constexpr (requires { Es::on_load(); }) Es::on_load();
    }(), ...);
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
