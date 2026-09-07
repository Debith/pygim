#pragma once
// pathlike/adapter/pathset.h — PathSet: many paths as one table, seen through views (prototype).
//
// PathSet owns a shared path_table (path_table.h) and a member list. Nothing
// here is a Python object per path: iteration hands out `fileview`s — a
// (table, row) pair that copies no text — and scan() reuses ONE view object
// for a whole pass. Value filters and set algebra work on rows; a filtered
// set shares its parent's table, so it is a mapping::id_set (members and a
// bitmap), nothing more. The way out to other tools is to_list(): see
// docs/design/pathset_storage.md for why there is no Arrow boundary here.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>

#include "../../mapping/id_set.h"
#include "../path_table.h"
#include "adapter.h"
#include "path_store.h"

namespace pygim::pathlike {

using table_ptr = std::shared_ptr<path_table>;

// A path inside a table. Copies nothing; keeps the table alive, so a view
// outlives the set it came from.
struct fileview {
    std::shared_ptr<const path_table> table;
    std::uint32_t row = 0;

    [[nodiscard]] file to_file() const { return file(table->value(row)); }
    [[nodiscard]] std::string fspath() const { return table->render<native_strategy>(row); }
    [[nodiscard]] bool equals(const fileview& o) const {
        if (table.get() == o.table.get()) return row == o.row;
        return table->find(*o.table, o.row) == row;
    }
};

class PathSet {
public:
    PathSet() : m_table(std::make_shared<path_table>()) {}
    explicit PathSet(table_ptr t) : m_table(std::move(t)) {}

    [[nodiscard]] std::size_t size() const noexcept { return m_ids.size(); }
    [[nodiscard]] const table_ptr& table() const noexcept { return m_table; }
    [[nodiscard]] const std::vector<std::uint32_t>& members() const noexcept { return m_ids.members(); }
    [[nodiscard]] fileview view(std::size_t i) const { return {m_table, m_ids[i]}; }

    // Room for `n` more paths: sizes the table's hash tables once instead of
    // letting them double their way up (a fresh table's rows run ~2-3x the
    // path count, its distinct segments about 1x).
    void reserve(std::size_t n) {
        m_ids.reserve(m_ids.size() + n);
        m_table->reserve(m_table->size() + 3 * n, m_table->segments().size() + n);
    }

    // ── adding (find-or-add in the table, then membership) ────────────────
    void add_text(std::string_view text) { note(m_table->insert<native_strategy>(text)); }
    void add_value(const uri& u) { note(m_table->insert<native_strategy>(u)); }
    void add_view(const fileview& v) {
        note(v.table.get() == m_table.get() ? v.row : m_table->insert_from(*v.table, v.row));
    }

    // ── membership ────────────────────────────────────────────────────────
    [[nodiscard]] bool has_row(std::uint32_t r) const noexcept { return r != path_table::none && m_ids.has(r); }
    [[nodiscard]] bool contains_text(std::string_view t) const { return has_row(m_table->find<native_strategy>(t)); }
    [[nodiscard]] bool contains_value(const uri& u) const { return has_row(m_table->find<native_strategy>(u)); }
    [[nodiscard]] bool contains_view(const fileview& v) const {
        return has_row(v.table.get() == m_table.get() ? v.row : m_table->find(*v.table, v.row));
    }

    // ── value filters: a new set over the SAME table ──────────────────────
    template <class Pred>
    [[nodiscard]] PathSet where(Pred pred) const {
        PathSet out(m_table);
        out.m_ids = m_ids.where(pred);
        return out;
    }
    [[nodiscard]] PathSet filter_suffix(std::string_view s) const {
        return where([&](std::uint32_t r) { return detail::suffix_of(m_table->name(r)) == s; });
    }
    [[nodiscard]] PathSet filter_name(std::string_view glob) const {
        return where([&](std::uint32_t r) { return detail::glob_match(glob, m_table->name(r)); });
    }
    [[nodiscard]] PathSet filter_absolute() const {
        return where([&](std::uint32_t r) { return m_table->is_absolute<native_strategy>(r); });
    }

