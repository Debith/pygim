// persistence/strategy/mssql/load_connection_pool.h
// Ephemeral connection pool for parallel load workers.
// Creates N OdbcConnection instances in parallel, RAII cleanup.
// Primary connection (already checked out) is NOT managed here —
// this pool only handles the N-1 additional worker connections.

#pragma once

#include "backend.h"
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <stdexcept>
#include <exception>

namespace pygim::strategy::mssql {

class LoadConnectionPool {
public:
    /// Create @p pool_size connections to @p conn_str in parallel.
    explicit LoadConnectionPool(std::string_view conn_str, int pool_size,
                                int packet_size = 16384)
    {
        if (pool_size <= 0) return;

        m_connections.resize(static_cast<std::size_t>(pool_size));

        if (pool_size == 1) {
            m_connections[0].open(conn_str, packet_size);
            return;
        }

        // Parallel connection establishment
        std::vector<std::exception_ptr> errors(static_cast<std::size_t>(pool_size));
        std::vector<std::thread> threads;
        threads.reserve(static_cast<std::size_t>(pool_size));
        std::string conn_str_copy(conn_str);  // stable storage for threads

        for (int i = 0; i < pool_size; ++i) {
            threads.emplace_back([this, &conn_str_copy, &errors, i, packet_size]() {
                try {
                    m_connections[static_cast<std::size_t>(i)].open(conn_str_copy, packet_size);
                } catch (...) {
                    errors[static_cast<std::size_t>(i)] = std::current_exception();
                }
            });
        }

        for (auto& t : threads) t.join();

        // On any error, all connections close via OdbcConnection destructor
        for (int i = 0; i < pool_size; ++i) {
            if (errors[static_cast<std::size_t>(i)]) {
                std::rethrow_exception(errors[static_cast<std::size_t>(i)]);
            }
        }
    }

    ~LoadConnectionPool() = default;  // OdbcConnection RAII handles cleanup

    LoadConnectionPool(const LoadConnectionPool&) = delete;
    LoadConnectionPool& operator=(const LoadConnectionPool&) = delete;
    LoadConnectionPool(LoadConnectionPool&&) = default;
    LoadConnectionPool& operator=(LoadConnectionPool&&) = default;

    [[nodiscard]] int size() const noexcept {
        return static_cast<int>(m_connections.size());
    }

    [[nodiscard]] OdbcConnection& operator[](int i) noexcept {
        return m_connections[static_cast<std::size_t>(i)];
    }

private:
    std::vector<OdbcConnection> m_connections;
};

} // namespace pygim::strategy::mssql
