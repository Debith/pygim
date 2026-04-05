// repository/adapter/adapter.h
// RepositoryAdapter<Backend> — pybind11 boundary adapter for Repository<Backend>.
//
// Follows the established core/adapter pattern (cf. registry/adapter.h,
// factory/adapter.h). Single class bound in bindings.cpp. Owns core
// Repository directly — ONE hop, no intermediaries.
//
// Format (Polars/Pandas) is a runtime enum member, not a template parameter.
// This means ONE template instantiation per backend (not 2×).

#pragma once

#include "arrow_export.h"
#include "arrow_import.h"
#include "../core/connection_pool.h"
#include "../core/query.h"
#include "../core/repository.h"
#include "../../utils/logging.h"

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pygim::adapter {

namespace py = pybind11;

// ── Format enum ─────────────────────────────────────────────────

/// Output format for edge conversion (Arrow ↔ Python DataFrame).
/// Runtime member on RepositoryAdapter, not a template parameter.
enum class Format { Polars, Pandas };

/// Convert format string from Python boundary to enum.
/// Throws py::value_error on unknown format (prevents constexpr evaluation for invalid inputs).
constexpr Format parse_format(std::string_view fmt) {
    if (fmt == "polars") return Format::Polars;
    if (fmt == "pandas") return Format::Pandas;
    throw py::value_error("Unknown format: '" + std::string(fmt) +
                          "'. Use 'polars' or 'pandas'.");
}

/// Human-readable format label for logging/repr.
constexpr const char* format_name(Format f) {
    switch (f) {
        case Format::Polars: return "polars";
        case Format::Pandas: return "pandas";
    }
    std::unreachable();
}

// ── RepositoryAdapter ───────────────────────────────────────────

/// RepositoryAdapter<Backend> — Python-facing repository with format + transforms.
///
/// Template on Backend only (one instantiation per DB engine).
/// Format is a runtime enum member selected at construction.
/// Pre/post transforms are py::function hooks run at the Python boundary.
///
/// Mirrors Registry/Factory adapter: owns core, handles Python boundary.
template <core::BackendPolicy Backend>
class RepositoryAdapter {
    core::Repository<Backend>  m_repo;
    Format                     m_format;
    int64_t                    m_batch_size;
    int64_t                    m_block_size;
    int                        m_packet_size;
    std::string                m_table_hint;
    int                        m_bcp_workers;
    std::vector<py::function>  m_pre_transforms;
    std::vector<py::function>  m_post_transforms;

public:
    RepositoryAdapter(std::shared_ptr<core::ConnectionPool<Backend>> pool,
                      Format format,
                      int64_t batch_size = 100000,
                      const std::string& table_hint = "TABLOCK",
                      int bcp_workers = 1,
                      int64_t block_size = 4096,
                      int packet_size = 16384)
        : m_repo(std::move(pool), block_size, packet_size)
        , m_format(format)
        , m_batch_size(batch_size)
        , m_block_size(block_size)
        , m_packet_size(packet_size)
        , m_table_hint(table_hint)
        , m_bcp_workers(bcp_workers)
    {
        PYGIM_LOG_FMT("[RepositoryAdapter<%s>] created (format=%s, batch_size=%lld, hint=%s, workers=%d)\n",
                      Backend::name(), format_name(m_format),
                      static_cast<long long>(m_batch_size),
                      m_table_hint.c_str(), m_bcp_workers);
    }

    [[nodiscard]]
    static RepositoryAdapter create(std::string_view conn_str,
                                    Format format,
                                    std::size_t pool_size = 4,
                                    int64_t batch_size = 100000,
                                    const std::string& table_hint = "TABLOCK",
                                    int bcp_workers = 1,
                                    int64_t block_size = 4096,
                                    int packet_size = 16384) {
        auto pool = std::make_shared<core::ConnectionPool<Backend>>(
            conn_str, pool_size, packet_size);
        return RepositoryAdapter(std::move(pool), format,
                                 batch_size, table_hint, bcp_workers,
                                 block_size, packet_size);
    }

    // ── Transform hooks ──────────────────────────────────────

    /// Add a pre-save/pre-load transform (runs WITH GIL, before core operation).
    void add_pre_transform(py::function fn) {
        m_pre_transforms.push_back(std::move(fn));
    }

    /// Add a post-save/post-load transform (runs WITH GIL, after core operation).
    void add_post_transform(py::function fn) {
        m_post_transforms.push_back(std::move(fn));
    }

    /// Remove all pre and post transforms.
    void clear_transforms() {
        m_pre_transforms.clear();
        m_post_transforms.clear();
    }

    // ── Core operations ──────────────────────────────────────
    /// Bulk-insert data into a database table via BCP.
    ///
    /// @param data          Python object: DataFrame, RecordBatch, Table, or anything
    ///                      implementing __arrow_c_stream__.
    /// @param table_name    Target table (qualified to dbo.table if no schema).
    /// @param bcp_workers   Override worker count; -1 uses the instance default.
    /// @return py::dict with timing metrics (total/connect/bind/row_loop/
    ///         batch_flush_seconds) and row counts.
    /// @throws std::runtime_error on zero rows, ODBC errors, or unsupported types.
    py::dict save(py::object data, std::string_view table_name, int bcp_workers = -1) {
        PYGIM_TIMED_SCOPE("RepositoryAdapter::save");
        run_transforms("pre_save", m_pre_transforms);

        // Import Arrow data as Table (GIL held — needed for Python object access)
        auto table_data = import_table(data);
        int workers = (bcp_workers >= 0) ? bcp_workers : m_bcp_workers;

        // BCP pipeline (GIL released — pure C++; reacquired on IIFE return)
        auto metrics = [&] {
            py::gil_scoped_release release;
            return m_repo.save(std::move(table_data), table_name,
                               m_batch_size, m_table_hint, workers);
        }();

        run_transforms("post_save", m_post_transforms);

        // Convert metrics to Python dict (GIL held)
        py::dict result;
        result["total_seconds"]       = metrics.total_seconds;
        result["connect_seconds"]     = metrics.connect_seconds;
        result["bind_seconds"]        = metrics.bind_seconds;
        result["row_loop_seconds"]    = metrics.row_loop_seconds;
        result["batch_flush_seconds"] = metrics.batch_flush_seconds;
        result["processed_rows"]      = metrics.processed_rows;
        result["sent_rows"]           = metrics.sent_rows;
        result["record_batches"]      = metrics.record_batches;

#ifdef PYGIM_BCP_PROFILING
        {
            py::dict prof;
            const auto& p = metrics.profiler;
            prof["bind_seconds"]        = p.bind_seconds;
            prof["rebind_seconds"]      = p.rebind_seconds;
            prof["classify_seconds"]    = p.classify_seconds;
            prof["fixed_copy_seconds"]  = p.fixed_copy_seconds;
            prof["string_copy_seconds"] = p.string_copy_seconds;
            prof["sendrow_seconds"]     = p.sendrow_seconds;
            prof["mid_flush_seconds"]   = p.mid_flush_seconds;
            prof["final_flush_seconds"] = p.final_flush_seconds;
            prof["init_session_seconds"]= p.init_session_seconds;
            prof["reader_next_seconds"] = p.reader_next_seconds;
            prof["sendrow_calls"]       = p.sendrow_calls;
            prof["mid_flush_calls"]     = p.mid_flush_calls;
            prof["string_calls"]        = p.string_calls;
            prof["fixed_calls"]         = p.fixed_calls;
            prof["rebind_calls"]        = p.rebind_calls;
            prof["bind_calls"]          = p.bind_calls;
            result["profiler"]          = prof;
        }
#endif

        return result;
    }

    /// Load data from a table name or raw SQL query.
    /// Returns a Polars or Pandas DataFrame (based on format setting).
    py::object load(std::string_view source, int load_workers = 1,
                    std::string_view partition_column = "") {
        PYGIM_TIMED_SCOPE("RepositoryAdapter::load");
        run_transforms("pre_load", m_pre_transforms);

        // Release GIL for ODBC operations (pure C++)
        auto result = [&] {
            py::gil_scoped_release release;
            return m_repo.load(source, load_workers, partition_column);
        }();

        // Export Arrow Table → Python DataFrame (GIL held)
        auto df = export_table(std::move(result.table),
                               m_format == Format::Polars);

        run_transforms("post_load", m_post_transforms);
        return df;
    }

    /// Load data from a Query object.
    /// Returns a Polars or Pandas DataFrame (based on format setting).
    py::object load(core::Query const& query, int load_workers = 1,
                    std::string_view partition_column = "") {
        PYGIM_TIMED_SCOPE("RepositoryAdapter::load(query)");
        run_transforms("pre_load", m_pre_transforms);

        auto result = [&] {
            py::gil_scoped_release release;
            return m_repo.load(query, load_workers, partition_column);
        }();

        auto df = export_table(std::move(result.table),
                               m_format == Format::Polars);

        run_transforms("post_load", m_post_transforms);
        return df;
    }

    // ── Introspection ────────────────────────────────────────

    [[nodiscard]] Format format() const { return m_format; }

    [[nodiscard]] std::string repr() const {
        return std::string("Repository(backend=") + Backend::name()
             + ", format=" + format_name(m_format)
             + ", transforms=" + std::to_string(m_pre_transforms.size())
             + "/" + std::to_string(m_post_transforms.size()) + ")";
    }

private:
    static void run_transforms(const char* phase,
                               std::vector<py::function> const& transforms) {
        if (transforms.empty()) return;
        PYGIM_LOG_FMT("[RepositoryAdapter] running %zu %s transforms\n",
                      transforms.size(), phase);
        for (auto const& fn : transforms) {
            fn();
        }
    }
};

} // namespace pygim::adapter
