// repository/core/repository.h
// Core C++ package — Repository<Backend> facade.
//
// Template on Backend only (D7).  Core knows only Arrow.
// save(ArrowTable): Backend::SaveImpl consumes Arrow directly.
// load(source): Backend::LoadImpl drives ArrowBuilder, returns RecordBatch.
//
// Refactored: Repository no longer owns a connection directly.
// It holds a shared_ptr<ConnectionPool<Backend>> and checks out
// connections per-operation via RAII ConnectionHandle.

#pragma once

#include "backend_trait.h"
#include "connection_pool.h"
#include "load_impl.h"
#include "query.h"
#include "save_impl.h"

#include "../../utils/logging.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pygim::core {

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

    // save: checks out connection, delegates to SaveImpl, handle returns on scope exit
    void save(std::string_view table_name, int bcp_workers = 1) {
        PYGIM_LOG_FMT("[Repository<%s>] save(table=\"%.*s\")\n",
                      backend_name(),
                      static_cast<int>(table_name.size()), table_name.data());

        auto result = m_pool->checkout();
        if (!result) {
            throw std::runtime_error("Repository: failed to checkout connection from pool");
        }
        auto handle = std::move(*result);
        Backend::SaveImpl::execute(handle.get(), table_name, bcp_workers);
    }

    // load: accepts Query → checks out connection
    void load(Query const& query, int load_workers = 1) {
        auto sql = query.build();
        PYGIM_LOG_FMT("[Repository<%s>] load(sql=\"%s\")\n",
                      backend_name(), sql.c_str());

        auto result = m_pool->checkout();
        if (!result) {
            throw std::runtime_error("Repository: failed to checkout connection from pool");
        }
        auto handle = std::move(*result);
        Backend::LoadImpl::execute(handle.get(), sql, load_workers);
    }

    // load from table name or raw SQL shortcut
    void load(std::string_view source, int load_workers = 1) {
        std::string sql;
        if (source.find(' ') != std::string_view::npos) {
            sql = std::string(source);
        } else {
            sql = "SELECT * FROM " + std::string(source);
        }
        PYGIM_LOG_FMT("[Repository<%s>] load(source=\"%.*s\") → sql=\"%s\"\n",
                      backend_name(),
                      static_cast<int>(source.size()), source.data(),
                      sql.c_str());

        auto result = m_pool->checkout();
        if (!result) {
            throw std::runtime_error("Repository: failed to checkout connection from pool");
        }
        auto handle = std::move(*result);
        Backend::LoadImpl::execute(handle.get(), sql, load_workers);
    }

    std::string_view connection_string() const {
        return m_pool->connection_string();
    }

    std::shared_ptr<ConnectionPool<Backend>> const& pool() const { return m_pool; }

private:
    static constexpr const char* backend_name() {
        if constexpr (std::is_same_v<Backend, MssqlBackend>)
            return "MssqlBackend";
        else
            return "UnknownBackend";
    }
};

} // namespace pygim::core
