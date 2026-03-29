// repository/core/format_adapter.h
// Adapter package — FormatAdapter<Backend, Format>.
//
// This is where format selection lives (D7).
// save: Python data → to_arrow() at the edge → core save(ArrowTable)
// load: core load() → RecordBatch → from_arrow() at the edge → Python data
//
// Format axis exists ONLY here — core C++ is unaware of Polars or Pandas.
// Refactored: takes shared_ptr<ConnectionPool> and passes it to Repository.

#pragma once

#include "connection_pool.h"
#include "repository.h"

#include "../../utils/logging.h"
#include <memory>
#include <string>
#include <string_view>

namespace pygim::adapter {

// ────────────────────────────────────────────────────────────────
// Format enum
// ────────────────────────────────────────────────────────────────

enum class Format { Polars, Pandas };

constexpr const char* format_name(Format f) {
    switch (f) {
        case Format::Polars: return "Polars";
        case Format::Pandas: return "Pandas";
    }
    return "Unknown";
}

// ────────────────────────────────────────────────────────────────
// FormatAdapter<Backend, Fmt>
// ────────────────────────────────────────────────────────────────

template <core::BackendPolicy Backend, Format Fmt>
class FormatAdapter {
    core::Repository<Backend> m_repo;

public:
    explicit FormatAdapter(std::shared_ptr<core::ConnectionPool<Backend>> pool)
        : m_repo(std::move(pool))
    {
        PYGIM_LOG_FMT("[FormatAdapter<%s, %s>] created\n",
                      backend_label(), format_name(Fmt));
    }

    // save: Python object → to_arrow → core save
    // Placeholder: just prints the conversion pipeline
    void save(std::string_view table_name, int bcp_workers = 1) {
        PYGIM_LOG_FMT("[FormatAdapter<%s, %s>] save()\n",
                      backend_label(), format_name(Fmt));

        if constexpr (Fmt == Format::Polars) {
            PYGIM_LOG_FMT("[FormatAdapter]   to_arrow: "
                          "polars_df.__arrow_c_stream__() → PyCapsule → "
                          "ArrowRecordBatchReader\n");
            PYGIM_LOG_FMT("[FormatAdapter]   zero-copy via Arrow C Data Interface\n");
        } else {
            PYGIM_LOG_FMT("[FormatAdapter]   to_arrow: "
                          "pa.RecordBatch.from_pandas(pandas_df)\n");
        }

        m_repo.save(table_name, bcp_workers);
    }

    // load: core load → RecordBatch → from_arrow → Python object
    // Placeholder: just prints the conversion pipeline
    void load(std::string_view source, int load_workers = 1) {
        PYGIM_LOG_FMT("[FormatAdapter<%s, %s>] load(\"%.*s\")\n",
                      backend_label(), format_name(Fmt),
                      static_cast<int>(source.size()), source.data());

        m_repo.load(source, load_workers);

        if constexpr (Fmt == Format::Polars) {
            PYGIM_LOG_FMT("[FormatAdapter]   from_arrow: "
                          "RecordBatch.__arrow_c_stream__() → PyCapsule → "
                          "pl.from_arrow()\n");
            PYGIM_LOG_FMT("[FormatAdapter]   zero-copy via Arrow C Data Interface\n");
        } else {
            PYGIM_LOG_FMT("[FormatAdapter]   from_arrow: "
                          "record_batch.to_pandas()\n");
        }
    }

    void load(core::Query const& query, int load_workers = 1) {
        PYGIM_LOG_FMT("[FormatAdapter<%s, %s>] load(query)\n",
                      backend_label(), format_name(Fmt));
        m_repo.load(query, load_workers);

        if constexpr (Fmt == Format::Polars) {
            PYGIM_LOG_FMT("[FormatAdapter]   from_arrow: pl.from_arrow()\n");
        } else {
            PYGIM_LOG_FMT("[FormatAdapter]   from_arrow: to_pandas()\n");
        }
    }

    core::Repository<Backend>& repo() { return m_repo; }
    core::Repository<Backend> const& repo() const { return m_repo; }

private:
    static constexpr const char* backend_label() {
        if constexpr (std::is_same_v<Backend, core::MssqlBackend>)
            return "Mssql";
        else
            return "Unknown";
    }
};

} // namespace pygim::adapter
