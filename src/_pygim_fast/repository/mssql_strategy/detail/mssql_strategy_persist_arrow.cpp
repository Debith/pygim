#include "mssql_strategy_persist.h"

#include <cstdlib>

#include "../../../utils/quick_timer.h"

namespace py = pybind11;

namespace pygim::persist_detail {

bool env_true(const char *name) {
    const char *raw = std::getenv(name);
    if (raw == nullptr) {
        return false;
    }
    const std::string value(raw);
    return value == "1" || value == "true" || value == "TRUE";
}

namespace {

py::object try_polars_compat_oldest() {
    try {
        py::module_ pl = py::module_::import("polars");
        return pl.attr("CompatLevel").attr("oldest")();
    } catch (...) {
        return py::none();
    }
}

} // namespace

PersistAttempt try_arrow_c_stream_bcp(MssqlStrategyNative &self,
                                      const std::string &table,
                                      const py::object &data_frame,
                                      int batch_size,
                                      const std::string &table_hint) {
    PersistAttempt out;
    out.mode = "arrow_c_stream_bcp";

    pygim::QuickTimer timer("persist_arrow_c_stream", std::clog, false);

    try {
        timer.start_sub_timer("to_arrow_c_stream");
        py::object c_stream_capsule;
        py::object compat_level = try_polars_compat_oldest();
        if (!compat_level.is_none() && py::hasattr(data_frame, "to_arrow")) {
            py::object arrow_table = data_frame.attr("to_arrow")(py::arg("compat_level") = compat_level);
            if (py::hasattr(arrow_table, "__arrow_c_stream__")) {
                c_stream_capsule = arrow_table.attr("__arrow_c_stream__")();
            } else {
                c_stream_capsule = data_frame.attr("__arrow_c_stream__")();
            }
        } else {
            c_stream_capsule = data_frame.attr("__arrow_c_stream__")();
        }
        out.prep_to_arrow_seconds = timer.stop_sub_timer("to_arrow_c_stream", false);

        timer.start_sub_timer("bcp_write");
        self.bulk_insert_arrow_bcp(table, c_stream_capsule, batch_size, table_hint);
        out.write_seconds = timer.stop_sub_timer("bcp_write", false);

        out.prep_seconds = out.prep_to_arrow_seconds;
        out.prep_ipc_seconds = 0.0;
        out.success = true;
        return out;
    } catch (const py::error_already_set &exc) {
        out.error = std::string("c_stream strategy failed: ") + exc.what();
        return out;
    } catch (const std::exception &exc) {
        out.error = std::string("c_stream strategy failed: ") + exc.what();
        return out;
    }
}

PersistAttempt try_arrow_ipc_bcp(MssqlStrategyNative &self,
                                 const std::string &table,
                                 const py::object &data_frame,
                                 int batch_size,
                                 const std::string &table_hint) {
    PersistAttempt out;
    out.mode = "arrow_ipc_bcp";

    pygim::QuickTimer timer("persist_arrow_ipc", std::clog, false);

    try {
        timer.start_sub_timer("to_arrow_ipc");
        py::object compat_level = try_polars_compat_oldest();
        py::object payload;
        if (!compat_level.is_none()) {
            payload = data_frame.attr("write_ipc")(
                py::arg("file") = py::none(),
                py::arg("compat_level") = compat_level);
        } else {
            payload = data_frame.attr("write_ipc")(py::arg("file") = py::none());
        }
        if (py::hasattr(payload, "getvalue")) {
            payload = payload.attr("getvalue")();
        }
        if (!py::isinstance<py::bytes>(payload) &&
            !py::isinstance<py::bytearray>(payload) &&
            !py::isinstance<py::memoryview>(payload)) {
            payload = py::bytes(payload);
        }
        out.prep_ipc_seconds = timer.stop_sub_timer("to_arrow_ipc", false);

        timer.start_sub_timer("bcp_write");
        self.bulk_insert_arrow_bcp(table, payload, batch_size, table_hint);
        out.write_seconds = timer.stop_sub_timer("bcp_write", false);

        out.prep_seconds = out.prep_ipc_seconds;
        out.prep_to_arrow_seconds = 0.0;
        out.success = true;
        return out;
    } catch (const py::error_already_set &exc) {
        out.error = std::string("ipc strategy failed: ") + exc.what();
        return out;
    } catch (const std::exception &exc) {
        out.error = std::string("ipc strategy failed: ") + exc.what();
        return out;
    }
}

PersistAttempt ArrowPersistPath::try_c_stream(const ArrowPersistView &view) const {
    return try_arrow_c_stream_bcp(
        m_strategy,
        view.table,
        view.data_frame,
        view.batch_size,
        view.table_hint);
}

PersistAttempt ArrowPersistPath::try_ipc(const ArrowPersistView &view) const {
    return try_arrow_ipc_bcp(
        m_strategy,
        view.table,
        view.data_frame,
        view.batch_size,
        view.table_hint);
}

} // namespace pygim::persist_detail
