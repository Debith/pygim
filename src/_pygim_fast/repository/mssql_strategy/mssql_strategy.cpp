// Pybind11 entry point for the MSSQL strategy. All business logic lives in
// detail/*.cpp translation units; this file only exposes the class to Python
// and advertises detected features.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <string>

#include "mssql_strategy.h"
#include "detail/mssql_strategy_persist.h"
#include "../../utils/logging.h"

namespace py = pybind11;

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
                 const persist_detail::PersistRequestView view{
                     table,
                     data_frame,
                     key_column,
                     prefer_arrow,
                     table_hint,
                     batch_size,
                 };
                 persist_detail::PersistDataFrameOrchestrator orchestrator(self);
                 return orchestrator.execute(view);
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
