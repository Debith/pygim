#pragma once
// pathlike/adapter/pathset.h — PathSet: many paths as one table, seen through views (prototype).
//
// PathSet owns a shared path_table (path_table.h) and a member list. Nothing
// here is a Python object per path: iteration hands out `fileview`s — a
// (table, row) pair that copies no text — and scan() reuses ONE view object
// for a whole pass. Value filters and set algebra work on rows; a filtered
// set shares its parent's table, so it is a member list and a bitmap, nothing
// more. paths()/names() expose the columns through the Arrow C Data Interface
// (arrow_c_abi.h) inside PyCapsules, and from_arrow() reads any object that
// speaks the same protocol — no libarrow is linked and no Python Arrow
// library is called; pyarrow, polars and duckdb sit on the other side.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>

#include "../arrow_c_abi.h"
#include "../path_table.h"
#include "adapter.h"

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

    [[nodiscard]] std::size_t size() const noexcept { return m_members.size(); }
    [[nodiscard]] const table_ptr& table() const noexcept { return m_table; }
    [[nodiscard]] const std::vector<std::uint32_t>& members() const noexcept { return m_members; }
    [[nodiscard]] fileview view(std::size_t i) const { return {m_table, m_members[i]}; }

    // ── adding (find-or-add in the table, then membership) ────────────────
    void add_text(std::string_view text) { note(m_table->insert<native_strategy>(text)); }
    void add_value(const uri& u) { note(m_table->insert<native_strategy>(u)); }
    void add_view(const fileview& v) {
        note(v.table.get() == m_table.get() ? v.row : m_table->insert_from(*v.table, v.row));
    }

    // ── membership ────────────────────────────────────────────────────────
    [[nodiscard]] bool has_row(std::uint32_t r) const noexcept {
        return r != path_table::none && r / 64 < m_bits.size() && ((m_bits[r / 64] >> (r % 64)) & 1u);
    }
    [[nodiscard]] bool contains_text(std::string_view t) const { return has_row(m_table->find<native_strategy>(t)); }
    [[nodiscard]] bool contains_value(const uri& u) const { return has_row(m_table->find<native_strategy>(u)); }
    [[nodiscard]] bool contains_view(const fileview& v) const {
        return has_row(v.table.get() == m_table.get() ? v.row : m_table->find(*v.table, v.row));
    }

    // ── value filters: a new set over the SAME table ──────────────────────
    template <class Pred>
    [[nodiscard]] PathSet where(Pred pred) const {
        PathSet out(m_table);
        for (const std::uint32_t r : m_members) {
            if (pred(r)) out.note(r);
        }
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

    // ── set algebra (bitmap when the tables are shared, chain lookup otherwise) ──
    [[nodiscard]] PathSet union_with(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) {
            PathSet out(m_table);
            for (const std::uint32_t r : m_members) out.note(r);
            for (const std::uint32_t r : o.m_members) out.note(r);
            return out;
        }
        PathSet out;
        path_table::row_map mine(*out.m_table, *m_table, out.m_table.get());
        for (const std::uint32_t r : m_members) out.note(mine(r));
        path_table::row_map theirs(*out.m_table, *o.m_table, out.m_table.get());
        for (const std::uint32_t r : o.m_members) out.note(theirs(r));
        return out;
    }
    [[nodiscard]] PathSet intersection(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) return where([&](std::uint32_t r) { return o.has_row(r); });
        path_table::row_map in_o(*o.m_table, *m_table);
        return where([&](std::uint32_t r) { return o.has_row(in_o(r)); });
    }
    [[nodiscard]] PathSet difference(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) return where([&](std::uint32_t r) { return !o.has_row(r); });
        path_table::row_map in_o(*o.m_table, *m_table);
        return where([&](std::uint32_t r) { return !o.has_row(in_o(r)); });
    }

    [[nodiscard]] std::size_t member_bytes() const noexcept {
        return m_members.capacity() * sizeof(std::uint32_t) + m_bits.capacity() * sizeof(std::uint64_t);
    }

