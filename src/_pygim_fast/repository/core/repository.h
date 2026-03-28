// repository/core/repository.h
// Core C++ package — Repository<Backend> facade placeholder.
//
// Template on Backend only (D7).  Core knows only Arrow.
// save(ArrowTable): Backend::SaveImpl consumes Arrow directly.
// load(source): Backend::LoadImpl drives ArrowBuilder, returns RecordBatch.

#pragma once

#include "backend_trait.h"
#include "load_impl.h"
#include "query.h"
#include "save_impl.h"

#include "../../utils/logging.h"
#include <string>
#include <string_view>

namespace pygim::core {

template <BackendPolicy Backend>
class Repository {
    typename Backend::Connection m_connection;
    std::string                  m_conn_str;

public:
    explicit Repository(std::string_view conn_str)
        : m_conn_str(conn_str)
    {
        PYGIM_LOG_FMT("[Repository<%s>] constructing\n", backend_name());
        m_connection = Backend::connect(conn_str);
    }

    // save: accepts Arrow data (placeholder — prints what it would do)
    void save(std::string_view table_name, int bcp_workers = 1) {
        PYGIM_LOG_FMT("[Repository<%s>] save(table=\"%.*s\")\n",
                      backend_name(),
                      static_cast<int>(table_name.size()), table_name.data());
        Backend::SaveImpl::execute(m_connection, table_name, bcp_workers);
    }

    // load: accepts Query or raw string → returns arrow::RecordBatch
    void load(Query const& query, int load_workers = 1) {
        auto sql = query.build();
        PYGIM_LOG_FMT("[Repository<%s>] load(sql=\"%s\")\n",
                      backend_name(), sql.c_str());
        Backend::LoadImpl::execute(m_connection, sql, load_workers);
    }

    // load from table name shortcut (D9: raw strings accepted)
    void load(std::string_view source, int load_workers = 1) {
        std::string sql;
        // If source contains whitespace, treat as raw SQL
        if (source.find(' ') != std::string_view::npos) {
            sql = std::string(source);
        } else {
            sql = "SELECT * FROM " + std::string(source);
        }
        PYGIM_LOG_FMT("[Repository<%s>] load(source=\"%.*s\") → sql=\"%s\"\n",
                      backend_name(),
                      static_cast<int>(source.size()), source.data(),
                      sql.c_str());
        Backend::LoadImpl::execute(m_connection, sql, load_workers);
    }

    std::string_view connection_string() const { return m_conn_str; }

private:
    static constexpr const char* backend_name() {
        if constexpr (std::is_same_v<Backend, MssqlBackend>)
            return "MssqlBackend";
        else
            return "UnknownBackend";
    }
};

} // namespace pygim::core
