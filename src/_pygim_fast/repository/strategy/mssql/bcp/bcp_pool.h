#pragma once
// BCP Connection Pool: manages M pre-connected ODBC connections for parallel
// bulk insert.  Each connection has BCP enabled.  RAII cleanup.
// Connections are established in parallel to minimize setup latency.
// Not thread-safe for pool management — designed for create-once, use-parallel.

#include "bcp_api.h"
#include "../odbc_error.h"

#include <string>
#include <thread>
#include <vector>
#include <stdexcept>

namespace pygim::strategy::mssql::bcp {

class BcpConnectionPool {
public:
    struct Connection {
        SQLHENV env{SQL_NULL_HENV};
        SQLHDBC dbc{SQL_NULL_HDBC};
        bool    connected{false};
    };

    /// Create a pool of @p pool_size connections to @p conn_str.
    /// Connections are established in parallel — O(1×latency) vs O(N×latency).
    explicit BcpConnectionPool(const std::string& conn_str, int pool_size)
    {
        m_connections.resize(static_cast<size_t>(pool_size));

        if (pool_size <= 1) {
            // Single connection: skip thread overhead
            alloc_and_connect(m_connections[0], conn_str, 0);
            return;
        }

        // Parallel connection establishment
        std::vector<std::exception_ptr> errors(static_cast<size_t>(pool_size));
        std::vector<std::thread> threads;
        threads.reserve(static_cast<size_t>(pool_size));

        for (int i = 0; i < pool_size; ++i) {
            threads.emplace_back([this, &conn_str, &errors, i]() {
                try {
                    alloc_and_connect(m_connections[static_cast<size_t>(i)],
                                      conn_str, i);
                } catch (...) {
                    errors[static_cast<size_t>(i)] = std::current_exception();
                }
            });
        }

        for (auto& t : threads) t.join();

        // Check for errors; cleanup all on any failure
        for (int i = 0; i < pool_size; ++i) {
            if (errors[static_cast<size_t>(i)]) {
                for (int j = 0; j < pool_size; ++j)
                    cleanup_connection(m_connections[static_cast<size_t>(j)]);
                std::rethrow_exception(errors[static_cast<size_t>(i)]);
            }
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
            throw std::runtime_error(
                "BcpConnectionPool: failed to alloc env handle #" + std::to_string(index));
        if (!SQL_SUCCEEDED(SQLSetEnvAttr(c.env, SQL_ATTR_ODBC_VERSION,
                        reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0))) {
            SQLFreeHandle(SQL_HANDLE_ENV, c.env);
            c.env = SQL_NULL_HENV;
            throw std::runtime_error("Failed to set ODBC version");
        }

        // 2. Allocate connection handle
        if (SQLAllocHandle(SQL_HANDLE_DBC, c.env, &c.dbc) != SQL_SUCCESS) {
            SQLFreeHandle(SQL_HANDLE_ENV, c.env);
            c.env = SQL_NULL_HENV;
            throw std::runtime_error(
                "BcpConnectionPool: failed to alloc dbc handle #" + std::to_string(index));
        }

        // 3. Enable BCP (must be set BEFORE SQLDriverConnect)
        SQLRETURN attr_ret = SQLSetConnectAttr(
            c.dbc, SQL_COPT_SS_BCP,
            reinterpret_cast<SQLPOINTER>(SQL_BCP_ON), SQL_IS_UINTEGER);
        if (!SQL_SUCCEEDED(attr_ret)) {
            cleanup_connection(c);
            throw std::runtime_error(
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
