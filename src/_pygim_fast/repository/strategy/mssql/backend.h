// repository/strategy/mssql/backend.h
// MssqlBackend — concrete backend trait for SQL Server via ODBC.
// Satisfies BackendPolicy concept defined in core/backend_policy.h.

#pragma once

#include "../../core/backend_policy.h"
#include "../../../utils/logging.h"
#include "dialect.h"
#include <string>
#include <string_view>

namespace pygim::strategy::mssql {

// ────────────────────────────────────────────────────────────────
// Connection handle placeholder
// ────────────────────────────────────────────────────────────────

struct OdbcConnection {
    std::string m_connection_string;

    void open(std::string_view conn_str) {
        m_connection_string = std::string(conn_str);
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
    using Dialect    = MssqlDialect;

    static constexpr const char* name() { return "mssql"; }

    static Connection connect(std::string_view conn_str) {
        PYGIM_LOG_FMT("[MssqlBackend] connect(\"%.*s\")\n",
                      static_cast<int>(conn_str.size()), conn_str.data());
        Connection conn;
        conn.open(conn_str);
        return conn;
    }

    static void reset(Connection& conn) {
        PYGIM_LOG_FMT("[MssqlBackend] reset(conn_str=\"%s\")\n",
                      conn.m_connection_string.c_str());
        // Placeholder: real impl would reset ODBC statement handles, etc.
    }
};

// NOTE: static_assert(BackendPolicy<MssqlBackend>) deferred to bindings.cpp
// because MssqlSaveImpl and MssqlLoadImpl are only forward-declared here.

} // namespace pygim::strategy::mssql
