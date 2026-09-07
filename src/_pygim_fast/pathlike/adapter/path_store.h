#pragma once
// pathlike/adapter/path_store.h — the flyweight store: one path_table per store,
// at most one live Python object per row.
//
// path() and every derived-path operation (parent, /, with_suffix, glob, ...)
// go through the CURRENT store: the value is interned in the store's
// path_table (path_table.h, docs/design/pathset_storage.md) and the row's
// live Python object is handed back when there is one — so equal paths made
// through path() are the SAME object for as long as any reference keeps it
// alive. The per-row slots are the toolkit's weak_slots
// (utils/flyweight_adapter.h: weak references, so the store pins nothing) and
// "the current store" is an ambient service (utils/ambient_adapter.h: an
// ordinary Python object — hand one to an IoC container as a singleton — that
// use_store() makes current for a block). Only the table grows.
//
// A pinned path (engine=...) is not interned: a pin is a per-object choice,
// not part of the value, and the slot holds the value's default wrap.
// file(...) — the class constructor — is the raw value constructor and never
// interns; path() is the interning entry point (as str vs sys.intern).
//
// Interning and slot refills happen on the thread holding the GIL: the
// single-writer rule of path_table (docs/design/pathset_storage.md).
//
// A worked example, used in the comments below (Python on the left, what
// happens here on the right; the store is the module's default, owner 1):
//
//     p = path("a/b.yaml")       make_text: tokenised into the table -> row 2 (rows 0 ".", 1 a);
//                                slot 2 empty -> wrap the row's value as a yamlfile, remember(2, p):
//                                p's value now carries token {1, 2}
//     path("a//b.yaml/") is p    -> True   row 2 again, slot 2 alive: the same object, no value built
//     del p; gc.collect()        slot 2's weakref dies; the row stays
//     path("a/b.yaml")           row 2, slot 2 dead -> a NEW yamlfile, remembered again
//     q = p.parent               parent_of: row_of(p) -> 2 (token valid: slot 2's referent is p),
//                                table.parent(2) -> 1, object_for(1): the object for "a"
//     p.parent is q              -> True
//     p / "c"                    joined_with: "c" is one plain component -> child_of(2, "c") -> row 3
//     path("a/b.yaml", engine="toml")   pinned: never interned, a fresh tomlfile each time
//     file("a/b.yaml") is p      -> False  the raw constructor: a new object with the same value
//
//     with use_store(PathStore()):      the ambient store swapped for the block
//         path("a/b.yaml") is p  -> False  a different table, a different object
//     path("a/b.yaml") is p      -> True   restored

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <pybind11/pybind11.h>

#include "../../utils/ambient_adapter.h"
#include "../../utils/flyweight_adapter.h"
#include "../path_table.h"
#include "adapter.h"

namespace pygim::pathlike {

/// A path_table plus one weak slot per row: the two caches of the flyweight,
/// the C++ one (values, in the table) and the Python one (objects, in the
/// slots). Python sees it as `pathlike.PathStore`.
class path_store {
public:
    /// A fresh store: an empty table, an empty slot holder with a new owner number.
    path_store() : m_table(std::make_shared<path_table>()) {}
    path_store(const path_store&) = delete;
    path_store& operator=(const path_store&) = delete;
    /// Movable (pybind11 moves the constructed value into its instance); not assignable.
    path_store(path_store&&) noexcept = default;
    path_store& operator=(path_store&&) = delete;

    /// The table (shared with any PathSet made over this store).
    [[nodiscard]] const std::shared_ptr<path_table>& table() const noexcept { return m_table; }
    /// The row for a value, created when absent.           intern(file("a/b.yaml")) -> 2
    [[nodiscard]] std::uint32_t intern(const file& f) { return m_table->insert<native_strategy>(f.value()); }
    /// The row for native text, created when absent — tokenised straight in,
    /// no value built.                                      intern("a//b.yaml/") -> 2
    [[nodiscard]] std::uint32_t intern(std::string_view native_text) { return m_table->insert<native_strategy>(native_text); }

    /// The live object for row `r` (a new reference), or null (weak_slots::live).
    [[nodiscard]] py::object live(std::uint32_t r) const { return m_objects.live(r); }
    /// Records `obj` as row `r`'s object and stamps its value (weak_slots::remember).
    void remember(std::uint32_t r, py::handle obj) { m_objects.remember(r, obj); }
    /// The row of an object THIS store handed out (validated through the
    /// slot: weak_slots::id_of); `none` for a raw file, a pinned file, an
    /// object from another store, or a copied token.        row_of(p, p.value) -> 2
    [[nodiscard]] std::uint32_t row_of(py::handle obj, const file& f) const { return m_objects.id_of(obj, f); }

