// repository/core/repository.h
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
#include "query.h"

#include "../../utils/logging.h"
#include <arrow/record_batch.h>
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

public:
    explicit Repository(std::shared_ptr<ConnectionPool<Backend>> pool)
        : m_pool(std::move(pool))
    {
        PYGIM_LOG_FMT("[Repository<%s>] constructing (pool-backed)\n", backend_name());
    }

    // Static factory: creates pool + repo in one call
    [[nodiscard]]
    static Repository create(std::string_view conn_str, std::size_t pool_size = 4) {
        auto pool = std::make_shared<ConnectionPool<Backend>>(conn_str, pool_size);
        return Repository(std::move(pool));
    }

    // save: checks out connection, delegates to SaveImpl with Arrow data
    [[nodiscard]]
    auto save(std::shared_ptr<arrow::RecordBatchReader> reader,
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
                std::string("Repository: checkout failed: ") + pool_error_name(result.error()));
        }
        auto handle = std::move(*result);
        return Backend::SaveImpl::execute(handle.get(), std::move(reader),
                                          table_name, batch_size, table_hint,
                                          bcp_workers);
    }

    // load: accepts Query → checks out connection
    void load(Query const& query, int load_workers = 1) {
        typename Backend::Dialect const dialect{};
        auto sql = build_sql(query, dialect);
        PYGIM_LOG_FMT("[Repository<%s>] load(sql=\"%s\")\n",
                      backend_name(), sql.c_str());

        auto result = m_pool->checkout();
        if (!result) {
            throw std::runtime_error(
                std::string("Repository: checkout failed: ") + pool_error_name(result.error()));
        }
        auto handle = std::move(*result);
        Backend::LoadImpl::execute(handle.get(), sql, load_workers);
    }

    /// Load from a table name or raw SQL string.
    ///
    /// Heuristic: if source contains a space, it is treated as raw SQL
    /// and passed through. Otherwise it is treated as a table name and
    /// wrapped in SELECT * FROM [table] via the backend dialect.
    void load(std::string_view source, int load_workers = 1) {
        std::string sql;
        if (source.contains(' ')) {
            sql = std::string(source);
        } else {
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
                std::string("Repository: checkout failed: ") + pool_error_name(result.error()));
        }
        auto handle = std::move(*result);
        Backend::LoadImpl::execute(handle.get(), sql, load_workers);
    }

    [[nodiscard]] std::string_view connection_string() const {
        return m_pool->connection_string();
    }

    [[nodiscard]] std::shared_ptr<ConnectionPool<Backend>> const& pool() const { return m_pool; }

private:
    static constexpr const char* backend_name() {
        return Backend::name();
    }
};

} // namespace pygim::core
