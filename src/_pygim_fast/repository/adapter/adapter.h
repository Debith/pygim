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

#include "../core/connection_pool.h"
#include "../core/query.h"
#include "../core/repository.h"
#include "../../utils/logging.h"

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>

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
/// Throws py::value_error on unknown format.
inline Format parse_format(std::string_view fmt) {
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
    std::vector<py::function>  m_pre_transforms;
    std::vector<py::function>  m_post_transforms;

public:
    RepositoryAdapter(std::shared_ptr<core::ConnectionPool<Backend>> pool,
                      Format format)
        : m_repo(std::move(pool))
        , m_format(format)
    {
        PYGIM_LOG_FMT("[RepositoryAdapter<%s>] created (format=%s)\n",
                      Backend::name(), format_name(m_format));
    }

    [[nodiscard]]
    static RepositoryAdapter create(std::string_view conn_str,
                                    Format format,
                                    std::size_t pool_size = 4) {
        auto pool = std::make_shared<core::ConnectionPool<Backend>>(
            conn_str, pool_size);
        return RepositoryAdapter(std::move(pool), format);
    }

    // ── Transform hooks ──────────────────────────────────────

    void add_pre_transform(py::function fn) {
        m_pre_transforms.push_back(std::move(fn));
    }

    void add_post_transform(py::function fn) {
        m_post_transforms.push_back(std::move(fn));
    }

    void clear_transforms() {
        m_pre_transforms.clear();
        m_post_transforms.clear();
    }

    // ── Core operations ──────────────────────────────────────

    void save(std::string_view table_name, int bcp_workers = 1) {
        PYGIM_TIMED_SCOPE("RepositoryAdapter::save");
        run_transforms("pre_save", m_pre_transforms);
        m_repo.save(table_name, bcp_workers);
        run_transforms("post_save", m_post_transforms);
    }

    void load(std::string_view source, int load_workers = 1) {
        PYGIM_TIMED_SCOPE("RepositoryAdapter::load");
        run_transforms("pre_load", m_pre_transforms);
        m_repo.load(source, load_workers);
        run_transforms("post_load", m_post_transforms);
    }

    void load(core::Query const& query, int load_workers = 1) {
        PYGIM_TIMED_SCOPE("RepositoryAdapter::load(query)");
        run_transforms("pre_load", m_pre_transforms);
        m_repo.load(query, load_workers);
        run_transforms("post_load", m_post_transforms);
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
