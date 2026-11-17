#pragma once
// BCP Connection Pool: manages M pre-connected ODBC connections for parallel
// bulk insert.  Each connection has BCP enabled.  RAII cleanup.
// Connections are established in parallel to minimize setup latency.
// Not thread-safe for pool management — designed for create-once, use-parallel.

#include "bcp_api.h"
#include "../odbc_error.h"
#include "../../../../core/primitives/parallel/for_each.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../../../errors/core.h"

namespace pygim::strategy::mssql::bcp {

class BcpConnectionPool {
public:
    struct Connection {
        SQLHENV env{SQL_NULL_HENV};
        SQLHDBC dbc{SQL_NULL_HDBC};
        bool    connected{false};
    };

    /// Create a pool of @p pool_size connections to @p conn_str.
    /// Connections are established in parallel, one thread each — O(1×latency) vs O(N×latency).
    /// On any failure every connection is cleaned up and the first error rethrown.
    explicit BcpConnectionPool(const std::string& conn_str, int pool_size)
        : m_connections(static_cast<size_t>(std::max(pool_size, 0)))
    {
        const size_t n = m_connections.size();
        try {
            primitives::parallel::for_each(n, n, [&](size_t i, size_t) { alloc_and_connect(m_connections[i], conn_str, static_cast<int>(i)); });
        } catch (...) {
            for (auto& c : m_connections) cleanup_connection(c);
            throw;
        }
    }

    ~BcpConnectionPool() noexcept {
        for (auto& c : m_connections)
            cleanup_connection(c);
    }

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
        // 1. Allocate environment handle
        if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &c.env) != SQL_SUCCESS)
            throw pygim::errors::backend_error(
                "BcpConnectionPool: failed to alloc env handle #" + std::to_string(index));
        if (!SQL_SUCCEEDED(SQLSetEnvAttr(c.env, SQL_ATTR_ODBC_VERSION,
                        reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0))) {
            SQLFreeHandle(SQL_HANDLE_ENV, c.env);
            c.env = SQL_NULL_HENV;
            throw pygim::errors::backend_error("Failed to set ODBC version");
        }

        // 2. Allocate connection handle
        if (SQLAllocHandle(SQL_HANDLE_DBC, c.env, &c.dbc) != SQL_SUCCESS) {
            SQLFreeHandle(SQL_HANDLE_ENV, c.env);
            c.env = SQL_NULL_HENV;
            throw pygim::errors::backend_error(
                "BcpConnectionPool: failed to alloc dbc handle #" + std::to_string(index));
        }

        // 3. Enable BCP (must be set BEFORE SQLDriverConnect)
        SQLRETURN attr_ret = SQLSetConnectAttr(
            c.dbc, SQL_COPT_SS_BCP,
            reinterpret_cast<SQLPOINTER>(SQL_BCP_ON), SQL_IS_UINTEGER);
        if (!SQL_SUCCEEDED(attr_ret)) {
            cleanup_connection(c);
            throw pygim::errors::backend_error(
                "BcpConnectionPool: failed to enable BCP on connection #"
                + std::to_string(index));
        }

        // 4. Connect
        SQLSMALLINT outlen = 0;
        SQLRETURN ret = SQLDriverConnect(
            c.dbc, nullptr,
            const_cast<SQLCHAR*>(reinterpret_cast<const SQLCHAR*>(conn_str.c_str())),
            SQL_NTS, nullptr, 0, &outlen, SQL_DRIVER_NOPROMPT);
        if (!SQL_SUCCEEDED(ret)) {
            // Collect diagnostics BEFORE cleanup destroys the handle
            auto msg = "BcpConnectionPool: connect #" + std::to_string(index);
            try {
                odbc::raise_if_error(ret, SQL_HANDLE_DBC, c.dbc, msg.c_str());
            } catch (...) {
                cleanup_connection(c);
                throw;
            }
            cleanup_connection(c);  // fallthrough: raise_if_error didn't throw (success-with-info)
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

} // namespace pygim::strategy::mssql::bcp
