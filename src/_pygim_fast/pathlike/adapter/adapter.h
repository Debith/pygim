#pragma once
// pathlike/adapter/adapter.h — the engine contract, and the registry as Python sees it.
//
// Nothing in this file names an engine. `Engine` completes the descriptor
// contract with the Python-facing half (load/write); each engine's row of entry
// points, and the dispatch through it, are pathview.h's. Adding a format
// touches exactly one file:
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
#include <span>
#include <string>
#include <string_view>

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

// The module-level ENGINES record: an immutable, introspectable view of the
// registry for Python tooling (the .pyi generator, the tests, users).
[[nodiscard]] inline py::tuple engines_record(std::span<const engine_info* const> infos) {
    py::object record = py::module_::import("collections")
                            .attr("namedtuple")("EngineInfo", "name label extensions aliases doc");
    const auto texts = [](const sv_list& views) {
        py::list out;
        for (std::string_view v : views) out.append(std::string(v));
        return py::tuple(out);
    };
    py::list out;
    for (const engine_info* e : infos) {
        out.append(record(std::string(e->name), std::string(e->label), texts(e->exts), texts(e->aliases),
                          std::string(e->doc)));
    }
    return py::tuple(out);
}

}  // namespace pygim::pathlike
