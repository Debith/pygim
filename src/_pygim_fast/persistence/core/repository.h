// persistence/core/repository.h
// Generic Repository<Backend> facade — the core data access pattern.
//
// Templated on Backend only; core layer operates on Arrow exclusively.
// save(): Backend::SaveImpl consumes Arrow data via a pooled connection.
// load(): Backend::LoadImpl drives ArrowBuilder via a pooled connection.
//
// Owns a shared_ptr<ConnectionPool<Backend>> and checks out connections
// per-operation via RAII ConnectionHandle (no long-lived connection).

#pragma once

#include "backend_policy.h"
#include "connection_pool.h"
#include "dialect.h"
#include "load_result.h"
#include "query.h"

#include "../../utils/logging.h"
#include <arrow/table.h>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pygim::core {

/// Repository<Backend> — Generic database facade operating on Arrow data.
///
/// Template on Backend only; format conversion (Polars/Pandas) is handled
/// by the adapter layer (RepositoryAdapter). Owns a shared ConnectionPool;
/// each save()/load() checks out a connection for the duration of the call.
template <BackendPolicy Backend>
class Repository {
    std::shared_ptr<ConnectionPool<Backend>> m_pool;
    typename Backend::LoadCache              m_load_cache;
    int64_t                                  m_block_size;
    int                                      m_packet_size;

public:
    explicit Repository(std::shared_ptr<ConnectionPool<Backend>> pool,
                        int64_t block_size = 4096,
                        int packet_size = 16384)
        : m_pool(std::move(pool))
        , m_block_size(block_size)
        , m_packet_size(packet_size)
    {
        PYGIM_LOG_FMT("[Repository<%s>] constructing (pool-backed)\n", backend_name());
    }

    // Static factory: creates pool + repo in one call
    [[nodiscard]]
    static Repository create(std::string_view conn_str, std::size_t pool_size = 4,
                             int64_t block_size = 4096, int packet_size = 16384) {
        auto pool = std::make_shared<ConnectionPool<Backend>>(conn_str, pool_size, packet_size);
        return Repository(std::move(pool), block_size, packet_size);
    }

    // save: checks out connection, delegates to SaveImpl with Arrow Table
    [[nodiscard]]
    auto save(std::shared_ptr<arrow::Table> table_data,
              std::string_view table_name,
              int64_t batch_size,
              const std::string& table_hint,
              int bcp_workers) {
        PYGIM_LOG_FMT("[Repository<%s>] save(table=\"%.*s\", workers=%d)\n",
                      backend_name(),
                      static_cast<int>(table_name.size()), table_name.data(),
                      bcp_workers);

        auto result = m_pool->checkout();
        if (!result) {
            throw std::runtime_error(
                std::format("Repository: checkout failed: {}", pool_error_name(result.error())));
        }
        auto handle = std::move(*result);
        return Backend::SaveImpl::execute(handle.get(), std::move(table_data),
                                          table_name, batch_size, table_hint,
                                          bcp_workers);
    }

    // load: accepts Query → checks out connection, returns Arrow Table + metrics
    [[nodiscard]]
    LoadResult load(Query const& query, int load_workers = 1,
                    std::string_view partition_column = "") {
        typename Backend::Dialect const dialect{};
        auto sql = build_sql(query, dialect);
        // Parallel range-partitioning builds its own SQL and would silently
        // drop predicates/columns/limit/params: only plain full-table scans
        // may take that path; anything else falls back to single-worker.
        std::string table_name_str = query.is_plain_table_scan()
            ? std::string(query.table()) : std::string{};
        PYGIM_LOG_FMT("[Repository<%s>] load(sql=\"%s\")\n",
                      backend_name(), sql.c_str());

        auto result = m_pool->checkout();
        if (!result) {
            throw std::runtime_error(
                std::format("Repository: checkout failed: {}", pool_error_name(result.error())));
        }
        auto handle = std::move(*result);
        return Backend::LoadImpl::execute(handle.get(), sql, load_workers,
                                          partition_column, table_name_str,
                                          m_load_cache, m_block_size,
                                          m_packet_size, query.params());
    }

    /// Load from a table name or raw SQL string.
    ///
    /// Heuristic: if source contains a space, it is treated as raw SQL
    /// and passed through. Otherwise it is treated as a table name and
    /// wrapped in SELECT * FROM [table] via the backend dialect.
    [[nodiscard]]
    LoadResult load(std::string_view source, int load_workers = 1,
                    std::string_view partition_column = "") {
        std::string sql;
        std::string table_name_str;
        if (source.contains(' ')) {
            sql = std::string(source);
            // Raw SQL — no table name available for parallel
        } else {
            table_name_str = std::string(source);
            Query q;
            q.from_table(source);
            typename Backend::Dialect const dialect{};
            sql = build_sql(q, dialect);
        }
        PYGIM_LOG_FMT("[Repository<%s>] load(source=\"%.*s\") → sql=\"%s\"\n",
                      backend_name(),
                      static_cast<int>(source.size()), source.data(),
                      sql.c_str());

        auto result = m_pool->checkout();
        if (!result) {
            throw std::runtime_error(
                std::format("Repository: checkout failed: {}", pool_error_name(result.error())));
        }
        auto handle = std::move(*result);
        return Backend::LoadImpl::execute(handle.get(), sql, load_workers,
                                          partition_column, table_name_str,
                                          m_load_cache, m_block_size,
                                          m_packet_size);
    }

