// persistence/adapter/bindings.cpp
// Production pybind11 bindings for the _persistence module.
//
// Binds ONE class: RepositoryAdapter<MssqlBackend>.
// Format is runtime enum, not template parameter — single instantiation.
// Test-specific bindings (Query, pool internals) live in test_bindings.cpp.
// Fetch benchmarks live in bench_bindings.cpp (_fetch_benchmark module).

#include "../core/query.h"
#include "../strategy/mssql/save_impl.h"
#include "../strategy/mssql/load_impl.h"
#include "adapter.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

namespace py = pybind11;
using namespace pygim;

static_assert(core::BackendPolicy<strategy::mssql::MssqlBackend>,
              "MssqlBackend must satisfy BackendPolicy");

using MssqlRepo    = adapter::RepositoryAdapter<strategy::mssql::MssqlBackend>;
using MssqlSession = adapter::SessionAdapter<strategy::mssql::MssqlBackend>;

static py::object acquire_datastore(const std::string& conn_str,
                               const std::string& format,
                               std::size_t pool_size,
                               int64_t batch_size,
                               const std::string& table_hint,
                               int bcp_workers,
                               int64_t block_size,
                               int packet_size,
                               py::object access_token) {
    auto fmt = adapter::parse_format(format);
    return py::cast(MssqlRepo::create(conn_str, fmt, pool_size,
                                      batch_size, table_hint, bcp_workers,
                                      block_size, packet_size,
                                      std::move(access_token)));
}