    /// Room for `n` more paths in the table (PathSet's rule: ~3 rows and ~1
    /// segment per path) and `n` more slots.
    void reserve(std::size_t n) {
        m_table->reserve(m_table->size() + 3 * n, m_table->segments().size() + n);
        m_objects.reserve(n);
    }
    /// How many rows currently have a live object (O(slots); for stats).
    [[nodiscard]] std::size_t live_count() const { return m_objects.live_count(); }
    /// The bytes the slots hold (PathStore.stats()["slot_bytes"]).
    [[nodiscard]] std::size_t slot_bytes() const noexcept { return m_objects.bytes(); }

private:
    std::shared_ptr<path_table> m_table;
    adapter::weak_slots<file> m_objects;
};

/// The ambient current store (utils/ambient_adapter.h): `current::current()`
/// is what every entry point below reads.
using current = adapter::ambient<path_store>;
/// The current store: one pointer read.
inline path_store& current_store() noexcept { return current::current(); }
/// `obj` as a store, or a TypeError naming both types (PathSet(store=...)).
inline path_store& as_store(py::handle obj) { return current::as(obj); }

// ── the flyweight entry points ──────────────────────────────────────────────
// Three ways in — a row, a value, path text — all ending at the same slot.

/// The object for row `r` of `st`: the live one, else a fresh wrap of the
/// row's value (path_table::value, then the typed class its extension
/// resolves to), remembered in the slot. What a fileview over the store's
/// table becomes, and the tail of every entry point below.
///
///     object_for(es, st, 2)   -> p while p lives (a new reference to it), else a new yamlfile for row 2
template <class... Es>
[[nodiscard]] py::object object_for(engine_list<Es...> es, path_store& st, std::uint32_t r) {
    if (py::object o = st.live(r)) return o;
    py::object o = wrap(es, file(st.table()->value(r)));
    st.remember(r, o);
    return o;
}
/// The object for a value: interned unless pinned. A pinned value is wrapped
/// fresh every time (a pin is per object, not per value, and the slot keeps
/// the default wrap). Every derived-path operation in bindings.cpp ends here
/// through wrap(file).
///
///     make(es, st, file("a/b.yaml"))                 -> p (interned: row 2's object)
///     make(es, st, file("a/b.yaml", &toml::info))    -> a fresh tomlfile, nothing remembered
template <class... Es>
[[nodiscard]] py::object make(engine_list<Es...> es, path_store& st, file f) {
    if (f.pinned()) return wrap(es, std::move(f));
    const std::uint32_t r = st.intern(f);
    if (py::object o = st.live(r)) return o;
    py::object o = wrap(es, std::move(f));
    st.remember(r, o);
    return o;
}
/// make() against the current store.
template <class... Es>
[[nodiscard]] py::object make(engine_list<Es...> es, file f) {
    return make(es, current_store(), std::move(f));
}
/// The object for path TEXT — `pygim.path(s)`. Native text (no scheme — the
/// constructor's own rule) is tokenised straight into the table, so a hit
/// builds no value at all (~370 ns against ~790 for the raw constructor at
/// 200k paths); a file:// URI or a pinned path takes the constructor route,
/// which owns the URI rules.
///
///     make_text(es, st, "a//b.yaml/", nullptr)         -> p       (row 2, slot alive)
///     make_text(es, st, "file:///a/b.yaml", nullptr)   -> parsed by file(), then make()
///     make_text(es, st, "a/b.yaml", &toml::info)       -> a fresh tomlfile
template <class... Es>
[[nodiscard]] py::object make_text(engine_list<Es...> es, path_store& st, std::string_view text, const engine_info* pin) {
    if (pin || !uri::scheme_of(text).empty()) return make(es, st, file(text, pin));
    return object_for(es, st, st.intern(text));
}

// ── derived paths by row ────────────────────────────────────────────────────
// When the object is one the current store handed out (and not pinned: a pin
// is inherited by derived values, which the row route cannot express), its
// parent, ancestors and single-component children are rows of the table —
// no value is built and nothing is re-probed segment by segment. Anything
// else takes the value route (basic_file's own algebra) and is interned.
// Parity between the two routes is tested over the corpus and the edge
// list (tests/unittests/test_path_store.py).

/// `p.parent`: path_table::parent(row) when p is the store's, else the value
/// route.                                                 parent_of(es, p, value) -> the object for row 1 ("a")
template <class... Es>
[[nodiscard]] py::object parent_of(engine_list<Es...> es, py::handle self, const file& f) {
    path_store& st = current_store();
    if (!f.pinned()) {
        if (const std::uint32_t r = st.row_of(self, f); r != path_table::none) return object_for(es, st, st.table()->parent(r));
    }
    return make(es, st, f.parent());
}
/// `p.parents`: the ancestor rows (path_table::parents, pathlib's rule) as
/// objects, closest first.                                parents_of(es, p, value) -> [object for "a"]
template <class... Es>
[[nodiscard]] py::list parents_of(engine_list<Es...> es, py::handle self, const file& f) {
    path_store& st = current_store();
    py::list out;
    if (!f.pinned()) {
        if (const std::uint32_t r = st.row_of(self, f); r != path_table::none) {
            for (const std::uint32_t p : st.table()->parents(r)) out.append(object_for(es, st, p));
            return out;
        }
    }
    for (file& p : f.parents()) out.append(make(es, st, std::move(p)));
    return out;
}
/// One plain component: no separator, not empty, not "." (dropped by the join),
/// not a Windows drive spelling. ".." IS a plain component, as in pathlib.
///
///     plain_component("c") -> true     plain_component("..") -> true
///     plain_component("c/d") -> false  plain_component("") -> false   plain_component("d:") -> false
[[nodiscard]] inline bool plain_component(std::string_view s) noexcept {
    if (s.empty() || s == ".") return false;
    if (s.size() >= 2 && s[1] == ':') return false;
    for (const char c : s) {
        if (native_strategy::is_sep(c)) return false;
    }
    return true;
}
/// `p / other` and each step of `p.joinpath(...)`: path_table::child_of(row,
/// other) when p is the store's and `other` is one plain component (one
/// interner lookup, one trie probe), else basic_file::joined and the value
/// route — absolute right-hand sides, multi-component text, "." and "" all
/// keep pathlib's rules there.
///
///     joined_with(es, p, value, "c")     -> the object for row 3 (a/b.yaml/c)
///     joined_with(es, p, value, "x/y")   -> make(file("a/b.yaml").joined("x/y"))
template <class... Es>
[[nodiscard]] py::object joined_with(engine_list<Es...> es, py::handle self, const file& f, std::string_view other) {
    path_store& st = current_store();
    if (!f.pinned() && plain_component(other)) {
        if (const std::uint32_t r = st.row_of(self, f); r != path_table::none) return object_for(es, st, st.table()->child_of(r, other));
    }
    return make(es, st, f.joined(other));
}

/// Binds PathStore, store(), use_store() and the scope class into the
/// pathlike module, creates the default store as `_default_store` (a module
/// attribute, so the interpreter cleans it up) and makes it current.
template <class... Es>
void bind_path_store(engine_list<Es...>, py::module_& m) {
    using Engines_ = engine_list<Es...>;

    py::class_<path_store>(m, "PathStore",
        "The flyweight store behind path(): a path_table interning every path value made "
        "through path() and derived-path operations, plus one weak slot per row for the "
        "row's live Python object — so equal paths are the same object while anything "
        "holds them. Rows are never freed: a store lives as long as its owner (a variable, "
        "an IoC container singleton); use_store() makes one current for a block.")
        .def(py::init<>())
        .def("path", [](py::object self, py::handle p) {
                 const text_arg t = text_view_of_arg(p);
                 return make_text(Engines_{}, py::cast<path_store&>(self), t.view, nullptr);
             }, py::arg("path"), "The interned file for `path` in THIS store (never pinned).")
        .def("reserve", &path_store::reserve, py::arg("n"), "Room for `n` more paths without rehashing.")
        .def("stats", [](const path_store& st) {
            py::dict d;
            d["rows"] = st.table()->size();
            d["segments"] = st.table()->segments().size();
            d["live"] = st.live_count();
            d["table_bytes"] = st.table()->bytes();
            d["slot_bytes"] = st.slot_bytes();
            d["bytes"] = st.table()->bytes() + st.slot_bytes();   // the total, the same key on every component
            return d;
        }, "rows / segments in the table, live objects, table_bytes + slot_bytes and their total `bytes`.")
        .def("__repr__", [](const path_store& st) {
            return "PathStore(rows=" + std::to_string(st.table()->size()) + ", live=" + std::to_string(st.live_count()) + ")";
        });

    // The default store: a module attribute (cleaned up by the interpreter) and the current one.
    py::object default_store = py::cast(path_store());
    m.attr("_default_store") = default_store;
    current::bind(m, "_StoreScope",
                  "store", "The current PathStore: what path() and derived-path operations intern into.",
                  "use_store",
                  "Context manager: `with use_store(s): ...` makes `s` the current store for the block "
                  "(nested; restores the previous one on exit). Give an IoC container a PathStore singleton "
                  "and use_store(container.resolve(PathStore)) scopes every path to that container.",
                  default_store);
}

}  // namespace pygim::pathlike
