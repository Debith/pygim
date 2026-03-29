// repository/core/dialect.h
// DialectPolicy concept — contract for SQL dialect renderers.
// Each backend strategy provides a concrete Dialect that can render
// a Query intent into backend-specific SQL.

#pragma once

#include "query.h"
#include <concepts>
#include <string>
#include <string_view>

namespace pygim::core {

template <typename D>
concept DialectPolicy = requires(D const& d, Query const& q, std::string_view sv) {
    { d.render(q) }            -> std::same_as<std::string>;
    { d.quote_identifier(sv) } -> std::same_as<std::string>;
};

/// Render a Query using a dialect. Raw SQL queries pass through unchanged.
template <DialectPolicy D>
[[nodiscard]] inline std::string build_sql(Query const& q, D const& dialect) {
    if (q.is_raw()) return std::string(q.raw_sql());
    return dialect.render(q);
}

} // namespace pygim::core
