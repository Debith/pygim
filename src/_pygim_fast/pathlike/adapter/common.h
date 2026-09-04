#pragma once
// pathlike/common.h — engine-shared plumbing: input validation and file output.
//
// Candidate for promotion into a shared _pygim_fast location once a second
// extension needs it; today the UTF-8 validator rides on simdjson, which is
// vendored inside pathlike, so it lives here.

#include <string>
#include <string_view>

#include <pybind11/pybind11.h>

#include "third_party/simdjson/simdjson.h"
#include "../core.h"

namespace pygim::pathlike::detail {

// The engines are UTF-8 only: without this gate, UTF-16 YAML input parses
// "successfully" into NUL-riddled garbage, and stray invalid bytes surface as
// confusing late errors — or not at all when they sit in a comment.
// Discovered by the encoding corpus tests; fail loudly instead. (simdjson's
// SIMD validate_utf8 is already linked in; a UTF-8 BOM is fine — every engine
// skips it, matching PyYAML/json/tomllib.)
inline void require_utf8(const std::string& bytes, const std::string& fspath) {
    const std::string_view b(bytes);
    if (b.starts_with("\xFF\xFE") || b.starts_with("\xFE\xFF") ||
        b.find('\0') != std::string_view::npos) {
        throw std::runtime_error("cannot decode " + fspath +
                                 ": input looks like UTF-16/32 or binary — "
                                 "pathlike engines require UTF-8");
    }
    if (!simdjson::validate_utf8(bytes.data(), bytes.size())) {
        throw std::runtime_error("cannot decode " + fspath +
                                 ": input is not valid UTF-8");
    }
}

inline void write_text_file(const file& f, std::string_view text) { f.write_bytes(text); }

}  // namespace pygim::pathlike::detail
