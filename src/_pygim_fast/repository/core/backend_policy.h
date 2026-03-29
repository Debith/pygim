// repository/core/backend_policy.h
// BackendPolicy concept — the contract every database backend must satisfy.
// No concrete backends defined here — those live in strategy/<backend>/.

#pragma once

#include "dialect.h"
#include <concepts>
#include <string_view>

namespace pygim::core {

/// BackendPolicy — the contract every database backend must satisfy.
///
/// Required associated types:
///   Connection — movable handle to a single database session (e.g., ODBC)
///   SaveImpl   — static execute() that bulk-inserts data via Connection
///   LoadImpl   — static execute() that queries via Connection → ArrowBuilder
///   Dialect    — satisfies DialectPolicy; renders Query into backend-specific SQL
///
/// Required static functions:
///   connect(conn_str) — open and return a new Connection
///   reset(conn)       — prepare a pooled connection for reuse (reset handles, clear state)
///   name()            — human-readable backend label for logging and repr
template <typename B>
concept BackendPolicy = requires(std::string_view s, typename B::Connection& conn) {
    typename B::Connection;
    typename B::SaveImpl;
    typename B::LoadImpl;
    typename B::Dialect;
    { B::connect(s) }    -> std::same_as<typename B::Connection>;
    { B::reset(conn) }   -> std::same_as<void>;
    { conn.close() }     -> std::same_as<void>;
    { B::name() }        -> std::convertible_to<const char*>;
} && DialectPolicy<typename B::Dialect>;

} // namespace pygim::core
