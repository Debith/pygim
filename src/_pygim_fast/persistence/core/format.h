// persistence/core/format.h
// The DataFrame library a repository hands its tables to, by the name Python gives it.
//
// CORE layer: pybind-free and constexpr.

#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "../../errors/core.h"

namespace pygim::core {

/// Output format for edge conversion (Arrow ↔ Python DataFrame).
/// Runtime member on RepositoryAdapter, not a template parameter.
enum class Format { Polars, Pandas };

/// The format named `fmt`; pygim::errors::option_error, naming both, for any other name.
constexpr Format parse_format(std::string_view fmt) {
    if (fmt == "polars") return Format::Polars;
    if (fmt == "pandas") return Format::Pandas;
    throw pygim::errors::option_error(std::string(fmt), {"polars", "pandas"});
}
static_assert(parse_format("polars") == Format::Polars && parse_format("pandas") == Format::Pandas);

/// Human-readable format label for logging/repr.
constexpr const char* format_name(Format f) {
    switch (f) {
        case Format::Polars: return "polars";
        case Format::Pandas: return "pandas";
    }
    std::unreachable();
}
static_assert(std::string_view(format_name(Format::Pandas)) == "pandas");

}  // namespace pygim::core
