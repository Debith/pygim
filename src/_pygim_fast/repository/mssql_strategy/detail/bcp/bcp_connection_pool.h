#pragma once
// BCP Connection Pool: manages M pre-connected ODBC connections for parallel
// bulk insert.  Each connection has its own SQLHENV + SQLHDBC with BCP enabled.
//
// Not thread-safe for pool management — designed for create-once, use-parallel
// (each worker owns its slot by index).

#include "bcp_api.h"
#include "../odbc_error.h"

#include <string>
#include <vector>
#include <stdexcept>

namespace pygim::bcp {

/// A pool of M independently connected ODBC connections, each BCP-enabled.
///
/// Usage:
///   BcpConnectionPool pool(conn_str, 4);
///   // Launch 4 threads, each accessing pool[i].dbc
///   // Each connection can run its own bcp_init → sendrow → done lifecycle.
///   // Pool destructor disconnects and frees all handles.
class BcpConnectionPool {
public:
    struct Connection {
        SQLHENV env{SQL_NULL_HENV};
        SQLHDBC dbc{SQL_NULL_HDBC};
        bool    connected{false};
    };

    /// Create a pool of @p pool_size connections to @p conn_str.
    /// All connections are established eagerly.
    /// @throws std::runtime_error on any ODBC allocation or connection failure.
    explicit BcpConnectionPool(const std::string& conn_str, int pool_size)
    {
        m_connections.resize(static_cast<size_t>(pool_size));
        for (int i = 0; i < pool_size; ++i) {
            auto& c = m_connections[static_cast<size_t>(i)];
            try {
                alloc_and_connect(c, conn_str, i);
            } catch (...) {
                // Clean up already-connected slots before rethrowing.
                for (int j = 0; j < i; ++j)
                    cleanup_connection(m_connections[static_cast<size_t>(j)]);
                throw;
            }
        }
    }

    ~BcpConnectionPool() noexcept {
        for (auto& c : m_connections)
            cleanup_connection(c);
    }

    // Non-copyable, non-movable (owns ODBC handles).
    BcpConnectionPool(const BcpConnectionPool&) = delete;
    BcpConnectionPool& operator=(const BcpConnectionPool&) = delete;

    [[nodiscard]] int size() const noexcept {
        return static_cast<int>(m_connections.size());
    }

    [[nodiscard]] Connection& operator[](int i) noexcept {
        return m_connections[static_cast<size_t>(i)];
    }

    [[nodiscard]] const Connection& operator[](int i) const noexcept {
        return m_connections[static_cast<size_t>(i)];
    }

private:
    std::vector<Connection> m_connections;

    static void alloc_and_connect(Connection& c,
                                  const std::string& conn_str,
                                  int index)
    {
        // 1. Allocate environment handle.
        if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &c.env) != SQL_SUCCESS)
            throw std::runtime_error(
                "BcpConnectionPool: failed to alloc env handle #" + std::to_string(index));
        SQLSetEnvAttr(c.env, SQL_ATTR_ODBC_VERSION,
                      reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);

        // 2. Allocate connection handle.
        if (SQLAllocHandle(SQL_HANDLE_DBC, c.env, &c.dbc) != SQL_SUCCESS) {
            SQLFreeHandle(SQL_HANDLE_ENV, c.env);
            c.env = SQL_NULL_HENV;
            throw std::runtime_error(
                "BcpConnectionPool: failed to alloc dbc handle #" + std::to_string(index));
        }

        // 3. Enable BCP on this connection (must be set BEFORE SQLDriverConnect).
        SQLRETURN attr_ret = SQLSetConnectAttr(
            c.dbc, SQL_COPT_SS_BCP,
            reinterpret_cast<SQLPOINTER>(SQL_BCP_ON), SQL_IS_UINTEGER);
        if (!SQL_SUCCEEDED(attr_ret)) {
            cleanup_connection(c);
            throw std::runtime_error(
                "BcpConnectionPool: failed to enable BCP on connection #"
                + std::to_string(index));
        }

        // 4. Connect.
        SQLSMALLINT outlen = 0;
        SQLRETURN ret = SQLDriverConnect(
            c.dbc, nullptr,
            const_cast<SQLCHAR*>(reinterpret_cast<const SQLCHAR*>(conn_str.c_str())),
            SQL_NTS, nullptr, 0, &outlen, SQL_DRIVER_NOPROMPT);
        if (!SQL_SUCCEEDED(ret)) {
            cleanup_connection(c);
            odbc::raise_if_error(ret, SQL_HANDLE_DBC, c.dbc,
                ("BcpConnectionPool: connect #" + std::to_string(index)).c_str());
        }
        c.connected = true;
    }

    static void cleanup_connection(Connection& c) noexcept {
        if (c.dbc != SQL_NULL_HDBC) {
            if (c.connected) SQLDisconnect(c.dbc);
            SQLFreeHandle(SQL_HANDLE_DBC, c.dbc);
            c.dbc = SQL_NULL_HDBC;
        }
        if (c.env != SQL_NULL_HENV) {
            SQLFreeHandle(SQL_HANDLE_ENV, c.env);
            c.env = SQL_NULL_HENV;
        }
        c.connected = false;
    }
};

} // namespace pygim::bcp