    // ── set algebra (bitmaps when the tables are shared, row mapping otherwise) ──
    [[nodiscard]] PathSet union_with(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) {
            PathSet out(m_table);
            out.m_ids = m_ids.united(o.m_ids);
            return out;
        }
        // Two tables: copy the larger one (a memcpy of flat arrays) and map only
        // the other set's rows into it. Member order stays "mine, then theirs".
        const bool keep_mine = m_table->size() >= o.m_table->size();
        const PathSet& kept = keep_mine ? *this : o;
        const PathSet& mapped = keep_mine ? o : *this;
        PathSet out(std::make_shared<path_table>(*kept.m_table));
        out.m_ids = kept.m_ids.sibling();
        path_table::row_map into(*out.m_table, *mapped.m_table, out.m_table.get());
        if (keep_mine) {
            for (const std::uint32_t r : m_ids.members()) out.note(r);
            for (const std::uint32_t r : o.m_ids.members()) out.note(into(r));
        } else {
            for (const std::uint32_t r : m_ids.members()) out.note(into(r));
            for (const std::uint32_t r : o.m_ids.members()) out.note(r);
        }
        return out;
    }
    [[nodiscard]] PathSet intersection(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) {
            PathSet out(m_table);
            out.m_ids = m_ids.intersected(o.m_ids);
            return out;
        }
        path_table::row_map in_o(*o.m_table, *m_table);
        return where([&](std::uint32_t r) { return o.has_row(in_o(r)); });
    }
    [[nodiscard]] PathSet difference(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) {
            PathSet out(m_table);
            out.m_ids = m_ids.subtracted(o.m_ids);
            return out;
        }
        path_table::row_map in_o(*o.m_table, *m_table);
        return where([&](std::uint32_t r) { return !o.has_row(in_o(r)); });
    }

    // |this ∪ o|, |this ∩ o|, |this ∖ o| without building the set: a popcount
    // over the bitmaps when the tables are shared; otherwise the other set's
    // rows are mapped once (no table copy, no result set).
    [[nodiscard]] std::size_t count_union(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) return m_ids.count_united(o.m_ids);
        return size() + o.size() - count_intersection(o);
    }
    [[nodiscard]] std::size_t count_intersection(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) return m_ids.count_intersected(o.m_ids);
        path_table::row_map in_o(*o.m_table, *m_table);
        std::size_t n = 0;
        for (const std::uint32_t r : m_ids.members()) n += o.has_row(in_o(r)) ? 1u : 0u;
        return n;
    }
    [[nodiscard]] std::size_t count_difference(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) return m_ids.count_subtracted(o.m_ids);
        return size() - count_intersection(o);
    }

    [[nodiscard]] std::size_t member_bytes() const noexcept { return m_ids.bytes(); }

private:
    void note(std::uint32_t r) { m_ids.note(r); }

    table_ptr m_table;
    mapping::id_set m_ids;   // the members (insertion order) and their bitmap
};

// ── Python glue ─────────────────────────────────────────────────────────────
inline void add_one(PathSet& ps, py::handle item) {
    if (PyUnicode_Check(item.ptr()) || PyBytes_Check(item.ptr())) {
        const text_arg t = text_view_of_arg(item);
        ps.add_text(t.view);
        return;
    }
    if (py::isinstance<fileview>(item)) {
        ps.add_view(py::cast<const fileview&>(item));
        return;
    }
    if (py::isinstance<file>(item)) {
        ps.add_value(py::cast<const file&>(item).value());
        return;
    }
    const text_arg t = text_view_of_arg(item);
    ps.add_text(t.view);
}
inline void extend(PathSet& ps, py::handle iterable) {
    if (PySequence_Check(iterable.ptr()) || PyAnySet_Check(iterable.ptr()) || PyDict_Check(iterable.ptr())) {
        const py::ssize_t n = PyObject_Length(iterable.ptr());
        if (n < 0) PyErr_Clear();
        else ps.reserve(static_cast<std::size_t>(n));
    }
    for (const py::handle item : py::iter(iterable)) add_one(ps, item);
}
inline bool contains(const PathSet& ps, py::handle item) {
    if (PyUnicode_Check(item.ptr()) || PyBytes_Check(item.ptr())) {
        const text_arg t = text_view_of_arg(item);
        return ps.contains_text(t.view);
    }
    if (py::isinstance<fileview>(item)) return ps.contains_view(py::cast<const fileview&>(item));
    if (py::isinstance<file>(item)) return ps.contains_value(py::cast<const file&>(item).value());
    const text_arg t = text_view_of_arg(item);
    return ps.contains_text(t.view);
}

