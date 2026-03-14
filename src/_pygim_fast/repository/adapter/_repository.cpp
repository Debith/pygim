// Pybind11 bindings for the core/adapter Repository architecture.
//
// Exposes:
// - QueryAdapter as "Query" (fluent builder)
// - Repository (adapter wrapping RepositoryCore)
// - _ArrowResult (Arrow PyCapsule protocol wrapper for load() results)
// - StatusPrinter (flag-controlled status output, distinct from logging)
// - acquire_repository() free function (recommended entry point)
//
// MemoryStrategy, MssqlStrategy, and MssqlDialect are internal C++ types —
// they are constructed/used automatically by Repository and do not need
// Python-level bindings.

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>

#include <arrow/api.h>
#include <arrow/c/bridge.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "../core/arrow_load_strategy.h"
#include "../core/status_printer.h"
#include "../core/value_types.h"
#include "data_extractor.h"
#include "query_adapter.h"
#include "repository.h"

namespace py = pybind11;

// ---------------------------------------------------------------------------
// _ArrowResult: PyCapsule protocol wrapper
// ---------------------------------------------------------------------------

/// Lightweight wrapper that implements Arrow PyCapsule protocol
/// (__arrow_c_stream__).  Created transiently by Repository.load() to
/// hand off data to Polars without going through Python dicts or PyArrow.
struct ArrowResult {
    std::shared_ptr<arrow::RecordBatch> batch;

    py::capsule arrow_c_stream(py::object /*requested_schema*/ = py::none()) const {
        auto reader_result = arrow::RecordBatchReader::Make({batch}, batch->schema());
        if (!reader_result.ok())
            throw std::runtime_error(reader_result.status().ToString());

        auto *c_stream = new ArrowArrayStream;
        std::memset(c_stream, 0, sizeof(*c_stream));

        auto status = arrow::ExportRecordBatchReader(
            std::move(*reader_result), c_stream);
        if (!status.ok()) {
            delete c_stream;
            throw std::runtime_error(status.ToString());
        }

        return py::capsule(
            static_cast<void *>(c_stream), "arrow_array_stream",
            [](void *ptr) {
                auto *s = static_cast<ArrowArrayStream *>(ptr);
                if (s->release) s->release(s);
                delete s;
            });
    }
};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Return a credential-free summary of *s* safe to print in status lines.
///  - URL form:   mssql://user:PASSWORD@host/db → mssql://user:***@host/db
///  - Raw ODBC:   ...;PWD=secret;...            → ...;PWD=***;...
///  - memory://   (and any credential-free URI)  → returned unchanged
static std::string mask_conn_str(const std::string &s) {
    std::string result = s;

    // URL form: mask password between the first ':' after "://" and '@'.
    auto scheme_end = result.find("://");
    if (scheme_end != std::string::npos) {
        auto after = scheme_end + 3;
        auto colon = result.find(':', after);
        auto at    = result.find('@', after);
        if (colon != std::string::npos && at != std::string::npos && colon < at) {
            result.replace(colon + 1, at - colon - 1, "***");
        }
    }

    // Raw ODBC: find PWD= (case-insensitive) and mask the value up to ';'.
    std::string lower = result;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto pwd = lower.find("pwd=");
    if (pwd != std::string::npos) {
        auto val_start = pwd + 4;
        auto val_end = result.find(';', val_start);
        if (val_end == std::string::npos) val_end = result.size();
        result.replace(val_start, val_end - val_start, "***");
    }

    return result;
}

// ---------------------------------------------------------------------------

