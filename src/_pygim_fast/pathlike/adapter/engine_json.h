#pragma once
// pathlike/engine_json.h — the simdjson engine: strict, SIMD-accelerated JSON
// reads. JSON writing goes through the shared ryml tree (see engine_yaml.h),
// which emits JSON text directly.

#include <string>
#include <string_view>

#include <pybind11/pybind11.h>

#include "third_party/simdjson/simdjson.h"
#include "common.h"
#include "../core.h"
#include "materialize.h"

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

[[nodiscard]] inline py::object load_json(const file& f, KeyCache& keys) {
    simdjson::dom::parser parser;   // must outlive the element it returns
    simdjson::dom::element doc;
    {
        py::gil_scoped_release nogil;
        const std::string bytes = f.read_bytes();
        // Same encoding gate as YAML: UTF-16 input otherwise dies with a
        // misleading UNESCAPED_CHARS parse error instead of naming the cause.
        require_utf8(bytes, f.fspath());
        try {
            doc = parser.parse(simdjson::padded_string(bytes));
        } catch (const simdjson::simdjson_error& e) {
            throw std::runtime_error("JSON parse error (" + f.fspath() + "): " + e.what());
        }
    }
    return json_to_py(doc, keys);
}

}  // namespace pygim::pathlike::detail
