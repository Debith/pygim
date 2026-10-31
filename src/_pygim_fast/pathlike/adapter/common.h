#pragma once
// pathlike/common.h — engine-shared plumbing: a file's bytes read for decoding, input validation.
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
inline void require_utf8(std::string_view bytes, std::string_view origin) {
    const std::string_view b(bytes);
    if (b.starts_with("\xFF\xFE") || b.starts_with("\xFE\xFF") ||
        b.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("cannot decode " + std::string(origin) +
                                 ": input looks like UTF-16/32 or binary — "
                                 "pathlike engines require UTF-8");
    }
    if (!simdjson::validate_utf8(bytes.data(), bytes.size())) {
        throw std::invalid_argument("cannot decode " + std::string(origin) +
                                 ": input is not valid UTF-8");
    }
}

/// Room past `bytes` for simdjson's padding, which its parser may read: the JSON engines parse
/// where the text lies, never copying it into a padded_string (found 2026-10-31: a second buffer as
/// large as the file). A string with the room already keeps its buffer.
inline void pad_for_simdjson(std::string& bytes) {
    if (bytes.capacity() - bytes.size() < simdjson::SIMDJSON_PADDING) bytes.reserve(bytes.size() + simdjson::SIMDJSON_PADDING);
}

/// A file's bytes for an engine to decode, with simdjson's padding past them, unfilled: one read,
/// whichever engine decodes them.
[[nodiscard]] inline std::string read_for_decoding(const file& f) {
    std::string bytes;
    const std::size_t got = f.read_bytes_into([&bytes](std::size_t n) {
        bytes.resize_and_overwrite(n + simdjson::SIMDJSON_PADDING, [](char*, std::size_t m) noexcept { return m; });
        return bytes.data();
    });
    bytes.resize(got);   // shorter: the capacity, and the padding in it, stay
    return bytes;
}

}  // namespace pygim::pathlike::detail
