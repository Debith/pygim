#pragma once
// pathlike/adapter/path_store_bindings.h — Python's PathStore: a path_store (../path_store.h) with
// a Python face, and the module's default store.
//
//     store = PathStore()
//     p = path("a/b.yaml", store=store)     rows in store's table; p.store is store's table
//     store.stats()                          -> rows, segments, bytes
//     default_store()                        -> the module's default (what path(text) uses)

#include <cstdint>
#include <string>

#include <pybind11/pybind11.h>

#include "../path_store.h"

namespace pygim::pathlike {

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
