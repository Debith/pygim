// Pybind11 entry point for the MSSQL strategy. All business logic lives in
// detail/*.cpp translation units; this file only exposes the class to Python
// and advertises detected features.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include "mssql_strategy.h"

namespace py = pybind11;

PYBIND11_MODULE(mssql_strategy, m) {
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
             py::arg("arrow_ipc_bytes"),
             py::arg("batch_size") = 100000,
             py::arg("table_hint") = "TABLOCK")
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