PYBIND11_MODULE(_persistence, m) {
    m.doc() = "DataStore — database access with Arrow core and format conversion";

    // Bridge C++ std::runtime_error to GimError (RuntimeError subclass).
    // This aligns with the project's GimError exception hierarchy while
    // maintaining backward compatibility with existing except RuntimeError catches.
    static auto gim_error = py::exception<std::runtime_error>(m, "GimError", PyExc_RuntimeError);

    // Format enum exposed to Python
    py::enum_<adapter::Format>(m, "Format")
        .value("polars", adapter::Format::Polars)
        .value("pandas", adapter::Format::Pandas)
        .export_values();

    // ONE class, not two
    py::class_<MssqlRepo>(m, "DataStore")
        .def("save", &MssqlRepo::save,
             py::arg("data"), py::arg("table_name"), py::arg("bcp_workers") = -1,
             py::kw_only(),
             py::arg("mode") = "append",
             py::arg("keys") = std::vector<std::string>{},
             R"doc(Bulk-write data into a table via BCP. Returns a dict of metrics.

mode : str, keyword-only
    "append" (default): plain BCP insert.
    "upsert": BCP into a staging temp table, then MERGE on `keys`
    (update matched rows, insert new ones). The metrics dict gains
    "affected_rows". When every frame column is a key there is
    nothing to update, so upsert degenerates to insert_missing.
    "insert_missing": like upsert, but existing keys are skipped
    instead of updated (anti-join insert).
keys : list[str], keyword-only
    Merge key columns; required for "upsert"/"insert_missing",
    forbidden for "append". Key columns must not contain NULLs and
    key values must be unique within the frame (both enforced).

Keyed modes require the frame to contain only writable target
columns (computed/rowversion columns fail server-side); a target
IDENTITY column present in the frame is handled automatically via
IDENTITY_INSERT. "affected_rows" is best-effort in the presence of
target triggers or NOCOUNT settings.

An empty frame is a no-op: zero-row metrics, no connection used.)doc")
        .def("session",
             [](py::object self) {
                 return self.cast<MssqlRepo&>().session();
             },
             py::keep_alive<0, 1>(),
             R"doc(Open a caller-owned transaction session.

Checks out one pooled connection with autocommit OFF; every save/load
through the session shares one transaction finished by commit() or
rollback(), making multi-table writes atomic. Use as a context
manager: commits on clean exit, rolls back on exception.

    with store.session() as s:
        s.save(orders_df, "orders")
        s.save(items_df, "order_items")
    # both committed together (or neither)

Session saves are single-connection (no parallel BCP workers).)doc")
        .def("load",
             py::overload_cast<std::string_view, int, std::string_view>(&MssqlRepo::load),
             py::arg("source"), py::arg("load_workers") = 1,
             py::arg("partition_column") = "",
             "Load data from a table name or raw SQL query. Returns a DataFrame.\n"
             "When load_workers > 1 and partition_column is empty, the integer\n"
             "primary key column is auto-detected via ODBC metadata.")
        .def("load",
             py::overload_cast<core::Query const&, int, std::string_view>(&MssqlRepo::load),
             py::arg("query"), py::arg("load_workers") = 1,
             py::arg("partition_column") = "",
             "Load data from a Query object. Returns a DataFrame.\n"
             "When load_workers > 1 and partition_column is empty, the integer\n"
             "primary key column is auto-detected via ODBC metadata.")
        .def("add_pre_transform", &MssqlRepo::add_pre_transform,
             py::arg("fn"),
             "Add a callable invoked before each save/load operation.")
        .def("add_post_transform", &MssqlRepo::add_post_transform,
             py::arg("fn"),
             "Add a callable invoked after each save/load operation.")
        .def("clear_transforms", &MssqlRepo::clear_transforms,
             "Remove all pre and post transform hooks.")
        .def_property_readonly("format",
             [](MssqlRepo const& r) { return r.format(); })
        .def("__repr__", &MssqlRepo::repr);

    // Caller-owned transaction session
    py::class_<MssqlSession>(m, "DataStoreSession",
        "Caller-owned transaction over one pooled connection.\n\n"
        "Obtain via DataStore.session(). As a context manager it commits on\n"
        "clean exit, rolls back on exception, then closes (returning the\n"
        "connection to the pool) — note it closes on exit, unlike DB-API\n"
        "connection context managers.\n\n"
        "A session occupies one pool connection until closed. Sessions are\n"
        "not thread-safe: use one session per thread.")
        .def("save", &MssqlSession::save,
             py::arg("data"), py::arg("table_name"),
             py::kw_only(),
             py::arg("mode") = "append",
             py::arg("keys") = std::vector<std::string>{},
             "Save within the session transaction (same modes as DataStore.save;\n"
             "always single-connection BCP).")
        .def("load", &MssqlSession::load, py::arg("source"),
             "Load within the session (sees this transaction's uncommitted writes).")
        .def("commit", &MssqlSession::commit,
             "Commit the current transaction; the session remains usable.")
        .def("rollback", &MssqlSession::rollback,
             "Roll back the current transaction; the session remains usable.")
        .def("close", &MssqlSession::close,
             "Roll back anything uncommitted and return the connection to the pool.")
        .def_property_readonly("closed", &MssqlSession::closed)
        .def("__enter__", [](py::object self) { return self; })
        .def("__exit__",
             [](MssqlSession& s, py::object exc_type, py::object, py::object) {
                 // close() must run even when commit/rollback throws (commit
                 // failure is ordinary: deadlock victim, dropped connection):
                 // otherwise the session pins its pooled connection with a
                 // dangling transaction holding server locks.
                 if (!s.closed()) {
                     try {
                         if (exc_type.is_none()) {
                             s.commit();
                         } else {
                             s.rollback();
                         }
                     } catch (...) {
                         s.close();
                         throw;
                     }
                     s.close();
                 }
                 return false;  // never swallow exceptions
             })
        .def("__repr__", &MssqlSession::repr);

    // Factory function
    m.def("acquire_datastore", &acquire_datastore,
          py::arg("conn_str"),
          py::arg("format") = "polars",
          py::arg("pool_size") = 4,
          py::arg("batch_size") = 100000,
          py::arg("table_hint") = "TABLOCK",
          py::arg("bcp_workers") = 1,
          py::arg("block_size") = 4096,
          py::arg("packet_size") = 16384,
          py::kw_only(),
          py::arg("access_token") = py::none(),
          R"doc(
          Create a DataStore from a connection string.

          Parameters
          ----------
          conn_str : str
              Connection string.
          format : str
              Output format: "polars" (default) or "pandas".
          pool_size : int
              Maximum pooled connections (default: 4).
          batch_size : int
              BCP batch size (default: 100000).
          table_hint : str
              BCP table hint (default: "TABLOCK").
          bcp_workers : int
              Number of parallel BCP workers (default: 1).
          block_size : int
              Block cursor size for load operations (default: 4096).
          packet_size : int
              ODBC connection packet size (default: 16384).
          access_token : str | bytes | Callable[[], str | bytes] | None
              Entra ID / Managed Identity access token for Azure SQL.
              Pass the RAW token (pygim performs the SQL_COPT_SS_ACCESS_TOKEN
              packing); a callable is invoked per physical connect, so
              short-lived tokens keep working as pooled connections are
              (re)created. Do not combine with UID/PWD/Trusted_Connection
              in conn_str. Currently requires bcp_workers=1/load_workers=1.
          )doc");
}