private:
    void note(std::uint32_t r) {
        if (r / 64 >= m_bits.size()) m_bits.resize(r / 64 + 1, 0);
        const std::uint64_t bit = std::uint64_t{1} << (r % 64);
        if (m_bits[r / 64] & bit) return;
        m_bits[r / 64] |= bit;
        m_members.push_back(r);
    }

    table_ptr m_table;
    std::vector<std::uint32_t> m_members;   // insertion order
    std::vector<std::uint64_t> m_bits;      // membership per table row
};

// ── Arrow columns: our buffers, Arrow's layout, exported through the C ABI ──
struct column {
    enum class kind { large_utf8, dictionary };
    kind k = kind::large_utf8;
    std::vector<std::int64_t> offsets{0};   // large_utf8
    std::string data;
    std::vector<std::int32_t> indices;       // dictionary<int32, large_utf8>
    std::shared_ptr<const column> dictionary;

    [[nodiscard]] std::int64_t length() const noexcept {
        return k == kind::large_utf8 ? static_cast<std::int64_t>(offsets.size()) - 1 : static_cast<std::int64_t>(indices.size());
    }
};

struct array_holder {
    std::shared_ptr<const column> col;
    const void* buffers[3] = {nullptr, nullptr, nullptr};
};

// Release callbacks are plain functions: the ABI wants C function pointers.
inline void release_array(ArrowArray* a) noexcept {
    if (!a->release) return;
    if (a->dictionary) {
        release_array(a->dictionary);
        delete a->dictionary;
        a->dictionary = nullptr;
    }
    delete static_cast<array_holder*>(a->private_data);
    a->private_data = nullptr;
    a->release = nullptr;
}
inline void release_schema(ArrowSchema* s) noexcept {
    if (!s->release) return;
    if (s->dictionary) {
        release_schema(s->dictionary);
        delete s->dictionary;
        s->dictionary = nullptr;
    }
    s->release = nullptr;
}

inline ArrowArray* export_array(std::shared_ptr<const column> col) {
    auto* h = new array_holder{col};
    auto* a = new ArrowArray{};
    a->length = col->length();
    a->null_count = 0;
    a->offset = 0;
    a->n_children = 0;
    a->children = nullptr;
    if (col->k == column::kind::large_utf8) {
        h->buffers[1] = col->offsets.data();
        h->buffers[2] = col->data.data();
        a->n_buffers = 3;
        a->dictionary = nullptr;
    } else {
        h->buffers[1] = col->indices.data();
        a->n_buffers = 2;
        a->dictionary = export_array(col->dictionary);
    }
    a->buffers = h->buffers;
    a->private_data = h;
    a->release = &release_array;
    return a;
}
inline ArrowSchema* export_schema(const column& col) {
    auto* s = new ArrowSchema{};
    s->format = col.k == column::kind::large_utf8 ? "U" : "i";
    s->name = "";
    s->metadata = nullptr;
    s->flags = 0;
    s->n_children = 0;
    s->children = nullptr;
    s->dictionary = col.k == column::kind::dictionary ? export_schema(*col.dictionary) : nullptr;
    s->private_data = nullptr;
    s->release = &release_schema;
    return s;
}

// PyCapsule destructors: release if the consumer never did, then free the struct.
inline void destroy_schema_capsule(PyObject* cap) {
    auto* s = static_cast<ArrowSchema*>(PyCapsule_GetPointer(cap, "arrow_schema"));
    if (!s) {
        PyErr_Clear();
        return;
    }
    if (s->release) s->release(s);
    delete s;
}
inline void destroy_array_capsule(PyObject* cap) {
    auto* a = static_cast<ArrowArray*>(PyCapsule_GetPointer(cap, "arrow_array"));
    if (!a) {
        PyErr_Clear();
        return;
    }
    if (a->release) a->release(a);
    delete a;
}

// The Python-facing column: implements the Arrow PyCapsule protocol.
struct arrow_column {
    std::shared_ptr<const column> col;

    [[nodiscard]] py::capsule schema_capsule() const { return py::capsule(export_schema(*col), "arrow_schema", &destroy_schema_capsule); }
    [[nodiscard]] py::capsule array_capsule() const { return py::capsule(export_array(col), "arrow_array", &destroy_array_capsule); }
};

