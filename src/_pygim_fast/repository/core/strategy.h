// Strategy: abstract interface for storage backends.
// This header is pybind-free — strategies must NOT include pybind11 headers.
//
// All Python data extraction happens in the adapter/DataExtractor layer.
// Strategies receive and return pure C++ types only.
//
// Each strategy implementation:
// 1. Reports capabilities via capabilities()
// 2. Implements the methods its capabilities indicate
// 3. Optionally provides a QueryDialect for rendering QueryIntents
//
// The default implementations throw — only override what is supported.
#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "query_dialect.h"
#include "query_intent.h"
#include "value_types.h"

// Forward-declare Arrow types to avoid pulling heavy headers.
namespace arrow {
class RecordBatchReader;
} // namespace arrow

namespace pygim::core {

/// Static capability flags — determined once at strategy construction,
/// queried by RepositoryCore to dispatch operations.
struct StrategyCapabilities {
    bool can_fetch{false};
    bool can_save{false};
    bool can_bulk_insert{false};
    bool can_bulk_upsert{false};
    bool can_persist_arrow{false};
};

/// Abstract base for all storage strategies.
///
/// Convention:
/// - Override capabilities() to declare supported operations.
/// - Override only the methods matching declared capabilities.
/// - Methods for unsupported operations throw by default.
class Strategy {
public:
    virtual ~Strategy() = default;

    /// Declare what this strategy supports.
    virtual StrategyCapabilities capabilities() const = 0;

    /// Return the dialect for rendering QueryIntents, or nullptr if
    /// this strategy does not support query-based fetch.
    virtual const QueryDialect *dialect() const { return nullptr; }

    // ---- Key-value operations -----------------------------------------------

    /// Execute a rendered query and return the result set.
    virtual std::optional<ResultSet> fetch(const RenderedQuery &query) {
        throw std::runtime_error("Strategy::fetch not supported");
    }

    /// Direct key-based fetch (no SQL rendering required).
    /// Returns nullopt by default — strategies with key-based storage override.
    virtual std::optional<RowMap> fetch_by_key(const TablePkKey & /*key*/) {
        return std::nullopt;
    }

    /// Direct key-based existence check.
    virtual bool contains_key(const TablePkKey & /*key*/) {
        return false;
    }

    /// Upsert a single row identified by table + pk.
    virtual void save(const TablePkKey &key, const RowMap &data) {
        throw std::runtime_error("Strategy::save not supported");
    }

    // ---- Bulk operations (column-major typed batch) -------------------------

    virtual void bulk_insert(const std::string &table,
                             const TypedColumnBatch &batch,
                             int batch_size,
                             const std::string &table_hint) {
        throw std::runtime_error("Strategy::bulk_insert not supported");
    }

    virtual void bulk_upsert(const std::string &table,
                             const TypedColumnBatch &batch,
                             const std::string &key_column,
                             int batch_size,
                             const std::string &table_hint) {
        throw std::runtime_error("Strategy::bulk_upsert not supported");
    }

    // ---- Arrow path (receives already-extracted reader) ---------------------

    virtual void persist_arrow(const std::string &table,
                               std::shared_ptr<arrow::RecordBatchReader> reader,
                               const std::string &input_mode,
                               int batch_size,
                               const std::string &table_hint) {
        throw std::runtime_error("Strategy::persist_arrow not supported");
    }
};

} // namespace pygim::core
