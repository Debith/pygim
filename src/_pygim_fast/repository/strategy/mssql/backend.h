// repository/strategy/mssql/backend.h
// MssqlBackend — concrete backend trait for SQL Server via ODBC.
// Real ODBC connection with BCP support enabled before connect.
// Satisfies BackendPolicy concept defined in core/backend_policy.h.

#pragma once

#include "../../core/backend_policy.h"
#include "../../../utils/logging.h"
#include "../../../utils/core_utils.h"
#include "dialect.h"
#include "odbc_error.h"
#include "bcp/bcp_api.h"   // SQL_COPT_SS_BCP, SQL_BCP_ON

#include <format>
#include <string>
#include <string_view>

namespace pygim::strategy::mssql {

inline constexpr int kDefaultPacketSize = 16384;

/// Ensure the connection string contains a PacketSize setting.
/// If the user already specified PacketSize (or "Packet Size"), their value is preserved.
/// Otherwise, appends ";PacketSize=<default_size>".
[[nodiscard]] inline std::string ensure_packet_size(std::string_view conn_str,
                                                     int packet_size = kDefaultPacketSize)
{
    // Case-insensitive, whitespace-insensitive search for "packetsize="
    auto lower = to_lower_copy(std::string(conn_str));
    std::erase(lower, ' ');

    if (lower.contains("packetsize="))
        return std::string(conn_str);  // User specified — respect it

    std::string result(conn_str);
    if (!result.empty() && result.back() != ';')
        result += ';';
    result += std::format("PacketSize={}", packet_size);
    return result;
}

// ────────────────────────────────────────────────────────────────
// OdbcConnection — real ODBC connection (SQLHENV + SQLHDBC)
// ────────────────────────────────────────────────────────────────

/// OdbcConnection — wraps SQLHENV + SQLHDBC handles with BCP enabled.
/// RAII: close() in destructor; move-only.
struct OdbcConnection {
    SQLHENV     m_env{SQL_NULL_HENV};
    SQLHDBC     m_dbc{SQL_NULL_HDBC};
    std::string m_conn_str;
    bool        m_connected{false};

    OdbcConnection() = default;
    ~OdbcConnection() noexcept { close(); }

    // Move-only (owns ODBC handles)
    OdbcConnection(OdbcConnection&& other) noexcept
        : m_env(other.m_env), m_dbc(other.m_dbc),
          m_conn_str(std::move(other.m_conn_str)),
          m_connected(other.m_connected) {
        other.m_env = SQL_NULL_HENV;
        other.m_dbc = SQL_NULL_HDBC;
        other.m_connected = false;
    }
    OdbcConnection& operator=(OdbcConnection&& other) noexcept {
        if (this != &other) {
            close();
            m_env = other.m_env;
            m_dbc = other.m_dbc;
            m_conn_str = std::move(other.m_conn_str);
            m_connected = other.m_connected;
            other.m_env = SQL_NULL_HENV;
            other.m_dbc = SQL_NULL_HDBC;
            other.m_connected = false;
        }
        return *this;
    }
    OdbcConnection(const OdbcConnection&) = delete;
    OdbcConnection& operator=(const OdbcConnection&) = delete;

    /// Open an ODBC connection with BCP enabled.
    void open(std::string_view conn_str) {
        m_conn_str = ensure_packet_size(conn_str);

        // 1. Allocate environment handle + set ODBC 3.x
        if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_env) != SQL_SUCCESS)
            throw std::runtime_error("OdbcConnection: failed to alloc env handle");
        if (!SQL_SUCCEEDED(SQLSetEnvAttr(m_env, SQL_ATTR_ODBC_VERSION,
                        reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0))) {
            SQLFreeHandle(SQL_HANDLE_ENV, m_env);
            m_env = SQL_NULL_HENV;
            throw std::runtime_error("Failed to set ODBC version");
        }

        // 2. Allocate connection handle
        if (SQLAllocHandle(SQL_HANDLE_DBC, m_env, &m_dbc) != SQL_SUCCESS) {
            SQLFreeHandle(SQL_HANDLE_ENV, m_env);
            m_env = SQL_NULL_HENV;
            throw std::runtime_error("OdbcConnection: failed to alloc dbc handle");
        }

