#pragma once

#include <optional>
#include <string>

#include <pybind11/pybind11.h>

#include "../mssql_strategy.h"

namespace py = pybind11;

namespace pygim::persist_detail {

struct PersistAttempt {
    bool success{false};
    std::string mode;
    double prep_seconds{0.0};
    double prep_to_arrow_seconds{0.0};
    double prep_ipc_seconds{0.0};
    double write_seconds{0.0};
    std::string error;
};

struct ArrowPersistView {
    const std::string &table;
    const py::object &data_frame;
    int batch_size;
    const std::string &table_hint;
};

struct PersistRequestView {
    const std::string &table;
    const py::object &data_frame;
    const std::string &key_column;
    bool prefer_arrow;
    const std::string &table_hint;
    int batch_size;

    ArrowPersistView arrow_view() const {
        return ArrowPersistView{table, data_frame, batch_size, table_hint};
    }
};

class ArrowPersistPath {
public:
    explicit ArrowPersistPath(MssqlStrategyNative &strategy) : m_strategy(strategy) {}

    PersistAttempt try_c_stream(const ArrowPersistView &view) const;
    PersistAttempt try_ipc(const ArrowPersistView &view) const;

private:
    MssqlStrategyNative &m_strategy;
};

class BulkUpsertPersistPath {
public:
    explicit BulkUpsertPersistPath(MssqlStrategyNative &strategy) : m_strategy(strategy) {}

    PersistAttempt run(const PersistRequestView &view) const;

private:
    MssqlStrategyNative &m_strategy;
};

class PersistDataFrameOrchestrator {
public:
    explicit PersistDataFrameOrchestrator(MssqlStrategyNative &strategy)
        : m_strategy(strategy),
          m_arrow(strategy),
          m_bulk(strategy) {}

    py::dict execute(const PersistRequestView &view) const;

private:
    MssqlStrategyNative &m_strategy;
    ArrowPersistPath m_arrow;
    BulkUpsertPersistPath m_bulk;
};

bool env_true(const char *name);

py::dict to_py_dict(const PersistAttempt &attempt,
                    const std::optional<std::string> &arrow_error = std::nullopt);

} // namespace pygim::persist_detail
