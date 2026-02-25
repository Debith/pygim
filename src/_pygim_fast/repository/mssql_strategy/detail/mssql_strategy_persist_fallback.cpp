#include "mssql_strategy_persist.h"

#include <vector>

#include "../../../utils/quick_timer.h"

namespace py = pybind11;

namespace pygim::persist_detail {

static PersistAttempt run_bulk_upsert(MssqlStrategyNative &self,
                               const std::string &table,
                               const py::object &data_frame,
                               const std::string &key_column,
                               int batch_size,
                               const std::string &table_hint) {
    PersistAttempt out;
    out.mode = "bulk_upsert";

    std::vector<std::string> columns;
    for (auto item : data_frame.attr("columns")) {
        columns.push_back(py::cast<std::string>(item));
    }

    pygim::QuickTimer timer("persist_bulk_upsert", std::clog, false);
    timer.start_sub_timer("bulk_upsert_write", false);
    self.bulk_upsert(table, columns, data_frame, key_column, batch_size, table_hint);
    out.write_seconds = timer.stop_sub_timer("bulk_upsert_write", false);
    out.success = true;
    return out;
}

py::dict to_py_dict(const PersistAttempt &attempt,
                    const std::optional<std::string> &arrow_error) {
    py::dict out;
    out["mode"] = py::str(attempt.mode.empty() ? "unknown" : attempt.mode);
    out["prep_seconds"] = py::float_(attempt.prep_seconds);
    out["prep_to_arrow_seconds"] = py::float_(attempt.prep_to_arrow_seconds);
    out["prep_ipc_seconds"] = py::float_(attempt.prep_ipc_seconds);
    out["write_seconds"] = py::float_(attempt.write_seconds);
    if (arrow_error.has_value()) {
        out["arrow_error"] = py::str(*arrow_error);
    } else if (!attempt.error.empty()) {
        out["arrow_error"] = py::str(attempt.error);
    } else {
        out["arrow_error"] = py::none();
    }
    return out;
}

PersistAttempt BulkUpsertPersistPath::run(const PersistRequestView &view) const {
    return run_bulk_upsert(
        m_strategy,
        view.table,
        view.data_frame,
        view.key_column,
        view.batch_size,
        view.table_hint);
}

} // namespace pygim::persist_detail
