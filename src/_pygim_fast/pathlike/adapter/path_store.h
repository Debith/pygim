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

class path_store {
public:
    path_store() : m_table(std::make_shared<path_table>()) {}
    path_store(const path_store&) = delete;
    path_store& operator=(const path_store&) = delete;
    path_store(path_store&&) noexcept = default;
    path_store& operator=(path_store&&) = delete;

    [[nodiscard]] const std::shared_ptr<path_table>& table() const noexcept { return m_table; }
    [[nodiscard]] std::uint32_t intern(const file& f) { return m_table->insert<native_strategy>(f.value()); }
    [[nodiscard]] std::uint32_t intern(std::string_view native_text) { return m_table->insert<native_strategy>(native_text); }

    [[nodiscard]] py::object live(std::uint32_t r) const { return m_objects.live(r); }
    void remember(std::uint32_t r, py::handle obj) { m_objects.remember(r, obj); }
    // The row of an object THIS store handed out (validated through the slot); `none` otherwise.
    [[nodiscard]] std::uint32_t row_of(py::handle obj, const file& f) const { return m_objects.id_of(obj, f); }

    void reserve(std::size_t n) {
        m_table->reserve(m_table->size() + 3 * n, m_table->segments().size() + n);
        m_objects.reserve(n);
    }
    [[nodiscard]] std::size_t live_count() const { return m_objects.live_count(); }
    [[nodiscard]] std::size_t slot_bytes() const noexcept { return m_objects.bytes(); }

private:
    std::shared_ptr<path_table> m_table;
    adapter::weak_slots<file> m_objects;
};

using current = adapter::ambient<path_store>;
inline path_store& current_store() noexcept { return current::current(); }
inline path_store& as_store(py::handle obj) { return current::as(obj); }

// ── the flyweight entry points ──────────────────────────────────────────────
// The object for row `r` of `st`: the live one, else a fresh wrap of the row's
// value, remembered in the slot.
template <class... Es>
[[nodiscard]] py::object object_for(engine_list<Es...> es, path_store& st, std::uint32_t r) {
    if (py::object o = st.live(r)) return o;
    py::object o = wrap(es, file(st.table()->value(r)));
    st.remember(r, o);
    return o;
}
// The object for a value: interned unless pinned.
template <class... Es>
[[nodiscard]] py::object make(engine_list<Es...> es, path_store& st, file f) {
    if (f.pinned()) return wrap(es, std::move(f));
    const std::uint32_t r = st.intern(f);
    if (py::object o = st.live(r)) return o;
    py::object o = wrap(es, std::move(f));
    st.remember(r, o);
    return o;
}
template <class... Es>
[[nodiscard]] py::object make(engine_list<Es...> es, file f) {
    return make(es, current_store(), std::move(f));
}
// The object for path TEXT. Native text (no scheme — the constructor's own rule)
// is tokenised straight into the table, so a hit builds no value at all; a
// file:// URI or a pinned path takes the constructor route.
template <class... Es>
[[nodiscard]] py::object make_text(engine_list<Es...> es, path_store& st, std::string_view text, const engine_info* pin) {
    if (pin || !uri::scheme_of(text).empty()) return make(es, st, file(text, pin));
    return object_for(es, st, st.intern(text));
}

// ── derived paths by row ────────────────────────────────────────────────────
// When the object is one the current store handed out (and not pinned: a pin
// is inherited by derived values, which the row route cannot express), its
// parent, ancestors and single-component children are rows of the table —
// no value is built and nothing is re-probed segment by segment.
template <class... Es>
[[nodiscard]] py::object parent_of(engine_list<Es...> es, py::handle self, const file& f) {
    path_store& st = current_store();
    if (!f.pinned()) {
        if (const std::uint32_t r = st.row_of(self, f); r != path_table::none) return object_for(es, st, st.table()->parent(r));
    }
    return make(es, st, f.parent());
}
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
// One plain component: no separator, not empty, not "." (dropped by the join),
// not a Windows drive spelling. ".." IS a plain component, as in pathlib.
[[nodiscard]] inline bool plain_component(std::string_view s) noexcept {
    if (s.empty() || s == ".") return false;
    if (s.size() >= 2 && s[1] == ':') return false;
    for (const char c : s) {
        if (native_strategy::is_sep(c)) return false;
    }
    return true;
}
template <class... Es>
[[nodiscard]] py::object joined_with(engine_list<Es...> es, py::handle self, const file& f, std::string_view other) {
    path_store& st = current_store();
    if (!f.pinned() && plain_component(other)) {
        if (const std::uint32_t r = st.row_of(self, f); r != path_table::none) return object_for(es, st, st.table()->child_of(r, other));
    }
    return make(es, st, f.joined(other));
}

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
