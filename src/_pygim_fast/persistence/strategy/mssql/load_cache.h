// repository/strategy/mssql/load_cache.h
// Persistent cache for parallel load resources.
// Lazily creates and reuses LoadConnectionPool across load() calls.
// Eliminates per-load connection establishment cost (~0.06-0.15s).

#pragma once

#include "load_connection_pool.h"
#include "../../../utils/logging.h"

#include <memory>
#include <string>
#include <string_view>

namespace pygim::strategy::mssql {

/// MssqlLoadCache — persistent pool cache for parallel load workers.
/// Owned by Repository<MssqlBackend>. Lazily creates connections on first
/// parallel load, reuses them on subsequent calls.
struct MssqlLoadCache {
    /// Ensure a load pool of the right size exists. Returns pointer to it.
    /// Recreates if conn_str or pool_size changed since last call.
    [[nodiscard]]
    LoadConnectionPool* ensure_pool(std::string_view conn_str, int pool_size,
                                    int packet_size = 16384) {
        if (m_pool && m_cached_size == pool_size && m_cached_conn_str == conn_str) {
            PYGIM_LOG_FMT("[MssqlLoadCache] reusing cached pool (%d connections)\n",
                          pool_size);
            return m_pool.get();
        }
        PYGIM_LOG_FMT("[MssqlLoadCache] creating new pool (%d connections)\n",
                      pool_size);
        m_pool = std::make_unique<LoadConnectionPool>(conn_str, pool_size, packet_size);
        m_cached_size = pool_size;
        m_cached_conn_str = std::string(conn_str);
        return m_pool.get();
    }

    /// Release all cached connections.
    void clear() noexcept {
        m_pool.reset();
        m_cached_size = 0;
        m_cached_conn_str.clear();
        PYGIM_LOG_FMT("[MssqlLoadCache] cleared\n");
    }

    /// Check if pool is currently cached.
    [[nodiscard]] bool has_pool() const noexcept { return m_pool != nullptr; }

private:
    std::unique_ptr<LoadConnectionPool> m_pool;
    int         m_cached_size{0};
    std::string m_cached_conn_str;
};

} // namespace pygim::strategy::mssql
