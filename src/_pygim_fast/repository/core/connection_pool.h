// repository/core/connection_pool.h
// Thread-safe ConnectionPool + RAII ConnectionHandle.
//
// ConnectionPool maintains a bounded set of Backend::Connection instances.
// ConnectionHandle checks out a connection (checkout()) and returns it on
// destruction or explicit release().
// Uses std::expected<T,E> for error handling — timeout and pool-closed are
// normal control flow, not exceptional conditions.

#pragma once

#include "backend_policy.h"
#include "../../utils/logging.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pygim::core {

// ────────────────────────────────────────────────────────────────
// Error type for pool operations
// ────────────────────────────────────────────────────────────────

enum class PoolError {
    Timeout,          ///< checkout() timed out waiting for an idle connection
    PoolClosed,       ///< pool was close()'d before checkout completed
    ConnectionFailed, ///< Backend::connect() threw during pool expansion
};

constexpr const char* pool_error_name(PoolError e) {
    switch (e) {
        case PoolError::Timeout:          return "timeout";
        case PoolError::PoolClosed:       return "pool_closed";
        case PoolError::ConnectionFailed: return "connection_failed";
    }
    std::unreachable();
}

// ────────────────────────────────────────────────────────────────
// Forward declarations
// ────────────────────────────────────────────────────────────────

template <BackendPolicy Backend>
class ConnectionPool;

// ────────────────────────────────────────────────────────────────
// ConnectionHandle — RAII checkout from pool
// ────────────────────────────────────────────────────────────────

/// ConnectionHandle — RAII checkout token from ConnectionPool.
///
/// Holds exactly one borrowed Connection. On destruction (or explicit
/// release()), returns the connection to the pool. Move-only: transferring
/// ownership is safe; the source becomes invalid (m_pool set to nullptr).
///
/// Thread safety: a single handle should be used from one thread at a time.
/// The return-to-pool operation (release/destructor) is thread-safe.
template <BackendPolicy Backend>
class ConnectionHandle {
    friend class ConnectionPool<Backend>;

    ConnectionPool<Backend>*     m_pool;
    typename Backend::Connection m_connection;

    // Private: only ConnectionPool creates handles
    ConnectionHandle(ConnectionPool<Backend>* pool,
                     typename Backend::Connection conn)
        : m_pool(pool)
        , m_connection(std::move(conn))
    {}

public:
    // Move-only
    ConnectionHandle(ConnectionHandle&& other) noexcept
        : m_pool(other.m_pool)
        , m_connection(std::move(other.m_connection))
    {
        other.m_pool = nullptr;
    }

    ConnectionHandle& operator=(ConnectionHandle&& other) noexcept {
        if (this != &other) {
            if (m_pool) release();
            m_pool       = other.m_pool;
            m_connection = std::move(other.m_connection);
            other.m_pool = nullptr;
        }
        return *this;
    }

    ConnectionHandle(const ConnectionHandle&)            = delete;
    ConnectionHandle& operator=(const ConnectionHandle&) = delete;

    ~ConnectionHandle() {
        if (m_pool) release();
    }

    // Access the underlying connection
    typename Backend::Connection&       get()       { assert(m_pool); return m_connection; }
    typename Backend::Connection const& get() const { assert(m_pool); return m_connection; }

    typename Backend::Connection&       operator*()       { assert(m_pool); return m_connection; }
    typename Backend::Connection const& operator*() const { assert(m_pool); return m_connection; }

    typename Backend::Connection*       operator->()       { assert(m_pool); return &m_connection; }
    typename Backend::Connection const* operator->() const { assert(m_pool); return &m_connection; }

    [[nodiscard]] bool valid() const { return m_pool != nullptr; }
    explicit operator bool() const   { return m_pool != nullptr; }

    // Explicitly release back to pool without waiting for destructor
    void release() {
        if (m_pool) {
            PYGIM_LOG_FMT("[ConnectionHandle] release → returning to pool\n");
            m_pool->return_connection(std::move(m_connection));
            m_pool = nullptr;
        }
    }
};

// ────────────────────────────────────────────────────────────────
// ConnectionPool — thread-safe pool of Backend::Connection
// ────────────────────────────────────────────────────────────────

/// ConnectionPool — Thread-safe, bounded pool of Backend::Connection.
///
/// Solves the cost of repeated connection creation by reusing idle connections.
/// checkout() returns std::expected<ConnectionHandle, PoolError> — uses
/// expected rather than exceptions because timeout and pool-closed are normal
/// control flow.
///
/// Thread safety: all public methods are safe to call concurrently.
/// Non-copyable, non-movable (owns mutex + condition_variable).
template <BackendPolicy Backend>
class ConnectionPool {
    friend class ConnectionHandle<Backend>;

