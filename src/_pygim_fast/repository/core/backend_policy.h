// repository/core/backend_policy.h
// BackendPolicy concept — the contract every database backend must satisfy.
// No concrete backends defined here — those live in strategy/<backend>/.

#pragma once

#include "dialect.h"
#include <concepts>
#include <string_view>

namespace pygim::core {

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
