// repository/adapter/bindings.cpp
// Adapter package — pybind11 bindings.
//
// Exposes acquire_repo(), FlexibleRepository, and Query to Python.
// Uses if constexpr trampoline (D2) for format selection.
// Refactored: creates ConnectionPool and passes it through.

#include "../core/query.h"
#include "../strategy/mssql/save_impl.h"
#include "../strategy/mssql/load_impl.h"
#include "../format/flexible_repository.h"
#include "../../utils/logging.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <memory>

namespace py = pybind11;
using namespace pygim;

// Verify concept satisfaction where all types are fully defined
static_assert(pygim::core::BackendPolicy<pygim::strategy::mssql::MssqlBackend>,
              "MssqlBackend must satisfy BackendPolicy");

// ────────────────────────────────────────────────────────────────
// Type aliases for the concrete instantiation we expose
// ────────────────────────────────────────────────────────────────

using MssqlPolarsRepo = adapter::FlexibleRepository<strategy::mssql::MssqlBackend, adapter::Format::Polars>;
using MssqlPandasRepo = adapter::FlexibleRepository<strategy::mssql::MssqlBackend, adapter::Format::Pandas>;

// ────────────────────────────────────────────────────────────────
// acquire_repo — Python entry point
// ────────────────────────────────────────────────────────────────

static py::object acquire_repo(const std::string& conn_str,
                               const std::string& format,
                               std::size_t pool_size) {
    PYGIM_TIMED_SCOPE("acquire_repo");
    PYGIM_LOG_FMT("[acquire_repo] conn_str=\"%s\", format=\"%s\", pool_size=%zu\n",
                  conn_str.c_str(), format.c_str(), pool_size);

    // D2: branch once on format string at the Python boundary,
    // then everything inside is fully inlined via templates.
    if (format == "polars") {
        PYGIM_LOG_FMT("[acquire_repo]   FormatEnum::Polars → "
                      "FlexibleRepository<Mssql, Polars>\n");
        return py::cast(MssqlPolarsRepo::create(conn_str, pool_size));
    } else if (format == "pandas") {
        PYGIM_LOG_FMT("[acquire_repo]   FormatEnum::Pandas → "
                      "FlexibleRepository<Mssql, Pandas>\n");
        return py::cast(MssqlPandasRepo::create(conn_str, pool_size));
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
             py::return_value_policy::reference_internal);

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
          py::arg("pool_size") = 4,
          R"doc(
          Create a repository from a connection string.

          Parameters
          ----------
          conn_str : str
              Connection string (e.g., "Driver={...};Server=...").
          format : str
              Output format: "polars" (default) or "pandas".
          pool_size : int
              Maximum number of pooled connections (default: 4).

          Returns
          -------
          MssqlPolarsRepository or MssqlPandasRepository
              A FlexibleRepository wrapping the appropriate backend + format.
          )doc");
}