    std::string                               m_conn_str;
    std::vector<typename Backend::Connection>  m_available;
    std::size_t                               m_total_created;
    std::size_t                               m_max_size;
    int                                       m_packet_size;
    bool                                      m_closed;
    mutable std::mutex                        m_mutex;
    std::condition_variable                   m_cv;

public:
    explicit ConnectionPool(std::string_view conn_str,
                            std::size_t max_size = 8,
                            int packet_size = 16384)
        : m_conn_str(conn_str)
        , m_total_created(0)
        , m_max_size(max_size)
        , m_packet_size(packet_size)
        , m_closed(false)
    {
        m_available.reserve(max_size);
        PYGIM_LOG_FMT("[ConnectionPool] created (max_size=%zu, conn_str=\"%.*s\")\n",
                      max_size,
                      static_cast<int>(conn_str.size()), conn_str.data());
    }

    ~ConnectionPool() {
        close();
    }

    // Non-copyable, non-movable (mutex + condition_variable)
    ConnectionPool(const ConnectionPool&)            = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
    ConnectionPool(ConnectionPool&&)                 = delete;
    ConnectionPool& operator=(ConnectionPool&&)      = delete;

    /// Checkout a connection from the pool (blocking with timeout).
    ///
    /// Priority: (1) reuse idle connection, (2) create new if under max_size,
    /// (3) wait up to timeout for a returned connection.
    /// Returns PoolError::Timeout or PoolError::PoolClosed on failure.
    [[nodiscard]]
    std::expected<ConnectionHandle<Backend>, PoolError>
    checkout(std::chrono::milliseconds timeout = std::chrono::milliseconds{5000}) {
        std::unique_lock lock(m_mutex);

        PYGIM_LOG_FMT("[ConnectionPool] checkout (available=%zu, total=%zu/%zu)\n",
                      m_available.size(), m_total_created, m_max_size);

        // Fast path: pool has an idle connection
        if (!m_available.empty()) {
            auto conn = std::move(m_available.back());
            m_available.pop_back();
            lock.unlock();
            Backend::reset(conn);
            PYGIM_LOG_FMT("[ConnectionPool] checkout → reused idle connection\n");
            return ConnectionHandle<Backend>(this, std::move(conn));
        }

        // Can we create a new one?
        if (m_total_created < m_max_size) {
            ++m_total_created;
            lock.unlock();
            auto conn = Backend::connect(m_conn_str, m_packet_size);
            PYGIM_LOG_FMT("[ConnectionPool] checkout → created new connection\n");
            return ConnectionHandle<Backend>(this, std::move(conn));
        }

        // Must wait for a return
        PYGIM_LOG_FMT("[ConnectionPool] checkout → waiting (timeout=%lldms)\n",
                      static_cast<long long>(timeout.count()));

        bool got_one = m_cv.wait_for(lock, timeout, [this] {
            return !m_available.empty() || m_closed;
        });

        if (m_closed) [[unlikely]] {
            PYGIM_LOG_FMT("[ConnectionPool] checkout → pool closed\n");
            return std::unexpected(PoolError::PoolClosed);
        }

        if (!got_one || m_available.empty()) [[unlikely]] {
            PYGIM_LOG_FMT("[ConnectionPool] checkout → timeout\n");
            return std::unexpected(PoolError::Timeout);
        }

        auto conn = std::move(m_available.back());
        m_available.pop_back();
        lock.unlock();
        Backend::reset(conn);
        PYGIM_LOG_FMT("[ConnectionPool] checkout → acquired after wait\n");
        return ConnectionHandle<Backend>(this, std::move(conn));
    }

    // ── Pool statistics ──────────────────────────────────────────

    [[nodiscard]] std::size_t available() const {
        std::lock_guard lock(m_mutex);
        return m_available.size();
    }

    [[nodiscard]] std::size_t total_created() const {
        std::lock_guard lock(m_mutex);
        return m_total_created;
    }

    [[nodiscard]] std::size_t max_size() const {
        // Immutable after construction — no lock needed
        return m_max_size;
    }

    [[nodiscard]] bool closed() const {
        std::lock_guard lock(m_mutex);
        return m_closed;
    }

    [[nodiscard]] std::string_view connection_string() const {
        // Immutable after construction — no lock needed
        return m_conn_str;
    }

    // ── Shut down pool ───────────────────────────────────────────

    void close() {
        std::lock_guard lock(m_mutex);
        if (m_closed) return;

        PYGIM_LOG_FMT("[ConnectionPool] closing (%zu idle connections)\n",
                      m_available.size());

        m_closed = true;
        for (auto& conn : m_available) {
            conn.close();
        }
        m_available.clear();
        m_cv.notify_all();
    }

private:
    // Called by ConnectionHandle destructor / release
    void return_connection(typename Backend::Connection conn) {
        std::lock_guard lock(m_mutex);
        if (m_closed) {
            PYGIM_LOG_FMT("[ConnectionPool] return_connection → pool closed, discarding\n");
            conn.close();
            return;
        }
        PYGIM_LOG_FMT("[ConnectionPool] return_connection (available=%zu→%zu)\n",
                      m_available.size(), m_available.size() + 1);
        m_available.push_back(std::move(conn));
        m_cv.notify_one();
    }

};

} // namespace pygim::core
