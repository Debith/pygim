#pragma once
// pathlike/path_store.h — a path_table with an owner: the table a set of paths lives in.
//
// Every path (pathview.h) is a handle on a row of some table. Which table is chosen ONCE, at
// construction, and inherited by every path derived from it, so a program picks a lifetime by
// argument and never through global state. A table lives as long as any handle or set over it:
// a server that makes its own store and passes it to every request drops the whole table, rows
// and all, when it stops and the last handle is gone.
//
// Rows are never freed within a table (docs/design/pathset_storage.md), which is the whole reason
// a store is a plain object with an owner rather than a hidden global. Inserts have a single
// writer at a time (from Python, the thread holding the GIL).
//
//     path_store store;                                // a fresh table
//     store.table()->insert<native_strategy>("a/b.yaml");
//     default_table()                                  // the module's default: what a path without a store uses
//
// CORE layer: pybind-free. Python's PathStore is adapter/path_store_bindings.h.

#include <cstddef>
#include <memory>
#include <utility>

#include "path_table.h"

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
    void reserve(std::size_t n) { m_table->reserve_paths(n); }

private:
    table_ptr m_table;
};

/// The module's default table: what `path(text)` without a store interns into.
/// A plain C++ static (no Python reference), so its destruction at exit is safe.
[[nodiscard]] inline const table_ptr& default_table() {
    static const table_ptr t = std::make_shared<path_table>();
    return t;
}

}  // namespace pygim::pathlike