// Iteration: fresh views by default; scan() reuses one view object.
struct pathset_iter {
    py::object owner;      // keeps the set (and so the table) alive
    const PathSet* set;
    bool reuse;
    std::size_t i = 0;
    py::object current;    // the one view object scan() hands out

    pathset_iter(py::object o, const PathSet* s, bool r) : owner(std::move(o)), set(s), reuse(r) {}
};

template <class... Es>
void bind_pathset(engine_list<Es...>, py::module_& m) {
    using Engines_ = engine_list<Es...>;

    py::class_<fileview>(m, "fileview",
        "A path inside a PathSet: a (table, row) pair that copies nothing. Behaves like a file "
        "for reading the value (name, parent, parents, stem, suffix, suffixes, uri, engine, "
        "os.fspath, ==, hash — equal to and hashing like the same path as a file); to_file() "
        "gives the owning, typed file for everything else.")
        .def_property_readonly("name", [](const fileview& v) { return str_from_text(v.table->name(v.row)); })
        .def_property_readonly("stem", [](const fileview& v) { return str_from_text(detail::stem_of(v.table->name(v.row))); })
        .def_property_readonly("suffix", [](const fileview& v) { return str_from_text(detail::suffix_of(v.table->name(v.row))); })
        .def_property_readonly("suffixes", [](const fileview& v) {
            py::list out;
            for (const std::string& s : v.to_file().suffixes()) out.append(str_from_text(s));
            return out;
        })
        .def_property_readonly("parent", [](const fileview& v) { return fileview{v.table, v.table->parent(v.row)}; })
        .def_property_readonly("parents", [](const fileview& v) {
            py::list out;
            for (const std::uint32_t r : v.table->parents(v.row)) out.append(fileview{v.table, r});
            return out;
        })
        .def_property_readonly("depth", [](const fileview& v) { return v.table->depth(v.row); })
        .def_property_readonly("uri", [](const fileview& v) { return v.to_file().as_uri(); })
        .def_property_readonly("engine", [](const fileview& v) -> py::object {
            const file f = v.to_file();
            const engine_info* e = Engines_::resolved(f);
            if (!e) return py::none();
            return py::str(std::string(e->label));
        })
        .def("is_absolute", [](const fileview& v) { return v.table->is_absolute<native_strategy>(v.row); })
        .def("to_file", [](const fileview& v) {
                 path_store& st = current_store();
                 if (v.table.get() == st.table().get()) return object_for(Engines_{}, st, v.row);   // same table: no re-intern
                 return make(Engines_{}, st, v.to_file());
             },
             "The owning file (typed by its engine) with the same value, interned in the current store.")
        .def("__fspath__", [](const fileview& v) { return str_from_text(v.fspath()); })
        .def("__str__", [](const fileview& v) { return str_from_text(v.fspath()); })
        .def("__repr__", [](const fileview& v) { return "fileview(" + py::repr(str_from_text(v.fspath())).cast<std::string>() + ")"; })
        .def("__eq__", [](const fileview& a, const fileview& b) { return a.equals(b); }, py::is_operator())
        .def("__eq__", [](const fileview& a, const file& b) { return a.table->value(a.row) == b.value(); }, py::is_operator())
        .def("__hash__", [](const fileview& v) {
            const auto h = static_cast<py::ssize_t>(v.table->hash(v.row));
            return h == -1 ? py::ssize_t(-2) : h;
        });

    py::class_<pathset_iter>(m, "_PathSetIterator")
        .def("__iter__", [](py::object self) { return self; })
        .def("__next__", [](pathset_iter& it) -> py::object {
            if (it.i >= it.set->size()) throw py::stop_iteration();
            fileview v = it.set->view(it.i++);
            if (!it.reuse) return py::cast(std::move(v));
            if (!it.current) {
                it.current = py::cast(std::move(v));
            } else {
                py::cast<fileview&>(it.current).row = v.row;
            }
            return it.current;
        });

    py::class_<PathSet>(m, "PathSet",
        "Many paths as ONE table: every distinct path component is stored once and every path is "
        "a row (parent, name) in a hash-consed trie, so a path costs a few bytes plus its share of "
        "the unique names. Iterating yields fileviews (nothing copied); scan() reuses one view "
        "object per pass. filter_suffix()/filter_name() and |, &, - work on rows and return sets "
        "sharing the table; to_list() renders the members. PathSet(paths, store=s) shares the store's "
        "table, so its views and s.path() objects meet without re-interning. Prototype: insertion "
        "order, append-only.")
        .def(py::init([](py::object paths, py::object store) {
                 PathSet ps = store.is_none() ? PathSet() : PathSet(as_store(store).table());
                 if (!paths.is_none()) extend(ps, paths);
                 return ps;
             }), py::arg("paths") = py::none(), py::kw_only(), py::arg("store") = py::none())
        .def("add", [](PathSet& ps, py::handle p) { add_one(ps, p); }, py::arg("path"))
        .def("extend", [](PathSet& ps, py::handle it) { extend(ps, it); }, py::arg("paths"))
        .def("reserve", &PathSet::reserve, py::arg("n"),
             "Size the table for `n` more paths up front (what a sized iterable already does).")
        .def("__len__", &PathSet::size)
        .def("__contains__", [](const PathSet& ps, py::handle p) { return contains(ps, p); })
        .def("__iter__", [](py::object self) {
            return pathset_iter(self, &py::cast<const PathSet&>(self), false);
        })
        .def("scan", [](py::object self) {
            return pathset_iter(self, &py::cast<const PathSet&>(self), true);
        }, "Iterate reusing ONE fileview object (its row advances): no allocation per element. "
           "Do not keep the yielded object across iterations.")
        .def("__getitem__", [](const PathSet& ps, py::ssize_t i) {
            const auto n = static_cast<py::ssize_t>(ps.size());
            if (i < 0) i += n;
            if (i < 0 || i >= n) throw py::index_error("PathSet index out of range");
            return ps.view(static_cast<std::size_t>(i));
        })
        .def("filter_suffix", [](const PathSet& ps, std::string_view s) { return ps.filter_suffix(s); }, py::arg("suffix"),
             "The members whose final suffix equals `suffix` (pathlib's rule; case-sensitive).")
        .def("filter_name", [](const PathSet& ps, std::string_view g) { return ps.filter_name(g); }, py::arg("glob"),
             "The members whose name matches the glob (`*` and `?`).")
        .def("filter_absolute", &PathSet::filter_absolute)
        .def("__or__", &PathSet::union_with, py::is_operator())
        .def("__and__", &PathSet::intersection, py::is_operator())
        .def("__sub__", &PathSet::difference, py::is_operator())
        .def("count_union", &PathSet::count_union, py::arg("other"),
             "len(self | other) without building the set: a popcount over the bitmaps when the tables are shared.")
        .def("count_intersection", &PathSet::count_intersection, py::arg("other"),
             "len(self & other) without building the set.")
        .def("count_difference", &PathSet::count_difference, py::arg("other"),
             "len(self - other) without building the set.")
        .def("to_list", [](const PathSet& ps) {
            py::list out;
            for (const std::uint32_t r : ps.members()) out.append(str_from_text(ps.table()->render<native_strategy>(r)));
            return out;
        })
        .def("stats", [](const PathSet& ps) {
            py::dict d;
            d["members"] = ps.size();
            d["rows"] = ps.table()->size();
            d["segments"] = ps.table()->segments().size();
            d["table_bytes"] = ps.table()->bytes();
            d["member_bytes"] = ps.member_bytes();
            d["bytes"] = ps.table()->bytes() + ps.member_bytes();   // the total, the same key on every component
            return d;
        });
}

}  // namespace pygim::pathlike
