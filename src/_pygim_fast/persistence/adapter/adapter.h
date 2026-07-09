// persistence/adapter/adapter.h
// RepositoryAdapter<Backend> — pybind11 boundary adapter for Repository<Backend>.
//
// Follows the established core/adapter pattern (cf. wiring/registry/adapter.h,
// wiring/factory/adapter.h). Single class bound in bindings.cpp. Owns core
// Repository directly — ONE hop, no intermediaries.
//
// Format (Polars/Pandas) is a runtime enum member, not a template parameter.
// This means ONE template instantiation per backend (not 2×).

#pragma once

#include "arrow_export.h"
#include "arrow_import.h"
#include "../core/connection_pool.h"
#include "../core/connection_string.h"
#include "../core/query.h"
#include "../core/repository.h"
#include "../../utils/logging.h"

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
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

// ── Save modes ──────────────────────────────────────────────────

/// Write disposition for save(): plain append (BCP insert), keyed upsert
/// (BCP staging + MERGE), or keyed insert-missing (BCP staging + anti-join).
enum class SaveMode { Append, Upsert, InsertMissing };

[[nodiscard]] inline SaveMode parse_save_mode(std::string_view mode, bool has_keys) {
    SaveMode parsed;
    if (mode == "append") parsed = SaveMode::Append;
    else if (mode == "upsert") parsed = SaveMode::Upsert;
    else if (mode == "insert_missing") parsed = SaveMode::InsertMissing;
    else throw py::value_error("Unknown save mode: '" + std::string(mode) +
                               "'. Use 'append', 'upsert', or 'insert_missing'.");
    if (parsed == SaveMode::Append && has_keys) {
        throw py::value_error("keys are only valid with mode='upsert' or 'insert_missing'");
    }
    if (parsed != SaveMode::Append && !has_keys) {
        throw py::value_error("mode='" + std::string(mode) + "' requires key columns (keys=[...])");
    }
    return parsed;
}

template <core::BackendPolicy Backend>
class SessionAdapter;

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
    bool                       m_token_auth;
    std::vector<py::function>  m_pre_transforms;
    std::vector<py::function>  m_post_transforms;