// ── Arrow import: utf8 in its three spellings, from an array or a stream ──
namespace detail {
inline bool valid_at(const ArrowArray& a, std::int64_t i) noexcept {
    if (a.null_count == 0 || !a.buffers[0]) return true;
    const auto* bits = static_cast<const std::uint8_t*>(a.buffers[0]);
    const std::int64_t j = a.offset + i;
    return (bits[j / 8] >> (j % 8)) & 1u;
}

// Calls f(std::string_view) for every non-null string of a utf8 ("u"),
// large_utf8 ("U") or utf8_view ("vu") array.
template <class F>
void for_each_string(std::string_view format, const ArrowArray& a, F&& f) {
    if (format == "u" || format == "U") {
        const auto* data = static_cast<const char*>(a.buffers[2]);
        for (std::int64_t i = 0; i < a.length; ++i) {
            if (!valid_at(a, i)) continue;
            const std::int64_t j = a.offset + i;
            std::int64_t from = 0, to = 0;
            if (format == "u") {
                const auto* off = static_cast<const std::int32_t*>(a.buffers[1]);
                from = off[j];
                to = off[j + 1];
            } else {
                const auto* off = static_cast<const std::int64_t*>(a.buffers[1]);
                from = off[j];
                to = off[j + 1];
            }
            f(std::string_view(data + from, static_cast<std::size_t>(to - from)));
        }
        return;
    }
    if (format == "vu") {   // Utf8View: 16-byte views; long strings point into the variadic buffers
        const auto* views = static_cast<const std::uint8_t*>(a.buffers[1]);
        for (std::int64_t i = 0; i < a.length; ++i) {
            if (!valid_at(a, i)) continue;
            const std::uint8_t* v = views + (a.offset + i) * 16;
            std::int32_t len = 0;
            std::memcpy(&len, v, 4);
            if (len <= 12) {
                f(std::string_view(reinterpret_cast<const char*>(v + 4), static_cast<std::size_t>(len)));
            } else {
                std::int32_t buf = 0, off = 0;
                std::memcpy(&buf, v + 8, 4);
                std::memcpy(&off, v + 12, 4);
                const auto* data = static_cast<const char*>(a.buffers[2 + buf]);
                f(std::string_view(data + off, static_cast<std::size_t>(len)));
            }
        }
        return;
    }
    throw py::type_error("PathSet.from_arrow: expected a utf8 column, got Arrow format '" + std::string(format) + "'");
}

// A one-column struct (a single-column table) is accepted as that column.
inline const ArrowSchema& column_schema(const ArrowSchema& s) {
    if (std::string_view(s.format) == "+s" && s.n_children == 1) return *s.children[0];
    return s;
}
inline const ArrowArray& column_array(const ArrowSchema& s, const ArrowArray& a) {
    if (std::string_view(s.format) == "+s" && s.n_children == 1) return *a.children[0];
    return a;
}
}  // namespace detail

template <class F>
void for_each_arrow_string(py::handle obj, F&& f) {
    if (py::hasattr(obj, "__arrow_c_stream__")) {
        const py::object cap = obj.attr("__arrow_c_stream__")();
        auto* st = static_cast<ArrowArrayStream*>(PyCapsule_GetPointer(cap.ptr(), "arrow_array_stream"));
        if (!st) throw py::error_already_set();
        ArrowSchema schema{};
        if (st->get_schema(st, &schema) != 0) throw std::runtime_error(std::string("Arrow stream: ") + st->get_last_error(st));
        const ArrowSchema& cs = detail::column_schema(schema);
        const std::string format = cs.format;
        while (true) {
            ArrowArray a{};
            if (st->get_next(st, &a) != 0) throw std::runtime_error(std::string("Arrow stream: ") + st->get_last_error(st));
            if (!a.release) break;   // end of stream
            detail::for_each_string(format, detail::column_array(schema, a), f);
            a.release(&a);
        }
        if (schema.release) schema.release(&schema);
        st->release(st);
        return;
    }
    if (py::hasattr(obj, "__arrow_c_array__")) {
        const py::tuple caps = obj.attr("__arrow_c_array__")();
        auto* s = static_cast<ArrowSchema*>(PyCapsule_GetPointer(caps[0].ptr(), "arrow_schema"));
        auto* a = static_cast<ArrowArray*>(PyCapsule_GetPointer(caps[1].ptr(), "arrow_array"));
        if (!s || !a) throw py::error_already_set();
        detail::for_each_string(detail::column_schema(*s).format, detail::column_array(*s, *a), f);
        if (a->release) a->release(a);
        if (s->release) s->release(s);
        return;
    }
    throw py::type_error("PathSet.from_arrow: the object implements neither __arrow_c_stream__ nor __arrow_c_array__");
}

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

