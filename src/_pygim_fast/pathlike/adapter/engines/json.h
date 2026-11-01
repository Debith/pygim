#pragma once
// pathlike/adapter/engines/json.h — the simdjson engine: strict, SIMD-accelerated
// JSON reads. JSON writing goes through the shared ryml tree (see yaml.h),
// which emits JSON text directly.
//
// One file IS one engine: the descriptor at the bottom is what the build
// discovers into the registry (see ../../registry.h).

#include <array>
#include <string>
#include <string_view>

#include <pybind11/pybind11.h>

#include "../third_party/simdjson/simdjson.h"
#include "../../core.h"
#include "../common.h"
#include "../materialize.h"
#include "yaml.h"   // the shared ryml JSON emitter (write side)

namespace pygim::pathlike::detail {

// Recursively materialise a simdjson DOM element as a native Python object.
// JSON semantics are exact: object keys are strings, numbers arrive already
// typed by the parser (int64 / uint64 / double).
[[nodiscard]] inline py::object json_to_py(simdjson::dom::element el, KeyCache& keys) {
    using simdjson::dom::element_type;
    switch (el.type()) {
        case element_type::OBJECT: {
            py::dict out;
            for (auto [key, value] : simdjson::dom::object(el)) {
                out[keys.get(key)] = json_to_py(value, keys);
            }
            return out;
        }
        case element_type::ARRAY: {
            py::list out;
            for (simdjson::dom::element child : simdjson::dom::array(el)) {
                out.append(json_to_py(child, keys));
            }
            return out;
        }
        case element_type::STRING: return py::str(std::string(std::string_view(el)));
        case element_type::INT64:  return py::int_(int64_t(el));
        case element_type::UINT64: return py::int_(uint64_t(el));
        case element_type::DOUBLE: return py::float_(double(el));
        case element_type::BOOL:   return py::bool_(bool(el));
        case element_type::NULL_VALUE: return py::none();
    }
    throw std::runtime_error("json: unhandled element type");
}

/// JSON text as Python objects; `origin` names it in an error (a file's path, or "<text>").
[[nodiscard]] inline py::object decode_json(std::string bytes, std::string_view origin, KeyCache& keys) {
    simdjson::dom::parser parser;   // must outlive the element it returns
    simdjson::dom::element doc;
    {
        py::gil_scoped_release nogil;
        // Same encoding gate as YAML: UTF-16 input otherwise dies with a
        // misleading UNESCAPED_CHARS parse error instead of naming the cause.
        require_utf8(bytes, origin);
        pad_for_simdjson(bytes);
        try {
            doc = parser.parse(bytes);   // padded: parsed in place, not copied
        } catch (const simdjson::simdjson_error& e) {
            throw std::invalid_argument("JSON parse error (" + std::string(origin) + "): " + e.what());
        }
    }
    return json_to_py(doc, keys);
}

}  // namespace pygim::pathlike::detail

// ── Registry entry ─────────────────────────────────────────────────────────
namespace pygim::pathlike::engines {

struct json {
    static constexpr std::array<std::string_view, 1> exts{".json"};
    static constexpr std::array<std::string_view, 0> aliases{};
    static constexpr engine_info info{
        .name = "json",
        .label = "simdjson",
        .doc = "JSON via simdjson (strict, SIMD-accelerated); writes real JSON and rejects non-finite floats.",
        .exts = exts,
        .aliases = aliases,
    };

    static py::object decode(std::string bytes, std::string_view origin, detail::KeyCache& keys) {
        return detail::decode_json(std::move(bytes), origin, keys);
    }
    static std::string encode(py::handle obj) { return detail::emit_ryml(obj, /*json_mode=*/true); }
};

}  // namespace pygim::pathlike::engines
