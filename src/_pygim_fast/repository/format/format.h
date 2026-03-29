// repository/format/format.h
// Format enum — the data frame format axis (Polars, Pandas, ...).
// Orthogonal to backend selection.

#pragma once

#include <utility>

namespace pygim::adapter {

enum class Format { Polars, Pandas };

consteval const char* format_name(Format f) {
    switch (f) {
        case Format::Polars: return "Polars";
        case Format::Pandas: return "Pandas";
        default: std::unreachable();
    }
}

} // namespace pygim::adapter