inline PathSet pathset_from_arrow(py::handle obj) {
    PathSet ps;
    for_each_arrow_string(obj, [&ps](std::string_view s) { ps.add_text(s); });
    return ps;
}

inline std::shared_ptr<const column> paths_column(const PathSet& ps) {
    auto col = std::make_shared<column>();
    col->offsets.reserve(ps.size() + 1);
    for (const std::uint32_t r : ps.members()) {
        col->data += ps.table()->render<native_strategy>(r);
        col->offsets.push_back(static_cast<std::int64_t>(col->data.size()));
    }
    return col;
}
inline std::shared_ptr<const column> names_column(const PathSet& ps) {
    const segment_table& seg = ps.table()->segments();
    auto dict = std::make_shared<column>();
    dict->offsets = seg.offsets();
    dict->data = seg.text();
    dict->offsets.push_back(dict->offsets.back());   // one trailing "" for rows without a name
    const auto blank = static_cast<std::int32_t>(seg.size());
    auto col = std::make_shared<column>();
    col->k = column::kind::dictionary;
    col->dictionary = dict;
    col->indices.reserve(ps.size());
    for (const std::uint32_t r : ps.members()) {
        col->indices.push_back(ps.table()->has_name(r) ? static_cast<std::int32_t>(ps.table()->name_id(r)) : blank);
    }
    return col;
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
        .def("to_file", [](const fileview& v) { return wrap(Engines_{}, v.to_file()); },
             "The owning file (typed by its engine) with the same value.")
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

    py::class_<arrow_column>(m, "arrow_column",
        "A column of a PathSet in Arrow layout, exported through the Arrow PyCapsule protocol "
        "(__arrow_c_schema__ / __arrow_c_array__) without copying: pass it to pyarrow.array(), "
        "polars.from_arrow() or any other consumer of the C Data Interface.")
        .def("__len__", [](const arrow_column& c) { return c.col->length(); })
        .def("__arrow_c_schema__", [](const arrow_column& c) { return c.schema_capsule(); })
        .def("__arrow_c_array__", [](const arrow_column& c, py::object /*requested_schema*/) {
            return py::make_tuple(c.schema_capsule(), c.array_capsule());
        }, py::arg("requested_schema") = py::none());

    py::class_<PathSet>(m, "PathSet",
        "Many paths as ONE table: every distinct path component is stored once and every path is "
        "a row (parent, name) in a hash-consed trie, so a path costs a few bytes plus its share of "
        "the unique names. Iterating yields fileviews (nothing copied); scan() reuses one view "
        "object per pass. filter_suffix()/filter_name() and |, &, - work on rows and return sets "
        "sharing the table. paths()/names() are Arrow columns for polars/pyarrow; from_arrow() "
        "reads any Arrow-protocol object. Prototype: insertion order, append-only.")
        .def(py::init([](py::object paths) {
                 PathSet ps;
                 if (!paths.is_none()) extend(ps, paths);
                 return ps;
             }), py::arg("paths") = py::none())
        .def("add", [](PathSet& ps, py::handle p) { add_one(ps, p); }, py::arg("path"))
        .def("extend", [](PathSet& ps, py::handle it) { extend(ps, it); }, py::arg("paths"))
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
        .def("paths", [](const PathSet& ps) { return arrow_column{paths_column(ps)}; },
             "The rendered paths as an Arrow large_utf8 column.")
        .def("names", [](const PathSet& ps) { return arrow_column{names_column(ps)}; },
             "The final components as an Arrow dictionary<int32, large_utf8> column (the segment table is the dictionary).")
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
            return d;
        })
        .def_static("from_arrow", [](py::handle obj) { return pathset_from_arrow(obj); }, py::arg("column"),
                    "A PathSet from any object implementing __arrow_c_stream__ or __arrow_c_array__ "
                    "(a pyarrow array, a polars Series, a one-column table) holding utf8 paths.");
}

}  // namespace pygim::pathlike
