#pragma once
// pathlike/adapter/path_store.h — the flyweight store: one path_table per store,
// at most one live Python object per row.
//
// path() and every derived-path operation (parent, /, with_suffix, glob, ...)
// go through the CURRENT store: the value is interned in the store's
// path_table (path_table.h, docs/design/pathset_storage.md) and the row's
// live Python object is handed back when there is one — so equal paths made
// through path() are the SAME object for as long as any reference keeps it
// alive. The per-row slot holds a weak reference: the store pins nothing,
// objects die when their last owner drops them, and the slot is refilled on
// the next request. Only the table grows (rows are never freed), which is why
// a store is an ordinary Python object rather than a hidden global: hand one
// to an IoC container as a singleton and its lifetime is the container's;
// use_store() makes it current for a block.
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
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>

#include "../path_table.h"
#include "adapter.h"

namespace pygim::pathlike {

namespace detail {
// The referent of a weakref as a new reference, or a null object once it died.
inline py::object weak_referent(PyObject* wr) {
#if PY_VERSION_HEX >= 0x030D0000
    PyObject* obj = nullptr;
    const int rc = PyWeakref_GetRef(wr, &obj);   // 1 alive (new ref), 0 dead, -1 error
    if (rc < 0) throw py::error_already_set();
    return rc == 1 ? py::reinterpret_steal<py::object>(obj) : py::object();
#else
    PyObject* obj = PyWeakref_GetObject(wr);   // borrowed; Py_None once dead
    if (!obj) throw py::error_already_set();
    return obj == Py_None ? py::object() : py::reinterpret_borrow<py::object>(obj);
#endif
}
}  // namespace detail

class path_store {
public:
    path_store() : m_table(std::make_shared<path_table>()), m_id(next_id()) {}
    path_store(const path_store&) = delete;
    path_store& operator=(const path_store&) = delete;
    path_store(path_store&& o) noexcept : m_table(std::move(o.m_table)), m_slots(std::move(o.m_slots)), m_id(o.m_id) {}
    path_store& operator=(path_store&&) = delete;
    ~path_store() {
        for (PyObject* wr : m_slots) Py_XDECREF(wr);   // pybind11 destroys instances under the GIL
    }

    [[nodiscard]] const std::shared_ptr<path_table>& table() const noexcept { return m_table; }
    [[nodiscard]] std::uint32_t intern(const file& f) { return m_table->insert<native_strategy>(f.value()); }
    [[nodiscard]] std::uint32_t intern(std::string_view native_text) { return m_table->insert<native_strategy>(native_text); }

    // The live object for row `r` (a new reference), or null.
    [[nodiscard]] py::object live(std::uint32_t r) const {
        if (r >= m_slots.size() || !m_slots[r]) return py::object();
        return detail::weak_referent(m_slots[r]);
    }
    void remember(std::uint32_t r, py::handle obj) {
        if (r >= m_slots.size()) m_slots.resize(static_cast<std::size_t>(r) + 1, nullptr);
        PyObject* wr = PyWeakref_NewRef(obj.ptr(), nullptr);
        if (!wr) throw py::error_already_set();
        Py_XDECREF(m_slots[r]);
        m_slots[r] = wr;
        py::cast<file&>(obj).set_interned({m_id, r});
    }
    // The row of an object THIS store handed out, validated through the slot
    // (a copied token in another object fails the referent check); `none` otherwise.
    [[nodiscard]] std::uint32_t row_of(py::handle obj, const file& f) const {
        const file::intern_token t = f.interned();
        if (t.owner != m_id || t.slot >= m_slots.size() || !m_slots[t.slot]) return path_table::none;
        const py::object referent = detail::weak_referent(m_slots[t.slot]);
        return referent && referent.ptr() == obj.ptr() ? t.slot : path_table::none;
    }
    void reserve(std::size_t n) {
        m_table->reserve(m_table->size() + 3 * n, m_table->segments().size() + n);
        m_slots.reserve(m_slots.size() + n);
    }

    [[nodiscard]] std::size_t live_count() const {
        std::size_t n = 0;
        for (PyObject* wr : m_slots) {
            if (wr && detail::weak_referent(wr)) ++n;
        }
        return n;
    }
    [[nodiscard]] std::size_t slot_bytes() const noexcept { return m_slots.capacity() * sizeof(PyObject*); }

private:
    [[nodiscard]] static std::uint64_t next_id() noexcept {
        static std::uint64_t n = 0;
        return ++n;   // under the GIL
    }
    std::shared_ptr<path_table> m_table;
    std::vector<PyObject*> m_slots;   // per row: a weakref (owned) or nullptr
    std::uint64_t m_id;
};

// ── the current store ───────────────────────────────────────────────────────
// Held in a leaked py::object (never destroyed after the interpreter is gone);
// the raw pointer beside it keeps the hot path free of a cast per call.
struct current_store_state {
    py::object object;
    path_store* store = nullptr;
};
inline current_store_state& current_store_slot() {
    static auto* s = new current_store_state{};
    return *s;
}
inline path_store& current_store() { return *current_store_slot().store; }
inline path_store& as_store(py::handle obj) {
    if (!py::isinstance<path_store>(obj)) throw py::type_error("expected a pathlike.PathStore, got " + py::repr(py::type::of(obj)).cast<std::string>());
    return py::cast<path_store&>(obj);
}
inline py::object set_current_store(py::object store) {
    path_store& st = as_store(store);
    current_store_state& s = current_store_slot();
    py::object prev = std::move(s.object);
    s.object = std::move(store);
    s.store = &st;
    return prev;
}

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

// with use_store(store): ... — makes `store` current for the block.
struct store_scope {
    py::object store;
    py::object previous;
};

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
            return d;
        }, "rows / segments in the table, live objects, and the bytes both sides hold.")
        .def("__repr__", [](const path_store& st) {
            return "PathStore(rows=" + std::to_string(st.table()->size()) + ", live=" + std::to_string(st.live_count()) + ")";
        });

    py::class_<store_scope>(m, "_StoreScope")
        .def("__enter__", [](store_scope& s) {
            s.previous = set_current_store(s.store);
            return s.store;
        })
        .def("__exit__", [](store_scope& s, py::handle, py::handle, py::handle) {
            set_current_store(std::move(s.previous));
            return false;
        });

    m.def("store", []() { return current_store_slot().object; },
          "The current PathStore: what path() and derived-path operations intern into.");
    m.def("use_store", [](py::object store) {
            as_store(store);   // validate now, not at __enter__
            return store_scope{std::move(store), py::object()};
        }, py::arg("store"),
        "Context manager: `with use_store(s): ...` makes `s` the current store for the block "
        "(nested; restores the previous one on exit). Give an IoC container a PathStore singleton "
        "and use_store(container.resolve(PathStore)) scopes every path to that container.");

    // The default store: a module attribute (cleaned up by the interpreter) and the current one.
    py::object default_store = py::cast(path_store());
    m.attr("_default_store") = default_store;
    set_current_store(default_store);
}

}  // namespace pygim::pathlike