    /// Keyed save (upsert / insert-missing) via pool checkout. Delegates to
    /// Backend::SaveImpl::execute_keyed; the adapter gates availability with
    /// a requires-check, so backends without keyed support never instantiate
    /// this.
    [[nodiscard]]
    auto save_keyed(std::shared_ptr<arrow::Table> table_data, std::string_view table_name,
                    int64_t batch_size, bool update_matched,
                    const std::vector<std::string>& keys) {
        auto result = m_pool->checkout();
        if (!result) {
            throw std::runtime_error(
                std::format("Repository: checkout failed: {}", pool_error_name(result.error())));
        }
        auto handle = std::move(*result);
        return Backend::SaveImpl::execute_keyed(handle.get(), std::move(table_data),
                                                table_name, batch_size, update_matched, keys);
    }

    /// Keyed save on a caller-held connection (session mode).
    [[nodiscard]]
    auto save_keyed_on(typename Backend::Connection& conn,
                       std::shared_ptr<arrow::Table> table_data, std::string_view table_name,
                       int64_t batch_size, bool update_matched,
                       const std::vector<std::string>& keys) {
        return Backend::SaveImpl::execute_keyed(conn, std::move(table_data), table_name,
                                                batch_size, update_matched, keys);
    }

    // ── Session-mode variants: operate on a caller-held connection ──
    // Used by DataStoreSession so multiple saves/loads share one connection
    // (and therefore one caller-controlled transaction).

    /// save on an explicit connection; always single-connection BCP.
    [[nodiscard]]
    auto save_on(typename Backend::Connection& conn,
                 std::shared_ptr<arrow::Table> table_data,
                 std::string_view table_name,
                 int64_t batch_size,
                 const std::string& table_hint) {
        return Backend::SaveImpl::execute(conn, std::move(table_data), table_name,
                                          batch_size, table_hint, /*bcp_workers=*/1);
    }

    /// load a Query on an explicit connection; always single-worker.
    [[nodiscard]]
    LoadResult load_on(typename Backend::Connection& conn, Query const& query) {
        typename Backend::Dialect const dialect{};
        auto sql = build_sql(query, dialect);
        return Backend::LoadImpl::execute(conn, sql, /*load_workers=*/1,
                                          /*partition_column=*/"", std::string{},
                                          m_load_cache, m_block_size, m_packet_size,
                                          query.params());
    }

    /// load on an explicit connection; always single-worker.
    [[nodiscard]]
    LoadResult load_on(typename Backend::Connection& conn, std::string_view source) {
        std::string sql;
        std::string table_name_str;
        if (source.contains(' ')) {
            sql = std::string(source);
        } else {
            table_name_str = std::string(source);
            Query q;
            q.from_table(source);
            typename Backend::Dialect const dialect{};
            sql = build_sql(q, dialect);
        }
        return Backend::LoadImpl::execute(conn, sql, /*load_workers=*/1,
                                          /*partition_column=*/"", table_name_str,
                                          m_load_cache, m_block_size, m_packet_size);
    }

    /// Catalog description of a table (delegates to the backend SaveImpl).
    [[nodiscard]]
    auto describe_table(std::string_view table_name) {
        auto handle = checkout_or_throw();
        return Backend::SaveImpl::describe_table(handle.get(), table_name);
    }

    void truncate_table(std::string_view table_name) {
        auto handle = checkout_or_throw();
        Backend::SaveImpl::execute_truncate(handle.get(), table_name);
    }

    void truncate_table_on(typename Backend::Connection& conn, std::string_view table_name) {
        Backend::SaveImpl::execute_truncate(conn, table_name);
    }

    [[nodiscard]]
    long long delete_rows(std::string_view table_name, const std::string& where,
                          const std::vector<QueryParam>& params) {
        auto handle = checkout_or_throw();
        return Backend::SaveImpl::execute_delete(handle.get(), table_name, where, params);
    }

    [[nodiscard]]
    long long delete_rows_on(typename Backend::Connection& conn, std::string_view table_name,
                             const std::string& where, const std::vector<QueryParam>& params) {
        return Backend::SaveImpl::execute_delete(conn, table_name, where, params);
    }

    [[nodiscard]] std::string_view connection_string() const {
        return m_pool->connection_string();
    }

    [[nodiscard]] std::shared_ptr<ConnectionPool<Backend>> const& pool() const { return m_pool; }

private:
    [[nodiscard]] ConnectionHandle<Backend> checkout_or_throw() {
        auto result = m_pool->checkout();
        if (!result) {
            throw std::runtime_error(
                std::format("Repository: checkout failed: {}", pool_error_name(result.error())));
        }
        return std::move(*result);
    }

    static constexpr const char* backend_name() {
        return Backend::name();
    }
};

} // namespace pygim::core
