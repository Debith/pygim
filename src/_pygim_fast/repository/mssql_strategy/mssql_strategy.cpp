// Pybind11 entry point for the MSSQL strategy. All business logic lives in
// detail/*.cpp translation units; this file only exposes the class to Python
// and advertises detected features.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <cstdlib>
#include <optional>
#include <string>

#include "mssql_strategy.h"
#include "../../utils/logging.h"
#include "../../utils/quick_timer.h"

namespace py = pybind11;

namespace {

struct PersistAttempt {
    bool success{false};
    std::string mode;
    double prep_seconds{0.0};
    double prep_to_arrow_seconds{0.0};
    double prep_ipc_seconds{0.0};
    double write_seconds{0.0};
    std::string error;
};

bool env_true(const char *name) {
    const char *raw = std::getenv(name);
    if (raw == nullptr) {
        return false;
    }
    const std::string value(raw);
    return value == "1" || value == "true" || value == "TRUE";
}

py::object try_polars_compat_oldest() {
    try {
        py::module_ pl = py::module_::import("polars");
        return pl.attr("CompatLevel").attr("oldest")();
    } catch (...) {
        return py::none();
    }
}

PersistAttempt try_arrow_c_stream_bcp(pygim::MssqlStrategyNative &self,
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

PersistAttempt try_arrow_ipc_bcp(pygim::MssqlStrategyNative &self,
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

PersistAttempt run_bulk_upsert(pygim::MssqlStrategyNative &self,
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
    timer.start_sub_timer("bulk_upsert_write");
    self.bulk_upsert(table, columns, data_frame, key_column, batch_size, table_hint);
    out.write_seconds = timer.stop_sub_timer("bulk_upsert_write", false);
    out.success = true;
    return out;
}

py::dict to_py_dict(const PersistAttempt &attempt,
                    const std::optional<std::string> &arrow_error = std::nullopt) {
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

} // namespace

PYBIND11_MODULE(mssql_strategy, m) {
    PYGIM_SCOPE_LOG_TAG("repo.module");
    using namespace pygim;

    py::class_<MssqlStrategyNative>(m, "MssqlStrategyNative")
        .def(py::init<std::string>(), py::arg("connection_string"))
        .def("fetch", &MssqlStrategyNative::fetch, py::arg("key"))
        .def("save", &MssqlStrategyNative::save, py::arg("key"), py::arg("value"))
        .def("bulk_insert", &MssqlStrategyNative::bulk_insert,
             py::arg("table"),
             py::arg("columns"),
             py::arg("rows"),
             py::arg("batch_size") = 1000,
             py::arg("table_hint") = "TABLOCK")
        .def("bulk_upsert", &MssqlStrategyNative::bulk_upsert,
             py::arg("table"),
             py::arg("columns"),
             py::arg("rows"),
             py::arg("key_column") = "id",
             py::arg("batch_size") = 1000,
             py::arg("table_hint") = "TABLOCK")
        .def("bulk_insert_arrow_bcp", &MssqlStrategyNative::bulk_insert_arrow_bcp,
             py::arg("table"),
               py::arg("arrow_ipc_payload"),
             py::arg("batch_size") = 100000,
             py::arg("table_hint") = "TABLOCK")
        .def("persist_dataframe",
             [](MssqlStrategyNative &self,
                const std::string &table,
                const py::object &data_frame,
                const std::string &key_column,
                bool prefer_arrow,
                const std::string &table_hint,
                int batch_size) {
                 std::optional<std::string> arrow_error = std::nullopt;

                 if (prefer_arrow) {
                     if (!env_true("PYGIM_ENABLE_ARROW_BCP")) {
                         arrow_error =
                             "Arrow BCP strategy disabled by default for stability; "
                             "set PYGIM_ENABLE_ARROW_BCP=1 to enable";
                     } else {
                         PersistAttempt c_stream_attempt = try_arrow_c_stream_bcp(
                             self,
                             table,
                             data_frame,
                             batch_size,
                             table_hint);
                         if (c_stream_attempt.success) {
                             return to_py_dict(c_stream_attempt, std::nullopt);
                         }

                         PersistAttempt ipc_attempt = try_arrow_ipc_bcp(
                             self,
                             table,
                             data_frame,
                             batch_size,
                             table_hint);
                         if (ipc_attempt.success) {
                             return to_py_dict(
                                 ipc_attempt,
                                 c_stream_attempt.error.empty()
                                     ? std::nullopt
                                     : std::optional<std::string>(c_stream_attempt.error));
                         }

                         std::string combined_error;
                         if (!c_stream_attempt.error.empty()) {
                             combined_error += c_stream_attempt.error;
                         }
                         if (!ipc_attempt.error.empty()) {
                             if (!combined_error.empty()) {
                                 combined_error += " | ";
                             }
                             combined_error += ipc_attempt.error;
                         }
                         arrow_error = combined_error.empty()
                                           ? std::optional<std::string>("Arrow strategies failed")
                                           : std::optional<std::string>(combined_error);
                     }
                 }

                 PersistAttempt fallback = run_bulk_upsert(
                     self,
                     table,
                     data_frame,
                     key_column,
                     batch_size,
                     table_hint);
                 return to_py_dict(fallback, arrow_error);
             },
             py::arg("table"),
             py::arg("data_frame"),
             py::arg("key_column") = "id",
             py::arg("prefer_arrow") = true,
             py::arg("table_hint") = "TABLOCK",
             py::arg("batch_size") = 1000,
             "Persist a DataFrame using Arrow BCP when available, with fallback to bulk_upsert.")
        .def("__repr__", &MssqlStrategyNative::repr);

#if PYGIM_HAVE_ODBC
    m.attr("HAVE_ODBC") = py::bool_(true);
#else
    m.attr("HAVE_ODBC") = py::bool_(false);
#endif

#ifdef PYGIM_HAVE_ARROW
    m.attr("HAVE_ARROW") = py::bool_(true);
#else
    m.attr("HAVE_ARROW") = py::bool_(false);
#endif
}
