// RepositoryCore: orchestration layer that dispatches operations to strategies.
// This header is pybind-free.
//
// RepositoryCore owns strategies and manages:
// - Strategy selection (first capable strategy wins)
// - Optional transformer pipeline (pre-save / post-load)
// - Optional factory (converts raw data → domain entities)
//
// Template parameter EnableTransformers compiles out transformer overhead
// when disabled (mirrors the hook pattern from RegistryCore).
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "query_dialect.h"
#include "query_intent.h"
#include "strategy.h"
#include "value_types.h"

// Forward-declare Arrow types.
namespace arrow {
class RecordBatchReader;
} // namespace arrow

namespace pygim::core {

// ---- Transformer types (pybind-free) ----------------------------------------

/// Pre-save transformer: (table, data) → transformed data.
using PreSaveTransform = std::function<RowMap(const std::string &table, const RowMap &data)>;

/// Post-load transformer: (table, result) → transformed result.
using PostLoadTransform = std::function<ResultSet(const std::string &table, ResultSet result)>;

/// Factory: converts a ResultSet row into a domain-specific RowMap.
using RowFactory = std::function<RowMap(const std::string &table, const RowMap &raw)>;

// ---- RepositoryCore ---------------------------------------------------------

class RepositoryCore {
public:
    explicit RepositoryCore(bool enable_transformers = false)
        : m_enable_transformers(enable_transformers) {}

    // ---- Strategy management ------------------------------------------------

    void add_strategy(std::unique_ptr<Strategy> strategy) {
        m_strategies.push_back(std::move(strategy));
    }

    size_t strategy_count() const noexcept { return m_strategies.size(); }

    // ---- Transformer registration -------------------------------------------

    void add_pre_transform(PreSaveTransform fn) {
        if (!m_enable_transformers) return;
        m_pre_save.push_back(std::move(fn));
    }

    void add_post_transform(PostLoadTransform fn) {
        if (!m_enable_transformers) return;
        m_post_load.push_back(std::move(fn));
    }

    // ---- Factory management -------------------------------------------------

    void set_factory(RowFactory factory) {
        m_factory = std::move(factory);
        m_has_factory = true;
    }

    void clear_factory() {
        m_factory = nullptr;
        m_has_factory = false;
    }

    bool has_factory() const noexcept { return m_has_factory; }

    // ---- Query rendering (delegates to first strategy with a dialect) -------

    /// Find the first strategy that provides a QueryDialect.
    const QueryDialect *find_dialect() const {
        for (const auto &s : m_strategies) {
            if (auto *d = s->dialect()) return d;
        }
        return nullptr;
    }

    /// Render a QueryIntent using the first available dialect.
    /// Throws if no strategy provides a dialect.
    RenderedQuery render_query(const QueryIntent &intent) const {
        const auto *d = find_dialect();
        if (!d) {
            throw std::runtime_error("RepositoryCore: no strategy provides a QueryDialect");
        }
        return d->render(intent);
    }

    // ---- Fetch operations ---------------------------------------------------

    /// Direct key-based fetch — tries each strategy's fetch_by_key.
    std::optional<RowMap> fetch_by_key(const TablePkKey &key) {
        for (auto &s : m_strategies) {
            if (!s->capabilities().can_save) continue; // key-based implies save-capable
            auto row = s->fetch_by_key(key);
            if (row.has_value()) return row;
        }
        return std::nullopt;
    }

    /// Direct key-based existence check.
    bool contains_key(const TablePkKey &key) {
        for (auto &s : m_strategies) {
            if (s->contains_key(key)) return true;
        }
        return false;
    }

    /// Fetch raw result set via rendered query. Returns nullopt if not found.
    std::optional<ResultSet> fetch_raw(const RenderedQuery &query) {
        for (auto &s : m_strategies) {
            if (!s->capabilities().can_fetch) continue;
            auto result = s->fetch(query);
            if (result.has_value() && !result->empty()) {
                return result;
            }
        }
        return std::nullopt;
    }

