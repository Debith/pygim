// repository/adapter/flexible_repository.h
// Adapter package — FlexibleRepository<Backend, Fmt> placeholder.
//
// Default pybind11-exposed surface.  Wraps FormatAdapter with optional
// pre/post transforms (D10: type-erased via py::function).
//
// Transform pipeline at call boundary only — not in the hot inner loop.
// If no transforms registered → delegates directly.

#pragma once

#include "format_adapter.h"

#include "../../utils/logging.h"
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace pygim::adapter {

// ────────────────────────────────────────────────────────────────
// Transform placeholder (in real code: py::function)
// ────────────────────────────────────────────────────────────────

using TransformFn = std::function<void(void)>;

// ────────────────────────────────────────────────────────────────
// FlexibleRepository<Backend, Fmt>
// ────────────────────────────────────────────────────────────────

template <core::BackendPolicy Backend, Format Fmt>
class FlexibleRepository {
    FormatAdapter<Backend, Fmt>  m_inner;
    std::vector<TransformFn>     m_pre_transforms;
    std::vector<TransformFn>     m_post_transforms;

public:
    explicit FlexibleRepository(std::string_view conn_str)
        : m_inner(conn_str)
    {
        PYGIM_LOG_FMT("[FlexibleRepository<%s, %s>] created\n",
                      backend_label(), format_name(Fmt));
    }

    void add_pre_transform(TransformFn fn) {
        PYGIM_LOG_FMT("[FlexibleRepository] add_pre_transform()\n");
        m_pre_transforms.push_back(std::move(fn));
    }

    void add_post_transform(TransformFn fn) {
        PYGIM_LOG_FMT("[FlexibleRepository] add_post_transform()\n");
        m_post_transforms.push_back(std::move(fn));
    }

    void save(std::string_view table_name, int bcp_workers = 1) {
        PYGIM_TIMED_SCOPE("FlexibleRepository::save");
        PYGIM_LOG_FMT("[FlexibleRepository] save(\"%.*s\")\n",
                      static_cast<int>(table_name.size()), table_name.data());

        PYGIM_TIMED_SUB("pre_transforms");
        run_transforms("pre_save", m_pre_transforms);
        PYGIM_TIMED_SUB("inner_save");
        m_inner.save(table_name, bcp_workers);
        PYGIM_TIMED_SUB("post_transforms");
        run_transforms("post_save", m_post_transforms);
    }

    void load(std::string_view source, int load_workers = 1) {
        PYGIM_TIMED_SCOPE("FlexibleRepository::load");
        PYGIM_LOG_FMT("[FlexibleRepository] load(\"%.*s\")\n",
                      static_cast<int>(source.size()), source.data());

        PYGIM_TIMED_SUB("pre_transforms");
        run_transforms("pre_load", m_pre_transforms);
        PYGIM_TIMED_SUB("inner_load");
        m_inner.load(source, load_workers);
        PYGIM_TIMED_SUB("post_transforms");
        run_transforms("post_load", m_post_transforms);
    }

    void load(core::Query const& query, int load_workers = 1) {
        PYGIM_TIMED_SCOPE("FlexibleRepository::load(query)");
        PYGIM_LOG_FMT("[FlexibleRepository] load(query)\n");

        PYGIM_TIMED_SUB("pre_transforms");
        run_transforms("pre_load", m_pre_transforms);
        PYGIM_TIMED_SUB("inner_load");
        m_inner.load(query, load_workers);
        PYGIM_TIMED_SUB("post_transforms");
        run_transforms("post_load", m_post_transforms);
    }

    FormatAdapter<Backend, Fmt>& adapter() { return m_inner; }

private:
    static void run_transforms(const char* phase,
                               std::vector<TransformFn> const& transforms) {
        if (transforms.empty()) {
            PYGIM_LOG_FMT("[FlexibleRepository]   %s transforms: empty → skip\n",
                          phase);
            return;
        }
        PYGIM_LOG_FMT("[FlexibleRepository]   running %zu %s transforms\n",
                      transforms.size(), phase);
        for (auto const& fn : transforms) {
            fn();
        }
    }

    static constexpr const char* backend_label() {
        if constexpr (std::is_same_v<Backend, core::MssqlBackend>)
            return "Mssql";
        else
            return "Unknown";
    }
};

} // namespace pygim::adapter