public:
    RepositoryAdapter(std::shared_ptr<core::ConnectionPool<Backend>> pool,
                      Format format,
                      int64_t batch_size = 100000,
                      const std::string& table_hint = "TABLOCK",
                      int bcp_workers = 1,
                      int64_t block_size = 4096,
                      int packet_size = 16384,
                      bool token_auth = false)
        : m_repo(std::move(pool), block_size, packet_size)
        , m_format(format)
        , m_batch_size(batch_size)
        , m_block_size(block_size)
        , m_packet_size(packet_size)
        , m_table_hint(table_hint)
        , m_bcp_workers(bcp_workers)
        , m_token_auth(token_auth)
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
                                    int packet_size = 16384,
                                    py::object access_token = py::none()) {
        // Parse the source (raw ODBC DSN or mssql+pyodbc:// URL) once, in the
        // pybind-free core; the pool owns the value object and renders it to a
        // DSN at each physical connect.
        const core::ConnectionString cs = core::ConnectionString::parse(conn_str);

        typename core::ConnectionPool<Backend>::ConnectFn connect_fn;
        const bool token_auth = !access_token.is_none();
        if (token_auth) {
            if (bcp_workers > 1) {
                throw py::value_error(
                    "access_token currently requires bcp_workers=1: parallel "
                    "workers open extra connections that would bypass token auth");
            }
            // Eager sanity checks: a wrong token TYPE or a conflicting auth
            // keyword should fail at acquire time, not at first checkout.
            if (!py::isinstance<py::str>(access_token) &&
                !py::isinstance<py::bytes>(access_token) &&
                !PyCallable_Check(access_token.ptr())) {
                throw py::type_error(
                    "access_token must be str, bytes, or a callable returning str/bytes");
            }
            // Real keyword check on the parsed attributes (no substring false
            // positives), and it works whether the source was a URL or a DSN.
            if (const auto conflicts = cs.token_conflicts(); !conflicts.empty()) {
                throw py::value_error(
                    std::string("access_token conflicts with '") +
                    std::string(core::detail::key_label(conflicts.front())) +
                    "' in the connection string; remove credential/authentication "
                    "keywords (and userinfo in a URL) when using token auth");
            }
            // Tokens expire while pooled connections persist, so the token
            // source (value or callable) is re-evaluated per physical connect.
            connect_fn = [token_source = access_token](std::string_view cs_view,
                                                       int ps) -> typename Backend::Connection {
                std::vector<unsigned char> packed;
                {
                    // The GIL may or may not be held on this path; acquire is
                    // reentrancy-safe either way.
                    py::gil_scoped_acquire gil;
                    packed = pack_access_token(token_source);
                }
                if constexpr (requires { Backend::connect_with_token(cs_view, ps, packed); }) {
                    return Backend::connect_with_token(cs_view, ps, packed);
                } else {
                    throw std::runtime_error("access_token is not supported by this backend");
                }
            };
        }
        auto pool = std::make_shared<core::ConnectionPool<Backend>>(
            cs, pool_size, packet_size, std::move(connect_fn));
        return RepositoryAdapter(std::move(pool), format,
                                 batch_size, table_hint, bcp_workers,
                                 block_size, packet_size, token_auth);
    }

    /// Convert a Python token source into the packed ACCESSTOKEN struct
    /// (4-byte LE byte-length + UTF-16-LE token). Accepts the RAW token as
    /// str/bytes — packing is done here, unlike pyodbc's attrs_before which
    /// expects the caller to pack — or a zero-argument callable returning
    /// str/bytes (invoked per physical connect, so short-lived tokens work).
    [[nodiscard]] static std::vector<unsigned char> pack_access_token(const py::object& source) {
        py::object token = source;
        if (!py::isinstance<py::str>(token) && !py::isinstance<py::bytes>(token)) {
            if (!PyCallable_Check(token.ptr())) {
                throw py::type_error(
                    "access_token must be str, bytes, or a callable returning str/bytes");
            }
            token = token();
        }
        std::string raw;
        if (py::isinstance<py::str>(token) || py::isinstance<py::bytes>(token)) {
            raw = token.cast<std::string>();
        } else {
            throw py::type_error("access_token callable must return str or bytes");
        }
        if (raw.empty()) {
            throw py::value_error("access_token must not be empty");
        }
        if (raw.find('\0') != std::string::npos) {
            throw py::value_error(
                "access_token appears to be pre-packed (pyodbc attrs_before format); "
                "pass the raw token string instead — pygim performs the packing");
        }
        for (unsigned char c : raw) {
            if (c >= 0x80) {
                throw py::value_error(
                    "access_token must be ASCII (pass the raw token; pygim performs "
                    "the UTF-16 expansion and length prefix itself)");
            }
        }
        // Pure byte-packing lives in the core value-object layer.
        return core::AccessTokenPacker::pack(raw);
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
    py::dict save(py::object data, std::string_view table_name, int bcp_workers = -1,
                  std::string_view mode = "append", std::vector<std::string> keys = {}) {
        return save_common(nullptr, std::move(data), table_name, bcp_workers, mode, keys);
    }

    /// Session-mode save: runs on the caller-held connection (one shared
    /// transaction), always single-connection BCP.
    py::dict save_on_connection(typename Backend::Connection& conn, py::object data,
                                std::string_view table_name,
                                std::string_view mode, std::vector<std::string> keys) {
        return save_common(&conn, std::move(data), table_name, /*bcp_workers=*/1, mode, keys);
    }

private:
    py::dict save_common(typename Backend::Connection* explicit_conn, py::object data,
                         std::string_view table_name, int bcp_workers,
                         std::string_view mode_str, const std::vector<std::string>& keys) {
        PYGIM_TIMED_SCOPE("RepositoryAdapter::save");
        const SaveMode mode = parse_save_mode(mode_str, !keys.empty());
        run_transforms("pre_save", m_pre_transforms);

        // Import Arrow data as Table (GIL held — needed for Python object access)
        auto table_data = import_table(data);
        int workers = (bcp_workers >= 0) ? bcp_workers : m_bcp_workers;
        if (m_token_auth && workers > 1) {
            throw py::value_error(
                "access_token currently requires bcp_workers=1: parallel workers "
                "open extra connections that would bypass token auth");
        }

        // Keyed-mode frame validation runs BEFORE the empty-frame return and
        // before any checkout: a typo'd key must fail on empty frames too,
        // and NULL key values would silently duplicate on every re-run
        // (NULL never equality-matches in the MERGE ON clause).
        if (mode != SaveMode::Append) {
            validate_frame_keys(*table_data, keys);
        }

        // Empty-frame no-op contract: no BCP session, no connection checkout,
        // zero-row metrics returned so callers need no is_empty() guard.
        if (table_data->num_rows() == 0) {
            py::dict zero = zero_metrics();
            if (mode != SaveMode::Append) zero["affected_rows"] = 0;
            run_transforms("post_save", m_post_transforms);
            return zero;
        }

        // Session saves suppress mid-save bcp_batch flushes (batch = whole
        // frame): atomicity then never depends on driver behavior for
        // batch-commits inside a manual-commit transaction.
        const int64_t batch_size = explicit_conn != nullptr
            ? std::max<int64_t>(table_data->num_rows(), 1)
            : m_batch_size;

        py::dict result;
        if (mode == SaveMode::Append) {
            auto metrics = [&] {
                py::gil_scoped_release release;
                if (explicit_conn != nullptr) {
                    return m_repo.save_on(*explicit_conn, std::move(table_data),
                                          table_name, batch_size, m_table_hint);
                }
                return m_repo.save(std::move(table_data), table_name,
                                   batch_size, m_table_hint, workers);
            }();
            result = metrics_to_dict(metrics);
        } else {
            // Keyed writes stage over one connection (local temp table
            // visibility), then apply a single set-based statement.
            const bool update_matched = (mode == SaveMode::Upsert);
            if constexpr (requires(typename Backend::Connection& c) {
                              Backend::SaveImpl::execute_keyed(c, table_data, table_name,
                                                               batch_size, update_matched, keys);
                          }) {
                auto keyed = [&] {
                    py::gil_scoped_release release;
                    if (explicit_conn != nullptr) {
                        return m_repo.save_keyed_on(*explicit_conn, std::move(table_data),
                                                    table_name, batch_size, update_matched, keys);
                    }
                    return m_repo.save_keyed(std::move(table_data), table_name,
                                             batch_size, update_matched, keys);
                }();
                result = metrics_to_dict(keyed.metrics);
                result["affected_rows"] = keyed.affected_rows;
            } else {
                throw std::runtime_error("keyed saves are not supported by this backend");
            }
        }

        run_transforms("post_save", m_post_transforms);
        return result;
    }

    /// Generic pre-checkout key validation on the Arrow frame: membership
    /// (works on empty frames — the schema is still present) and NULL-free
    /// key columns. Backend-side validation remains as defense in depth.
    static void validate_frame_keys(const arrow::Table& table,
                                    const std::vector<std::string>& keys) {
        std::string missing, nulled;
        for (const auto& key : keys) {
            auto column = table.GetColumnByName(key);
            if (column == nullptr) {
                missing += (missing.empty() ? "" : ", ") + key;
            } else if (column->null_count() > 0) {
                nulled += (nulled.empty() ? "" : ", ") + key;
            }
        }
        if (!missing.empty()) {
            throw py::value_error("key column(s) not present in the frame: " + missing);
        }
        if (!nulled.empty()) {
            throw py::value_error(
                "key column(s) contain NULLs (NULL keys never match and would "
                "duplicate on every save): " + nulled);
        }
    }

    [[nodiscard]] static py::dict zero_metrics() {
        py::dict result;
        result["total_seconds"]       = 0.0;
        result["connect_seconds"]     = 0.0;
        result["bind_seconds"]        = 0.0;
        result["row_loop_seconds"]    = 0.0;
        result["batch_flush_seconds"] = 0.0;
        result["processed_rows"]      = 0;
        result["sent_rows"]           = 0;
        result["record_batches"]      = 0;
        return result;
    }

    template <typename Metrics>
    [[nodiscard]] static py::dict metrics_to_dict(const Metrics& metrics) {
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

public:
    /// Load data from a table name or raw SQL query.
    /// Returns a Polars or Pandas DataFrame (based on format setting).
    py::object load(std::string_view source, int load_workers = 1,
                    std::string_view partition_column = "",
                    std::vector<core::QueryParam> params = {}) {
        PYGIM_TIMED_SCOPE("RepositoryAdapter::load");
        if (!params.empty()) {
            if (!source.contains(' ')) {
                throw py::value_error(
                    "params requires raw SQL with '?' markers (or use a Query); "
                    "a bare table name takes no parameters");
            }
            return load(core::Query(source, std::move(params)), load_workers,
                        partition_column);
        }
        if (m_token_auth && load_workers > 1) {
            throw py::value_error(
                "access_token currently requires load_workers=1: parallel workers "
                "open extra connections that would bypass token auth");
        }
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
        if (m_token_auth && load_workers > 1) {
            throw py::value_error(
                "access_token currently requires load_workers=1: parallel workers "
                "open extra connections that would bypass token auth");
        }
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

    /// Session-mode load: runs on the caller-held connection, single-worker.
    py::object load_on_connection(typename Backend::Connection& conn, std::string_view source,
                                  std::vector<core::QueryParam> params = {}) {
        PYGIM_TIMED_SCOPE("RepositoryAdapter::load(session)");
        if (!params.empty() && !source.contains(' ')) {
            throw py::value_error(
                "params requires raw SQL with '?' markers (or use a Query); "
                "a bare table name takes no parameters");
        }
        run_transforms("pre_load", m_pre_transforms);

        auto result = [&] {
            py::gil_scoped_release release;
            if (!params.empty()) {
                return m_repo.load_on(conn, core::Query(source, std::move(params)));
            }
            return m_repo.load_on(conn, source);
        }();

        auto df = export_table(std::move(result.table),
                               m_format == Format::Polars);

        run_transforms("post_load", m_post_transforms);
        return df;
    }

    /// Catalog description of a table: list of per-column dicts with name,
    /// type, nullability, identity/computed/default flags (design doc 2.4).
    [[nodiscard]] py::list describe(std::string_view table_name) {
        if constexpr (requires { m_repo.describe_table(table_name); }) {
            auto schema = [&] {
                py::gil_scoped_release release;
                return m_repo.describe_table(table_name);
            }();
            py::list out;
            for (const auto& col : schema) {
                py::dict d;
                d["name"]        = col.name;
                d["type"]        = col.type_name;
                d["max_length"]  = col.max_length;
                d["precision"]   = col.precision;
                d["scale"]       = col.scale;
                d["nullable"]    = col.nullable;
                d["is_identity"] = col.is_identity;
                d["is_computed"] = col.is_computed;
                d["has_default"] = col.has_default;
                out.append(std::move(d));
            }
            return out;
        } else {
            throw std::runtime_error("describe is not supported by this backend");
        }
    }

    /// TRUNCATE TABLE (design doc 2.11).
    void truncate(std::string_view table_name, typename Backend::Connection* conn = nullptr) {
        if constexpr (requires { m_repo.truncate_table(table_name); }) {
            py::gil_scoped_release release;
            if (conn != nullptr) {
                m_repo.truncate_table_on(*conn, table_name);
            } else {
                m_repo.truncate_table(table_name);
            }
        } else {
            throw std::runtime_error("truncate is not supported by this backend");
        }
    }

    /// DELETE with optional predicate + bound params; returns affected rows.
    [[nodiscard]] long long delete_rows(std::string_view table_name, const py::object& where,
                                        std::vector<core::QueryParam> params,
                                        typename Backend::Connection* conn = nullptr) {
        std::string predicate;
        if (!where.is_none()) {
            predicate = where.cast<std::string>();
        }
        if (predicate.empty() && !params.empty()) {
            throw py::value_error("params given without a where predicate");
        }
        if constexpr (requires { m_repo.delete_rows(table_name, predicate, params); }) {
            py::gil_scoped_release release;
            if (conn != nullptr) {
                return m_repo.delete_rows_on(*conn, table_name, predicate, params);
            }
            return m_repo.delete_rows(table_name, predicate, params);
        } else {
            throw std::runtime_error("delete is not supported by this backend");
        }
    }

    /// Open a caller-owned transaction session: one pooled connection,
    /// autocommit off, explicit commit()/rollback(). See SessionAdapter.
    [[nodiscard]] std::unique_ptr<SessionAdapter<Backend>> session();

    // ── Introspection ────────────────────────────────────────

    [[nodiscard]] Format format() const { return m_format; }

    [[nodiscard]] std::string repr() const {
        return std::format("DataStore(backend={}, format={}, transforms={}/{})",
                           Backend::name(), format_name(m_format),
                           m_pre_transforms.size(), m_post_transforms.size());
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

// ── SessionAdapter ──────────────────────────────────────────────

/// SessionAdapter — caller-owned transaction over one pooled connection.
///
/// Holds a checked-out connection with autocommit OFF for its lifetime, so
/// every save()/load() through the session shares one transaction that the
/// caller finishes with commit()/rollback(). Multi-table writes become
/// atomic. close() rolls back anything uncommitted, restores autocommit,
/// and returns the connection to the pool.
///
/// Python-side lifetime: bindings keep the parent DataStore alive for as
/// long as the session exists (keep_alive), and __exit__ commits on clean
/// exit / rolls back on exception.
template <core::BackendPolicy Backend>
class SessionAdapter {
    RepositoryAdapter<Backend>&      m_parent;   //!< bindings pin its lifetime
    core::ConnectionHandle<Backend>  m_handle;   //!< held for the session
    bool                             m_open{true};

public:
    SessionAdapter(RepositoryAdapter<Backend>& parent,
                   core::ConnectionHandle<Backend> handle)
        : m_parent(parent)
        , m_handle(std::move(handle)) {
        if constexpr (requires { m_handle.get().set_autocommit(false); }) {
            try {
                m_handle.get().set_autocommit(false);
            } catch (...) {
                // The connection's state is unknown; never let a suspect
                // connection re-enter the pool.
                m_handle.discard();
                throw;
            }
        } else {
            throw std::runtime_error("sessions are not supported by this backend");
        }
    }

    ~SessionAdapter() {
        try { close(); } catch (...) {}
    }

    SessionAdapter(const SessionAdapter&)            = delete;
    SessionAdapter& operator=(const SessionAdapter&) = delete;

    py::dict save(py::object data, std::string_view table_name,
                  std::string_view mode = "append", std::vector<std::string> keys = {}) {
        ensure_open();
        return m_parent.save_on_connection(m_handle.get(), std::move(data),
                                           table_name, mode, std::move(keys));
    }

    py::object load(std::string_view source, std::vector<core::QueryParam> params = {}) {
        ensure_open();
        return m_parent.load_on_connection(m_handle.get(), source, std::move(params));
    }

    /// TRUNCATE within the session transaction (atomic replace = truncate +
    /// save + commit).
    void truncate(std::string_view table_name) {
        ensure_open();
        m_parent.truncate(table_name, &m_handle.get());
    }

    /// DELETE within the session transaction; returns affected rows.
    [[nodiscard]] long long delete_rows(std::string_view table_name, const py::object& where,
                                        std::vector<core::QueryParam> params) {
        ensure_open();
        return m_parent.delete_rows(table_name, where, std::move(params), &m_handle.get());
    }

    /// Commit the current transaction. The session stays usable; the next
    /// statement implicitly opens a new transaction (autocommit stays off).
    void commit() {
        ensure_open();
        py::gil_scoped_release release;
        m_handle.get().commit();
    }

    /// Roll back the current transaction. The session stays usable.
    void rollback() {
        ensure_open();
        py::gil_scoped_release release;
        m_handle.get().rollback();
    }

    /// Roll back anything uncommitted, restore autocommit, return the
    /// connection to the pool. Idempotent; the session is unusable after.
    /// If either cleanup step fails, the connection's transaction state is
    /// unknown — pooling it would make later plain saves write into an
    /// uncommitted transaction (silent data loss) — so it is destroyed and
    /// its pool slot freed instead.
    void close() {
        if (!m_open) return;
        m_open = false;
        py::gil_scoped_release release;
        bool clean = true;
        try { m_handle.get().rollback(); } catch (...) { clean = false; }
        try { m_handle.get().set_autocommit(true); } catch (...) { clean = false; }
        if (clean) {
            m_handle.release();
        } else {
            m_handle.discard();
        }
    }

    [[nodiscard]] bool closed() const noexcept { return !m_open; }

    [[nodiscard]] std::string repr() const {
        return std::format("DataStoreSession(backend={}, {})",
                           Backend::name(), m_open ? "open" : "closed");
    }

private:
    void ensure_open() const {
        if (!m_open) {
            throw std::runtime_error("DataStoreSession is closed");
        }
    }
};

template <core::BackendPolicy Backend>
std::unique_ptr<SessionAdapter<Backend>> RepositoryAdapter<Backend>::session() {
    // Checkout can connect (network I/O) or wait up to the pool timeout for
    // a free slot: never do that holding the GIL — a waiting session() would
    // stall every Python thread, including the one about to free a slot.
    py::gil_scoped_release release;
    auto checkout = m_repo.pool()->checkout();
    if (!checkout) {
        throw std::runtime_error(std::format(
            "session: checkout failed: {}", core::pool_error_name(checkout.error())));
    }
    return std::make_unique<SessionAdapter<Backend>>(*this, std::move(*checkout));
}

} // namespace pygim::adapter