        // 3. Enable BCP (MUST be set BEFORE SQLDriverConnect)
        SQLRETURN attr_ret = SQLSetConnectAttr(
            m_dbc, SQL_COPT_SS_BCP,
            reinterpret_cast<SQLPOINTER>(SQL_BCP_ON), SQL_IS_UINTEGER);
        if (!SQL_SUCCEEDED(attr_ret)) {
            SQLFreeHandle(SQL_HANDLE_DBC, m_dbc);
            SQLFreeHandle(SQL_HANDLE_ENV, m_env);
            m_dbc = SQL_NULL_HDBC;
            m_env = SQL_NULL_HENV;
            throw std::runtime_error("OdbcConnection: failed to enable BCP");
        }

        // 4. Connect via SQLDriverConnect
        SQLSMALLINT outlen = 0;
        SQLRETURN ret = SQLDriverConnect(
            m_dbc, nullptr,
            const_cast<SQLCHAR*>(reinterpret_cast<const SQLCHAR*>(m_conn_str.c_str())),
            SQL_NTS, nullptr, 0, &outlen, SQL_DRIVER_NOPROMPT);
        if (!SQL_SUCCEEDED(ret)) {
            odbc::raise_if_error(ret, SQL_HANDLE_DBC, m_dbc,
                                 "OdbcConnection: SQLDriverConnect");
        }
        m_connected = true;

        PYGIM_LOG_FMT("[OdbcConnection] open() connected (BCP enabled)\n");
    }

    /// Disconnect and free ODBC handles.
    void close() noexcept {
        if (m_dbc != SQL_NULL_HDBC) {
            if (m_connected) {
                SQLDisconnect(m_dbc);
                m_connected = false;
            }
            SQLFreeHandle(SQL_HANDLE_DBC, m_dbc);
            m_dbc = SQL_NULL_HDBC;
        }
        if (m_env != SQL_NULL_HENV) {
            SQLFreeHandle(SQL_HANDLE_ENV, m_env);
            m_env = SQL_NULL_HENV;
        }
        PYGIM_LOG_FMT("[OdbcConnection] close()\n");
    }

    /// Accessor: ODBC connection handle (needed by BCP pipeline).
    [[nodiscard]] SQLHDBC dbc() const noexcept { return m_dbc; }

    /// Accessor: connection string (needed for parallel pool).
    [[nodiscard]] const std::string& conn_str() const noexcept { return m_conn_str; }

    /// Is this connection currently open?
    [[nodiscard]] bool connected() const noexcept { return m_connected; }
};

// ────────────────────────────────────────────────────────────────
// Forward declarations (defined in save_impl.h / load_impl.h)
// ────────────────────────────────────────────────────────────────

struct MssqlSaveImpl;
struct MssqlLoadImpl;

// ────────────────────────────────────────────────────────────────
// MssqlBackend — the concrete backend trait
// ────────────────────────────────────────────────────────────────

/// MssqlBackend — Concrete backend for SQL Server via ODBC.
/// Satisfies BackendPolicy: provides Connection, SaveImpl, LoadImpl, Dialect.
/// static_assert deferred to bindings.cpp where all types are fully defined.
struct MssqlBackend {
    using Connection = OdbcConnection;
    using SaveImpl   = MssqlSaveImpl;
    using LoadImpl   = MssqlLoadImpl;
    using Dialect    = MssqlDialect;

    static constexpr const char* name() { return "mssql"; }

    static Connection connect(std::string_view conn_str) {
        PYGIM_LOG_FMT("[MssqlBackend] connect()\n");
        Connection conn;
        conn.open(conn_str);
        return conn;
    }

    static void reset(Connection& conn) {
        PYGIM_LOG_FMT("[MssqlBackend] reset()\n");
        // BCP sessions are short-lived; reset is a no-op for now.
        // If a statement handle were cached, we'd SQLFreeStmt here.
        (void)conn;
    }
};

// NOTE: static_assert(BackendPolicy<MssqlBackend>) deferred to bindings.cpp
// because MssqlSaveImpl and MssqlLoadImpl are only forward-declared here.

} // namespace pygim::strategy::mssql
