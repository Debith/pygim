// repository/adapter/bindings.cpp
// Adapter package — pybind11 bindings placeholder.
//
// Exposes acquire_repo(), FlexibleRepository, and Query to Python.
// Uses if constexpr trampoline (D2) for format selection.

#include "flexible_repository.h"
#include "../core/query.h"
#include "../../utils/logging.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace pygim;

// ────────────────────────────────────────────────────────────────
// Type aliases for the concrete instantiation we expose
// ────────────────────────────────────────────────────────────────

using MssqlPolarsRepo = adapter::FlexibleRepository<core::MssqlBackend, adapter::Format::Polars>;
using MssqlPandasRepo = adapter::FlexibleRepository<core::MssqlBackend, adapter::Format::Pandas>;

// ────────────────────────────────────────────────────────────────
// acquire_repo — Python entry point
// ────────────────────────────────────────────────────────────────

static py::object acquire_repo(const std::string& conn_str,
                               const std::string& format) {
    PYGIM_TIMED_SCOPE("acquire_repo");
    PYGIM_LOG_FMT("[acquire_repo] conn_str=\"%s\", format=\"%s\"\n",
                  conn_str.c_str(), format.c_str());

    // D2: if constexpr trampoline — at Python boundary we branch once
    // on the format string, then everything inside is fully inlined.
    if (format == "polars") {
        PYGIM_LOG_FMT("[acquire_repo]   FormatEnum::Polars → "
                      "FlexibleRepository<Mssql, Polars>\n");
        return py::cast(MssqlPolarsRepo(conn_str));
    } else if (format == "pandas") {
        PYGIM_LOG_FMT("[acquire_repo]   FormatEnum::Pandas → "
                      "FlexibleRepository<Mssql, Pandas>\n");
        return py::cast(MssqlPandasRepo(conn_str));
    } else {
        throw py::value_error("Unknown format: " + format +
                              ". Use 'polars' or 'pandas'.");
    }
}

// ────────────────────────────────────────────────────────────────
// Module definition
// ────────────────────────────────────────────────────────────────

PYBIND11_MODULE(_repository, m) {
    m.doc() = "Repository adapter — placeholder bindings";

    // Query builder
    py::class_<core::Query>(m, "Query")
        .def(py::init<>())
        .def(py::init<std::string_view>(), py::arg("raw_sql"))
        .def("select", &core::Query::select, py::arg("col"),
             py::return_value_policy::reference_internal)
        .def("from_table", &core::Query::from_table, py::arg("table"),
             py::return_value_policy::reference_internal)
        .def("where", &core::Query::where, py::arg("clause"),
             py::return_value_policy::reference_internal)
        .def("limit", &core::Query::limit, py::arg("n"),
             py::return_value_policy::reference_internal)
        .def("build", &core::Query::build);

    // FlexibleRepository<MssqlBackend, Polars>
    py::class_<MssqlPolarsRepo>(m, "MssqlPolarsRepository")
        .def("save", &MssqlPolarsRepo::save,
             py::arg("table_name"), py::arg("bcp_workers") = 1)
        .def("load",
             py::overload_cast<std::string_view, int>(&MssqlPolarsRepo::load),
             py::arg("source"), py::arg("load_workers") = 1)
        .def("__repr__", [](MssqlPolarsRepo const&) {
            return "MssqlPolarsRepository(placeholder)";
        });

    // FlexibleRepository<MssqlBackend, Pandas>
    py::class_<MssqlPandasRepo>(m, "MssqlPandasRepository")
        .def("save", &MssqlPandasRepo::save,
             py::arg("table_name"), py::arg("bcp_workers") = 1)
        .def("load",
             py::overload_cast<std::string_view, int>(&MssqlPandasRepo::load),
             py::arg("source"), py::arg("load_workers") = 1)
        .def("__repr__", [](MssqlPandasRepo const&) {
            return "MssqlPandasRepository(placeholder)";
        });

    // acquire_repo factory
    m.def("acquire_repo", &acquire_repo,
          py::arg("conn_str"),
          py::arg("format") = "polars",
          R"doc(
          Create a repository from a connection string.

          Parameters
          ----------
          conn_str : str
              Connection string (e.g., "Driver={...};Server=...").
          format : str
              Output format: "polars" (default) or "pandas".

          Returns
          -------
          MssqlPolarsRepository or MssqlPandasRepository
              A FlexibleRepository wrapping the appropriate backend + format.
          )doc");
}
