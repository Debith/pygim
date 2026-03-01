// MemoryStrategy: in-memory storage backend for development and testing.
// This header is pybind-free.
//
// Stores rows as RowMap keyed by (table, pk_string). Supports fetch and save.
// Does not support bulk operations or Arrow persistence.
#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "strategy.h"
#include "value_types.h"

namespace pygim::core {

namespace memory_detail {

/// Convert a CellValue to a string key for internal map lookup.
inline std::string cell_to_string(const CellValue &v) {
    struct Visitor {
        std::string operator()(Null) const { return "<null>"; }
        std::string operator()(bool b) const { return b ? "true" : "false"; }
        std::string operator()(int64_t i) const { return std::to_string(i); }
        std::string operator()(double d) const { return std::to_string(d); }
        std::string operator()(const std::string &s) const { return s; }
    };
    return std::visit(Visitor{}, v);
}

/// Composite key: "table\0pk_string".
inline std::string make_key(const std::string &table, const CellValue &pk) {
    return table + '\0' + cell_to_string(pk);
}

} // namespace memory_detail

class MemoryStrategy final : public Strategy {
public:
    StrategyCapabilities capabilities() const override {
        return {
            .can_fetch = true,
            .can_save = true,
            .can_bulk_insert = false,
            .can_bulk_upsert = false,
            .can_persist_arrow = false,
        };
    }

    std::optional<ResultSet> fetch(const RenderedQuery & /*query*/) override {
        // MemoryStrategy doesn't support SQL queries — only direct key lookup.
        return std::nullopt;
    }

    std::optional<RowMap> fetch_by_key(const TablePkKey &key) override {
        auto composite = memory_detail::make_key(key.table, key.pk);
        auto it = m_store.find(composite);
        if (it == m_store.end()) return std::nullopt;
        return it->second;
    }

    bool contains_key(const TablePkKey &key) override {
        auto composite = memory_detail::make_key(key.table, key.pk);
        return m_store.count(composite) > 0;
    }

    void save(const TablePkKey &key, const RowMap &data) override {
        auto composite = memory_detail::make_key(key.table, key.pk);
        m_store[composite] = data;
    }

    /// Number of stored entries.
    size_t size() const noexcept { return m_store.size(); }

    /// Clear all stored data.
    void clear() noexcept { m_store.clear(); }

private:
    std::unordered_map<std::string, RowMap> m_store;
};

} // namespace pygim::core
