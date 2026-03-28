// repository/core/backend_trait.h
// Core C++ package — Backend concept and MssqlBackend trait.
//
// Target architecture: Repository<Backend> is templated on a single
// Backend parameter.  This header defines the concept constraint and
// the concrete MssqlBackend with its nested Connection, SaveImpl, LoadImpl.

#pragma once

#include <concepts>
#include "../../utils/logging.h"
#include <string>
#include <string_view>

namespace pygim::core {

// ────────────────────────────────────────────────────────────────
// Connection handle placeholder
// ────────────────────────────────────────────────────────────────

struct OdbcConnection {
    std::string connection_string;

    void open(std::string_view conn_str) {
        connection_string = std::string(conn_str);
        PYGIM_LOG_FMT("[OdbcConnection] open(\"%.*s\")\n",
                      static_cast<int>(conn_str.size()), conn_str.data());
    }

    void close() {
        PYGIM_LOG_FMT("[OdbcConnection] close()\n");
    }
};

// ────────────────────────────────────────────────────────────────
// Forward declarations (defined in save_impl.h / load_impl.h)
// ────────────────────────────────────────────────────────────────

struct MssqlSaveImpl;
struct MssqlLoadImpl;

// ────────────────────────────────────────────────────────────────
// MssqlBackend — the concrete backend trait
// ────────────────────────────────────────────────────────────────

struct MssqlBackend {
    using Connection = OdbcConnection;
    using SaveImpl   = MssqlSaveImpl;
    using LoadImpl   = MssqlLoadImpl;

    static Connection connect(std::string_view conn_str) {
        PYGIM_LOG_FMT("[MssqlBackend] connect(\"%.*s\")\n",
                      static_cast<int>(conn_str.size()), conn_str.data());
        Connection conn;
        conn.open(conn_str);
        return conn;
    }
};

// ────────────────────────────────────────────────────────────────
// BackendPolicy concept (C++20)
// ────────────────────────────────────────────────────────────────

template <typename B>
concept BackendPolicy = requires(B b, std::string_view s) {
    typename B::Connection;
    typename B::SaveImpl;
    typename B::LoadImpl;
    { B::connect(s) } -> std::same_as<typename B::Connection>;
};

static_assert(BackendPolicy<MssqlBackend>,
              "MssqlBackend must satisfy BackendPolicy");

} // namespace pygim::core
