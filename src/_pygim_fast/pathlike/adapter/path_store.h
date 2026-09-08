#pragma once
// pathlike/adapter/path_store.h — a PathStore is a path_table with a Python face.
//
// Every path object (pathview.h) is a handle on a row of some table. Which
// table is chosen ONCE, at construction — `path(text)` uses the module's
// default table, `path(text, store=s)` uses s's — and inherited by every path
// derived from it, so a program picks a lifetime by argument and never through
// global state. A table lives as long as any handle or set over it: a server
// that makes its own store and passes it to every request drops the whole
// table, rows and all, when it stops and the last handle is gone.
//
// Rows are never freed within a table (docs/design/pathset_storage.md), which
// is the whole reason a store is a plain object with an owner rather than a
// hidden global. Inserts happen under the GIL (the single writer); a store
// shared between threads is safe to read and to insert into from any thread
// that holds it.
//
//     store = PathStore()
//     p = path("a/b.yaml", store=store)     rows in store's table; p.store is store's table
//     store.stats()                          -> rows, segments, bytes
//     default_store()                        -> the module's default (what path(text) uses)

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <pybind11/pybind11.h>

#include "../path_table.h"

namespace pygim::pathlike {

using table_ptr = std::shared_ptr<path_table>;

/// A table holder. `PathStore()` makes a fresh table; the module's default
/// store wraps the default table.
class path_store {
public:
    path_store() : m_table(std::make_shared<path_table>()) {}
    explicit path_store(table_ptr t) : m_table(std::move(t)) {}

    [[nodiscard]] const table_ptr& table() const noexcept { return m_table; }
    /// Room for `n` more paths (~3 rows and ~1 distinct segment per path).
    void reserve(std::size_t n) { m_table->reserve(m_table->size() + 3 * n, m_table->segments().size() + n); }

private:
    table_ptr m_table;
};

/// The module's default table: what `path(text)` without a store interns into.
/// A plain C++ static (no Python reference), so its destruction at exit is safe.
[[nodiscard]] inline const table_ptr& default_table() {
    static const table_ptr t = std::make_shared<path_table>();
    return t;
}
/// `obj` as a store, or a TypeError naming both types.
[[nodiscard]] inline path_store& as_store(py::handle obj) {
    if (!py::isinstance<path_store>(obj)) {
        throw py::type_error("expected a pathlike.PathStore, got " + py::str(py::type::of(obj)).cast<std::string>());
    }
    return py::cast<path_store&>(obj);
}

inline void bind_path_store(py::module_& m) {
    py::class_<path_store>(m, "PathStore",
        "A table of paths: every distinct component once, every path a row. path(text, store=s) "
        "interns into s's table and every path derived from that one stays there, so a store is "
        "how a program scopes a lifetime — pass one, keep it, drop it. Rows are never freed within "
        "a table; the table is freed with its last handle. PathStore() is a fresh table; "
        "default_store() is the one path(text) uses.")
        .def(py::init<>())
        .def("reserve", &path_store::reserve, py::arg("n"), "Room for `n` more paths without rehashing.")
        .def("stats", [](const path_store& st) {
            py::dict d;
            d["rows"] = st.table()->size();
            d["segments"] = st.table()->segments().size();
            d["bytes"] = st.table()->bytes();   // the total, the same key on every component
            return d;
        }, "rows and segments in the table, and the bytes it holds.")
        .def("__eq__", [](const path_store& a, const path_store& b) { return a.table().get() == b.table().get(); }, py::is_operator())
        .def("__hash__", [](const path_store& st) { return static_cast<py::ssize_t>(reinterpret_cast<std::uintptr_t>(st.table().get()) >> 4); })
        .def("__repr__", [](const path_store& st) {
            return "PathStore(rows=" + std::to_string(st.table()->size()) + ")";
        });

    // The default store: a module attribute over the default table.
    m.attr("_default_store") = py::cast(path_store(default_table()));
    m.def("default_store", [m]() { return m.attr("_default_store"); },
          "The module's default PathStore: what path(text) without store= interns into.");
}

}  // namespace pygim::pathlike