    /// Fetch via QueryIntent (renders using first available dialect).
    std::optional<ResultSet> fetch_raw(const QueryIntent &intent) {
        auto rendered = render_query(intent);
        return fetch_raw(rendered);
    }

    /// Fetch with post-load transforms and optional factory applied.
    std::optional<ResultSet> fetch(const QueryIntent &intent) {
        auto result = fetch_raw(intent);
        if (!result.has_value()) return std::nullopt;
        return apply_post_load(intent.table, std::move(*result));
    }

    // ---- Save operations ----------------------------------------------------

    void save(const TablePkKey &key, RowMap data) {
        RowMap current = apply_pre_save(key.table, std::move(data));
        bool any = false;
        for (auto &s : m_strategies) {
            if (!s->capabilities().can_save) continue;
            s->save(key, current);
            any = true;
        }
        if (!any) {
            throw std::runtime_error("RepositoryCore: no strategy supports save()");
        }
    }

    // ---- Bulk operations ----------------------------------------------------

    void bulk_insert(const std::string &table,
                     const TypedColumnBatch &batch,
                     int batch_size = 1000,
                     const std::string &table_hint = "TABLOCK") {
        bool any = false;
        for (auto &s : m_strategies) {
            if (!s->capabilities().can_bulk_insert) continue;
            s->bulk_insert(table, batch, batch_size, table_hint);
            any = true;
        }
        if (!any) {
            throw std::runtime_error("RepositoryCore: no strategy supports bulk_insert()");
        }
    }

    void bulk_upsert(const std::string &table,
                     const TypedColumnBatch &batch,
                     const std::string &key_column = "id",
                     int batch_size = 500,
                     const std::string &table_hint = "TABLOCK") {
        bool any = false;
        for (auto &s : m_strategies) {
            if (!s->capabilities().can_bulk_upsert) continue;
            s->bulk_upsert(table, batch, key_column, batch_size, table_hint);
            any = true;
        }
        if (!any) {
            throw std::runtime_error("RepositoryCore: no strategy supports bulk_upsert()");
        }
    }

    // ---- Arrow persistence --------------------------------------------------

    void persist_arrow(const std::string &table,
                       std::shared_ptr<arrow::RecordBatchReader> reader,
                       const std::string &input_mode,
                       int batch_size = 100000,
                       const std::string &table_hint = "TABLOCK") {
        for (auto &s : m_strategies) {
            if (!s->capabilities().can_persist_arrow) continue;
            s->persist_arrow(table, std::move(reader), input_mode, batch_size, table_hint);
            return;
        }
        throw std::runtime_error("RepositoryCore: no strategy supports persist_arrow()");
    }

    // ---- Introspection ------------------------------------------------------

    bool transformers_enabled() const noexcept { return m_enable_transformers; }

    std::string repr() const {
        return "RepositoryCore(strategies=" + std::to_string(m_strategies.size()) +
               ", transformers=" + (m_enable_transformers ? "True" : "False") +
               ", factory=" + (m_has_factory ? "True" : "False") + ")";
    }

private:
    std::vector<std::unique_ptr<Strategy>> m_strategies;
    std::vector<PreSaveTransform> m_pre_save;
    std::vector<PostLoadTransform> m_post_load;
    RowFactory m_factory;
    bool m_has_factory{false};
    bool m_enable_transformers;

    RowMap apply_pre_save(const std::string &table, RowMap data) {
        if (!m_enable_transformers) return data;
        for (auto &t : m_pre_save) {
            data = t(table, data);
        }
        return data;
    }

    ResultSet apply_post_load(const std::string &table, ResultSet result) {
        if (m_enable_transformers) {
            for (auto &t : m_post_load) {
                result = t(table, std::move(result));
            }
        }
        // Factory is applied per-row if present
        // (factory transforms individual rows, not the full result set)
        // This is left as a hook — the adapter layer will handle entity construction
        // since factories typically return Python objects.
        return result;
    }
};

} // namespace pygim::core
