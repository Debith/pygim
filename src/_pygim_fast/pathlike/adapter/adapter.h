#pragma once
// pathlike/adapter.h — the engine dispatchers.
//
// Each engine is a strategy in its own header; adding a format touches three
// places, each one line or one file:
//
//   1. core.h        — an Engine enum value + a kExtEngines table entry
//   2. engine_<x>.h  — the strategy: load_<x>() and (optionally) write_<x>()
//   3. this file     — one case in load() and one in write()
//
// Shared machinery: scalars.h (the compile-time-proven YAML 1.2 scalar rules
// and KeyCache) and common.h (UTF-8 gate, file output). core.h stays free of
// pybind11 and every vendored parser.

#include <pybind11/pybind11.h>

#include "../core.h"
#include "engine_json.h"
#include "engine_toml.h"
#include "engine_yaml.h"
#include "materialize.h"

namespace pygim::pathlike {

// Read `f` and decode it with `engine`, all native C++: YAML via rapidyaml,
// JSON via simdjson (SIMD-accelerated), TOML via toml++. `key_cache_capacity`
// bounds the per-read key-interning cache (0 disables it).
[[nodiscard]] inline py::object load(const file& f, Engine engine,
                                     std::size_t key_cache_capacity = 256) {
    detail::KeyCache keys(key_cache_capacity);
    switch (engine) {
        case Engine::Yaml: return detail::load_yaml(f, keys);
        case Engine::Json: return detail::load_json(f, keys);
        case Engine::Toml: return detail::load_toml(f, keys);
        case Engine::Unknown: break;
    }
    throw std::invalid_argument("no engine resolved for " + f.fspath());
}

// Serialise `obj` to `f` with `engine`. YAML/JSON share the ryml tree and
// accept mapping or sequence roots; TOML requires a mapping root (TOML
// documents ARE tables) and enforces its own value constraints.
inline void write(const file& f, py::handle obj, Engine engine) {
    switch (engine) {
        case Engine::Yaml: return detail::write_ryml(f, obj, /*json_mode=*/false);
        case Engine::Json: return detail::write_ryml(f, obj, /*json_mode=*/true);
        case Engine::Toml: return detail::write_toml(f, obj);
        case Engine::Unknown: break;
    }
    throw std::invalid_argument("no engine resolved for " + f.fspath());
}

}  // namespace pygim::pathlike
