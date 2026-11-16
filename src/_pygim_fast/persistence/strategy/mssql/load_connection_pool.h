// persistence/strategy/mssql/load_connection_pool.h
// Ephemeral connection pool for parallel load workers.
// Creates N OdbcConnection instances in parallel, RAII cleanup.
// Primary connection (already checked out) is NOT managed here —
// this pool only handles the N-1 additional worker connections.

#pragma once

#include "backend.h"
#include "../../../core/primitives/parallel/for_each.h"

#include <algorithm>
#include <string_view>
#include <vector>

namespace pygim::strategy::mssql {

class LoadConnectionPool {
public:
    /// Create @p pool_size connections to @p conn_str in parallel, one thread each. The first error
    /// is rethrown once every attempt has ended; the connections opened close with the pool.
    explicit LoadConnectionPool(std::string_view conn_str, int pool_size,
                                int packet_size = 16384)
        : m_connections(static_cast<std::size_t>(std::max(pool_size, 0)))
    {
        const std::size_t n = m_connections.size();
        primitives::parallel::for_each(n, n, [&](std::size_t i, std::size_t) { m_connections[i].open(conn_str, packet_size); });
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