PYBIND11_MODULE(_repository, m) {
    m.doc() = "Repository core/adapter — pybind-free strategies, "
              "centralized data extraction, dialect-based query rendering.";

    using namespace pygim;

    // ---- StatusPrinter ------------------------------------------------------

    py::class_<StatusPrinter>(m, "StatusPrinter",
        "Flag-controlled status output for repository operations.\n\n"
        "StatusPrinter prints directly to stdout and is intentionally\n"
        "separate from the structured logging subsystem.\n\n"
        "Each boolean flag gates one category of output:\n"
        "  connection  — one line per connection attempt (default True).\n\n"
        "Pass a StatusPrinter to acquire_repository() to control output:\n"
        "  repo = acquire_repository('mssql://host/db')                   # prints\n"
        "  repo = acquire_repository('mssql://host/db',\n"
        "                            printer=StatusPrinter(connection=False))  # quiet")
        .def(py::init([](bool connection) {
                 return StatusPrinter{connection};
             }),
             py::arg("connection") = true)
        .def_readwrite("connection", &StatusPrinter::connection,
                       "Print one status line per connection attempt.")
        .def("__repr__", [](const StatusPrinter &p) {
            return "StatusPrinter(connection=" +
                   std::string(p.connection ? "True" : "False") + ")";
        });

    // ---- _ArrowResult (PyCapsule protocol for load results) ----------------

    py::class_<ArrowResult>(m, "_ArrowResult",
        "Internal Arrow PyCapsule protocol wrapper.\n\n"
        "Created transiently by Repository.load() to convert data from\n"
        "C++ Arrow RecordBatch to Polars DataFrame via zero-copy.")
        .def("__arrow_c_stream__", &ArrowResult::arrow_c_stream,
             py::arg("requested_schema") = py::none(),
             "Export as Arrow C Data Interface stream (PyCapsule protocol).");

    // ---- QueryAdapter as "Query" --------------------------------------------

    py::class_<adapter::QueryAdapter>(m, "Query")
        .def(py::init<>(),
             "Create an empty query builder.")
        .def(py::init<std::string, std::vector<py::object>>(),
             py::arg("sql"), py::arg("params") = std::vector<py::object>{},
             "Create a query from raw SQL and optional parameters.")
        .def("select", &adapter::QueryAdapter::select, py::arg("columns"),
             "Set the SELECT column list.")
        .def("from_table", &adapter::QueryAdapter::from_table, py::arg("table"),
             "Set the FROM table.")
        .def("where", &adapter::QueryAdapter::where,
             py::arg("clause"), py::arg("param"),
             "Append a WHERE clause with a bound parameter.")
        .def("limit", &adapter::QueryAdapter::limit, py::arg("rows"),
             "Set a LIMIT (<=0 clears).")
        .def("build", &adapter::QueryAdapter::build,
             py::return_value_policy::move,
             "Freeze builder state into a new Query copy.")
        .def("clone", &adapter::QueryAdapter::clone,
             py::return_value_policy::move,
             "Deep copy without rebuilding.")
        .def("params", &adapter::QueryAdapter::params_py,
             "Return bound parameters as a Python list.");

    // ---- Repository ---------------------------------------------------------

    py::class_<adapter::Repository>(m, "Repository", py::dynamic_attr())
        .def(py::init<std::string, bool, std::string>(),
             py::arg("connection_uri"),
             py::arg("transformers") = false,
             py::arg("transpose") = "",
             "Create a Repository directly from a connection URI (no status output).\n\n"
             "Prefer acquire_repository() for interactive use — it prints a status\n"
             "line before connecting and accepts a StatusPrinter for control.\n\n"
             "Supported schemes:\n"
             "  memory://              — in-memory (dev/test)\n"
             "  mssql://server/db     — MSSQL via ODBC\n"
             "  Driver={...};Server=… — raw ODBC (MSSQL)\n\n"
             "Parameters:\n"
             "  connection_uri: URI or ODBC connection string.\n"
             "  transformers:   enable pre-save / post-load pipeline.\n"
             "  transpose:      BCP row-loop algorithm — \"\" / \"row_major\" (default)\n"
             "                  or \"column_major\".  Fixed for the repository's lifetime.")
        .def("set_factory", &adapter::Repository::set_factory, py::arg("factory"),
             "Set factory callable: factory(key, data) -> entity.")
        .def("clear_factory", &adapter::Repository::clear_factory)
        .def("add_pre_transform", &adapter::Repository::add_pre_transform, py::arg("func"),
             "Add pre-save transformer func(table, data) -> data. No-op if transformers disabled.")
        .def("add_post_transform", &adapter::Repository::add_post_transform, py::arg("func"),
             "Add post-load transformer func(table, rows) -> rows. No-op if transformers disabled.")
        .def("fetch_raw",
             [](adapter::Repository &self, const adapter::QueryAdapter &query) {
                 return self.fetch_raw(query);
             },
             py::arg("query"),
             "Fetch raw data via query without transforms/factory; returns None if not found.")
        .def("get", &adapter::Repository::get, py::arg("key"),
             "Get by key (raises if not found).")
        .def("get", &adapter::Repository::get_default,
             py::arg("key"), py::arg("default"),
             "Get with default (like dict.get).")
        .def("contains", &adapter::Repository::contains, py::arg("key"))
        .def("save", &adapter::Repository::save,
             py::arg("key"), py::arg("value"))
        .def("bulk_insert", &adapter::Repository::bulk_insert,
             py::arg("table"), py::arg("columns"), py::arg("rows"),
             py::arg("batch_size") = 1000, py::arg("table_hint") = "TABLOCK")
        .def("bulk_upsert", &adapter::Repository::bulk_upsert,
             py::arg("table"), py::arg("columns"), py::arg("rows"),
             py::arg("key_column") = "id", py::arg("batch_size") = 500,
             py::arg("table_hint") = "TABLOCK")
        .def("persist_dataframe", &adapter::Repository::persist_dataframe,
             py::arg("table"), py::arg("data_frame"),
             py::arg("key_column") = "id",
             py::arg("prefer_arrow") = true,
             py::arg("table_hint") = "TABLOCK",
             py::arg("batch_size") = 1000,
             py::arg("bcp_batch_size") = 0,
             py::arg("bcp_workers") = 0,
             "Persist a DataFrame via Arrow BCP when available, with fallback to bulk_upsert.\n\n"
             "batch_size     controls MERGE commit frequency (fallback path).\n"
             "bcp_batch_size controls BCP bcp_batch() commit frequency; 0 = auto (100 000).\n"
             "bcp_workers    number of parallel BCP connections; 0 = single-connection (default).")
        .def("load",
             [](adapter::Repository &self, const std::string &source) -> py::object {
                 pygim::core::ArrowLoadStrategy strat;
                 self.load(source, strat);
                 auto batch = strat.result();
                 py::module_ pl = py::module_::import("polars");
                 if (!batch || batch->num_rows() == 0) {
                     return pl.attr("DataFrame")();
                 }
                 ArrowResult wrapper{std::move(batch)};
                 return pl.attr("from_arrow")(py::cast(std::move(wrapper)));
             },
             py::arg("source"),
             "Load data from a table name or SQL SELECT into a Polars DataFrame.\n\n"
             "Data flows directly through Arrow builders during the ODBC fetch loop\n"
             "(single-pass, zero intermediate copies)::\n\n"
             "    df = repo.load('dbo.my_table')\n"
             "    df = repo.load('SELECT TOP 100 * FROM t')\n\n"
             "If *source* contains no whitespace it is treated as a table name\n"
             "and ``SELECT * FROM <source>`` is executed automatically.\n\n"
             "Returns an empty DataFrame if no rows are found.")
        .def("__getitem__", &adapter::Repository::get, py::arg("key"))
        .def("__setitem__", &adapter::Repository::save,
             py::arg("key"), py::arg("value"))
        .def("__repr__", &adapter::Repository::repr);

    // ---- acquire_repository free function -----------------------------------

    m.def("acquire_repository",
        [](const std::string &conn_str,
           bool transformers,
           const std::string &transpose,
           std::optional<StatusPrinter> printer) -> adapter::Repository {
            const StatusPrinter p = printer.value_or(StatusPrinter{});
            p.print_connection("connecting  " + mask_conn_str(conn_str));
            return adapter::Repository(conn_str, transformers, transpose);
        },
        py::arg("conn_str"),
        py::arg("transformers") = false,
        py::arg("transpose") = "",
        py::arg("printer") = py::none(),
        "Create a Repository from a connection string with optional status output.\n\n"
        "A status line is printed to stdout before the connection is established\n"
        "unless suppressed via the printer argument.  Credentials in the connection\n"
        "string are masked automatically.\n\n"
        "Supported schemes:\n"
        "  memory://              — in-memory (dev/test)\n"
        "  mssql://server/db     — MSSQL via ODBC\n"
        "  Driver={...};Server=… — raw ODBC (MSSQL)\n\n"
        "Parameters:\n"
        "  conn_str:     URI or ODBC connection string.\n"
        "  transformers: enable pre-save / post-load pipeline.\n"
        "  transpose:    BCP row-loop algorithm — \"\" / \"row_major\" (default)\n"
        "                or \"column_major\".  Fixed for the repository's lifetime.\n"
        "  printer:      StatusPrinter controlling output; None uses the default\n"
        "                (connection=True).  Pass StatusPrinter(connection=False)\n"
        "                to suppress all status output.\n\n"
        "Examples::\n\n"
        "    from pygim import acquire_repository, StatusPrinter\n\n"
        "    repo = acquire_repository('mssql://host/db')                     # prints, row_major\n"
        "    repo = acquire_repository('mssql://host/db', transpose='column_major')  # column_major\n"
        "    repo = acquire_repository('mssql://host/db',\n"
        "                             printer=StatusPrinter(connection=False))  # quiet"
    );
}
