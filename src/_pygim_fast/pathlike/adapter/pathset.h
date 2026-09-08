#pragma once
// pathlike/adapter/pathset.h — PathSet: many paths as one table.
//
// PathSet owns a shared path_table (path_table.h) and a member list. Nothing
// here is a Python object per path: iteration hands out `pathview`s — a
// (table, row) pair that copies no text — and scan() reuses ONE view object
// for a whole pass. Value filters and set algebra work on rows; a filtered
// set shares its parent's table, so it is a mapping::id_set (members and a
// bitmap), nothing more. The way out to other tools is to_list()
// (docs/design/pathset_storage.md).
//
// A worked example (POSIX), used in the comments below. Row numbers are the
// table's; see path_table.h for how the rows come about.
//
//     PathSet ps;                          // its own fresh table
//     ps.add_text("a/b.yaml");             // rows: 0 ".", 1 a, 2 a/b.yaml       members [2]
//     ps.add_text("a/c.json");             // row 3 a/c.json                     members [2, 3]
//     ps.add_text("/d");                   // rows 4 "/", 5 /d                   members [2, 3, 5]
//     ps.add_text("a//b.yaml/");           // row 2 again: the same value        members unchanged
//     ps.size() -> 3       ps.view(1) -> pathview{table, 3}
//
//     yaml = ps.filter_suffix(".yaml")     // members [2]      (same table)
//     abs  = ps.filter_absolute()          // members [5]
//     yaml.union_with(abs)                 // [2, 5]   yaml.intersection(abs) -> []   ps.difference(yaml) -> [3, 5]
//     ps.count_intersection(yaml) -> 1     (a popcount: no set built)
//     ps.contains_text("a/c.json") -> true     ps.contains_text("a/q") -> false
//
//     PathSet other;  other.add_text("a/c.json");  other.add_text("e");   // ANOTHER table: its rows 2 and 3
//     ps.intersection(other)               // [3]: other's rows mapped into ps's table, never rendered
//     ps.union_with(other)                 // ps's table copied, other's rows mapped in: [2, 3, 5, 6]
//
// From Python: PathSet(["a/b.yaml", "a/c.json", "/d"]); ps[1].name == "c.json";
// [v.name for v in ps.scan()]; ps & other; ps.count_union(other); ps.to_list().
//
// Filters and queries (the vocabulary the former pygim.pathset module had,
// now over rows): a `Filter` is a predicate on (table, row) with &, | and ~;
// `ps & f` is a lazy `Query` that chains more filters and evaluates to a set
// over the same table.
//
//     rst = ext(".rst")                     Filter: suffix == ".rst"
//     (ps & rst).eval()                     -> the .rst members            (ps.filter(rst) is the same)
//     (ps & ~rst).eval()                    -> everything else
//     (ps & rst | ext(".txt")).eval()       -> .rst or .txt                 (Query | Filter widens)
//     (ps & (rst & ext(".txt"))).eval()     -> nothing: no name has both
//     for v in ps & name("read*"): ...      iterating a Query evaluates it

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>

#include <functional>

#include "../../mapping/id_set.h"
#include "../path_table.h"
#include "adapter.h"
#include "path_store.h"
#include "pathview.h"

namespace pygim::pathlike {

/// A predicate over a row of a table, with boolean algebra. Python's
/// `pathlike.Filter`: made by ext(), name(), absolute(), combined with &, |
/// and ~, applied by `ps & f` (a Query) or `ps.filter(f)`. It reads the
/// table (a name, a suffix, the anchor), never the filesystem, so a filter
/// is pure and a query over a million rows is a table pass.
///
///     row_filter rst = ext_filter(".rst");        rst(table, row_of("x.rst")) -> true
///     (rst & name_filter("read*"))(table, row_of("readme.rst")) -> true
///     (!rst)(table, row_of("x.rst")) -> false
struct row_filter {
    std::function<bool(const path_table&, std::uint32_t)> pred;

    [[nodiscard]] bool operator()(const path_table& t, std::uint32_t r) const { return pred(t, r); }

    /// Both hold.
    friend row_filter operator&(row_filter a, row_filter b) {
        return {[a = std::move(a), b = std::move(b)](const path_table& t, std::uint32_t r) { return a(t, r) && b(t, r); }};
    }
    /// Either holds.
    friend row_filter operator|(row_filter a, row_filter b) {
        return {[a = std::move(a), b = std::move(b)](const path_table& t, std::uint32_t r) { return a(t, r) || b(t, r); }};
    }
    /// Does not hold.
    friend row_filter operator!(row_filter a) {
        return {[a = std::move(a)](const path_table& t, std::uint32_t r) { return !a(t, r); }};
    }
};

/// pathlib's final suffix equals `s` (case-sensitive).          ext_filter(".rst")
[[nodiscard]] inline row_filter ext_filter(std::string s) {
    return {[s = std::move(s)](const path_table& t, std::uint32_t r) { return detail::suffix_of(t.name(r)) == s; }};
}
/// The name matches a glob (`*`, `?` within the component).       name_filter("read*")
[[nodiscard]] inline row_filter name_filter(std::string glob) {
    return {[g = std::move(glob)](const path_table& t, std::uint32_t r) { return detail::glob_match(g, t.name(r)); }};
}
/// pathlib's is_absolute().                                         absolute_filter()
[[nodiscard]] inline row_filter absolute_filter() {
    return {[](const path_table& t, std::uint32_t r) { return t.is_absolute<native_strategy>(r); }};
}

class PathSet {
public:
    /// An empty set over a fresh table of its own.
    PathSet() : m_table(std::make_shared<path_table>()) {}
    /// An empty set over an existing table — a filter's result, a set over a
    /// PathStore's table (`PathSet(paths, store=s)` in Python).
    explicit PathSet(table_ptr t) : m_table(std::move(t)) {}

    /// How many members.                                   ps.size() -> 3
    [[nodiscard]] std::size_t size() const noexcept { return m_ids.size(); }
    /// The table the rows index.
    [[nodiscard]] const table_ptr& table() const noexcept { return m_table; }
    /// The member rows in insertion order.                ps.members() -> [2, 3, 5]
    [[nodiscard]] const std::vector<std::uint32_t>& members() const noexcept { return m_ids.members(); }
    /// The i-th member as a view (no bounds check).       ps.view(1) -> pathview{table, 3}
    [[nodiscard]] pathview view(std::size_t i) const { return {m_table, m_ids[i], nullptr}; }

    /// Room for `n` more paths: sizes the table's hash tables once instead of
    /// letting them double their way up (a fresh table's rows run ~2-3x the
    /// path count, its distinct segments about 1x). A sized Python iterable
    /// triggers this automatically (extend); measured 14% off a 1M build.
    void reserve(std::size_t n) {
        m_ids.reserve(m_ids.size() + n);
        m_table->reserve(m_table->size() + 3 * n, m_table->segments().size() + n);
    }

    // ── adding (find-or-add in the table, then membership) ────────────────

    /// Adds native path text: tokenised into the table, then its row noted.
    /// A spelling of a path already present adds nothing.
    ///
    ///     ps.add_text("a/b.yaml")      // row 2 noted
    ///     ps.add_text("a//b.yaml/")    // row 2 again: no change
    void add_text(std::string_view text) { note(m_table->insert<native_strategy>(text)); }
    /// Adds a parsed value (a Python `file`): fed into the table, row noted.
    void add_value(const uri& u) { note(m_table->insert<native_strategy>(u)); }
    /// Adds a view: its row directly when the view is over this table,
    /// otherwise its chain copied in (path_table::insert_from).
    void add_view(const pathview& v) {
        note(v.table.get() == m_table.get() ? v.row : m_table->insert_from(*v.table, v.row));
    }

    // ── membership ────────────────────────────────────────────────────────

    /// Whether row `r` of this table is a member: one bit test (`none` is never
    /// a member, so a failed lookup can be passed straight in).
    [[nodiscard]] bool has_row(std::uint32_t r) const noexcept { return r != path_table::none && m_ids.has(r); }
    /// `"a/c.json" in ps`: the text looked up (never added), then the bit.
    ///
    ///     ps.contains_text("a/c.json") -> true     ps.contains_text("a") -> false   (row 1 exists but is not a member)
    [[nodiscard]] bool contains_text(std::string_view t) const { return has_row(m_table->find<native_strategy>(t)); }
    /// `file in ps`: the value looked up, then the bit.
    [[nodiscard]] bool contains_value(const uri& u) const { return has_row(m_table->find<native_strategy>(u)); }
    /// `pathview in ps`: the row directly over the same table, else the view's
    /// chain looked up here.
    [[nodiscard]] bool contains_view(const pathview& v) const {
        return has_row(v.table.get() == m_table.get() ? v.row : m_table->find(*v.table, v.row));
    }

    // ── value filters: a new set over the SAME table ──────────────────────

    /// The members for which pred(row) holds, in order, as a set over the
    /// same table (mapping::id_set::where). Every filter below is one of
    /// these with a predicate that reads the table; ~10 ns per member.
    template <class Pred>
    [[nodiscard]] PathSet where(Pred pred) const {
        PathSet out(m_table);
        out.m_ids = m_ids.where(pred);
        return out;
    }
    /// pathlib's rule for the final suffix, compared case-sensitively.
    ///
    ///     ps.filter_suffix(".yaml")   -> [2]        ps.filter_suffix("") -> [5]   ("/d" has no suffix)
    [[nodiscard]] PathSet filter_suffix(std::string_view s) const {
        return where([&](std::uint32_t r) { return detail::suffix_of(m_table->name(r)) == s; });
    }
    /// The name matched against a glob (`*` and `?` within the component).
    ///
    ///     ps.filter_name("*.j*")      -> [3]
    [[nodiscard]] PathSet filter_name(std::string_view glob) const {
        return where([&](std::uint32_t r) { return detail::glob_match(glob, m_table->name(r)); });
    }
    /// pathlib's is_absolute() per row.                    ps.filter_absolute() -> [5]
    [[nodiscard]] PathSet filter_absolute() const {
        return where([&](std::uint32_t r) { return m_table->is_absolute<native_strategy>(r); });
    }

    // ── set algebra (bitmaps when the tables are shared, row mapping otherwise) ──

    /// this ∪ o. Shared table: a bitmap union, "mine, then theirs", ~2 ns per
    /// element. Two tables: the larger table is COPIED (a memcpy of flat
    /// arrays) and only the other set's rows are mapped into it once
    /// (path_table::row_map), so the result owns a table neither operand
    /// shares; ~300 ns per mapped element.
    ///
    ///     yaml.union_with(abs)     -> [2, 5]       over ps's table
    ///     ps.union_with(other)     -> [2, 3, 5, 6] over a copy of ps's table, with other's "e" as row 6
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
    /// this ∩ o, in this set's order. Shared table: a bitmap pass. Two tables:
    /// my rows looked up in `o`'s table (lookup only, nothing added anywhere)
    /// and kept when `o` has them; the result is over MY table.
    ///
    ///     yaml.intersection(abs)   -> []         ps.intersection(other) -> [3]
    [[nodiscard]] PathSet intersection(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) {
            PathSet out(m_table);
            out.m_ids = m_ids.intersected(o.m_ids);
            return out;
        }
        path_table::row_map in_o(*o.m_table, *m_table);
        return where([&](std::uint32_t r) { return o.has_row(in_o(r)); });
    }
    /// this ∖ o, in this set's order; the two-table case as for intersection.
    ///
    ///     ps.difference(yaml)      -> [3, 5]     ps.difference(other) -> [2, 5]
    [[nodiscard]] PathSet difference(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) {
            PathSet out(m_table);
            out.m_ids = m_ids.subtracted(o.m_ids);
            return out;
        }
        path_table::row_map in_o(*o.m_table, *m_table);
        return where([&](std::uint32_t r) { return !o.has_row(in_o(r)); });
    }

    // ── counting without building ─────────────────────────────────────────
    // |this ∪ o|, |this ∩ o|, |this ∖ o| without building the set: a popcount
    // over the bitmaps when the tables are shared (id_set::count_united & co,
    // 64 ids per step: 25 us against 570-2170 us for the built set at 1M
    // paths); otherwise the other set's rows are mapped once — no table copy,
    // no result set.

    /// |this ∪ o|.                                         ps.count_union(other) -> 4
    [[nodiscard]] std::size_t count_union(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) return m_ids.count_united(o.m_ids);
        return size() + o.size() - count_intersection(o);
    }
    /// |this ∩ o|.                                         ps.count_intersection(yaml) -> 1
    [[nodiscard]] std::size_t count_intersection(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) return m_ids.count_intersected(o.m_ids);
        path_table::row_map in_o(*o.m_table, *m_table);
        std::size_t n = 0;
        for (const std::uint32_t r : m_ids.members()) n += o.has_row(in_o(r)) ? 1u : 0u;
        return n;
    }
    /// |this ∖ o|.                                         ps.count_difference(yaml) -> 2
    [[nodiscard]] std::size_t count_difference(const PathSet& o) const {
        if (o.m_table.get() == m_table.get()) return m_ids.count_subtracted(o.m_ids);
        return size() - count_intersection(o);
    }

    // ── the set-like surface (what pygim.pathset had) ─────────────────────

    /// The members satisfying a row_filter, as a set over the same table:
    /// what `(ps & f).eval()` and `ps.filter(f)` return.
    ///
    ///     ps.filter(ext_filter(".yaml"))   -> [2]
    [[nodiscard]] PathSet filter(const row_filter& f) const {
        return where([&](std::uint32_t r) { return f(*m_table, r); });
    }
    /// Same members? Same table: same size and every one of mine is in `o`.
    /// Two tables: my rows looked up in `o`'s table. Insertion order does not
    /// matter — sets compare as sets.                     ps.equals(ps.clone()) -> true
    [[nodiscard]] bool equals(const PathSet& o) const {
        if (size() != o.size()) return false;
        if (o.m_table.get() == m_table.get()) {
            for (const std::uint32_t r : m_ids.members()) {
                if (!o.has_row(r)) return false;
            }
            return true;
        }
        path_table::row_map in_o(*o.m_table, *m_table);
        for (const std::uint32_t r : m_ids.members()) {
            if (!o.has_row(in_o(r))) return false;
        }
        return true;
    }
    /// A distinct set object with the same members over the same table
    /// (sets are never edited in place, so this is cheap and only needed
    /// for identity: `ps.clone() is not ps`).
    [[nodiscard]] PathSet clone() const {
        PathSet out(m_table);
        out.m_ids = m_ids;
        return out;
    }
    /// The set holding the current working directory (pygim.pathset's
    /// PathSet.cwd()), over a fresh table.
    [[nodiscard]] static PathSet cwd() {
        PathSet out;
        out.add_value(file().absolute().value());
        return out;
    }

    /// The bytes the members and bitmap hold (PathSet.stats()["member_bytes"]).
    [[nodiscard]] std::size_t member_bytes() const noexcept { return m_ids.bytes(); }

private:
    /// Membership for a row of this table (a repeat is a no-op).
    void note(std::uint32_t r) { m_ids.note(r); }

    table_ptr m_table;
    mapping::id_set m_ids;   // the members (insertion order) and their bitmap
};

// ── Python glue ─────────────────────────────────────────────────────────────

/// Adds one Python item: str/bytes as text, a pathview by row, a file by
/// value, anything else through os.fspath (a TypeError when it cannot).
inline void add_one(PathSet& ps, py::handle item) {
    if (PyUnicode_Check(item.ptr()) || PyBytes_Check(item.ptr())) {
        const text_arg t = text_view_of_arg(item);
        ps.add_text(t.view);
        return;
    }
    if (py::isinstance<pathview>(item)) {
        ps.add_view(py::cast<const pathview&>(item));
        return;
    }
    const text_arg t = text_view_of_arg(item);
    ps.add_text(t.view);
}
/// Whether a Python object is ONE path rather than an iterable of them: str,
/// bytes, anything with __fspath__ (pathlib.Path), a file or a pathview. A
/// str is a path, never a sequence of characters — `PathSet("a/b")` is one
/// member.
[[nodiscard]] inline bool is_single_path(py::handle item) {
    return PyUnicode_Check(item.ptr()) || PyBytes_Check(item.ptr()) || py::isinstance<pathview>(item) ||
           py::hasattr(item, "__fspath__");
}
/// Adds every item of an iterable; a sized one (list, tuple, set, dict, ...)
/// reserves the table first.
inline void extend(PathSet& ps, py::handle iterable) {
    if (PySequence_Check(iterable.ptr()) || PyAnySet_Check(iterable.ptr()) || PyDict_Check(iterable.ptr())) {
        const py::ssize_t n = PyObject_Length(iterable.ptr());
        if (n < 0) PyErr_Clear();
        else ps.reserve(static_cast<std::size_t>(n));
    }
    for (const py::handle item : py::iter(iterable)) add_one(ps, item);
}
/// One path or an iterable of paths, added.
inline void add_any(PathSet& ps, py::handle items) {
    if (is_single_path(items)) add_one(ps, items);
    else extend(ps, items);
}
/// The right-hand side of `ps + x` / `ps - x` when `x` is not a PathSet: a
/// set over ps's table holding the given path(s). For a union the paths are
/// added to the table (they become members); for a difference they are only
/// looked up (a path the table does not know cannot be a member anyway).
[[nodiscard]] inline PathSet operand_over(const PathSet& base, py::handle items, bool insert) {
    PathSet out(base.table());
    if (insert) {
        add_any(out, items);
        return out;
    }
    auto note_found = [&](py::handle item) {
        if (PyUnicode_Check(item.ptr()) || PyBytes_Check(item.ptr()) || py::hasattr(item, "__fspath__")) {
            const text_arg t = text_view_of_arg(item);
            const std::uint32_t r = base.table()->find<native_strategy>(t.view);
            if (r != path_table::none) out.add_text(t.view);
        } else if (py::isinstance<pathview>(item)) {
            const auto& v = py::cast<const pathview&>(item);
            const std::uint32_t r = v.table.get() == base.table().get() ? v.row : base.table()->find(*v.table, v.row);
            if (r != path_table::none) out.add_view(v);
        } else {
            const text_arg t = text_view_of_arg(item);
            if (base.table()->find<native_strategy>(t.view) != path_table::none) out.add_text(t.view);
        }
    };
    if (is_single_path(items)) note_found(items);
    else for (const py::handle item : py::iter(items)) note_found(item);
    return out;
}
/// `item in ps` for the same four kinds of item as add_one.
inline bool contains(const PathSet& ps, py::handle item) {
    if (PyUnicode_Check(item.ptr()) || PyBytes_Check(item.ptr())) {
        const text_arg t = text_view_of_arg(item);
        return ps.contains_text(t.view);
    }
    if (py::isinstance<pathview>(item)) return ps.contains_view(py::cast<const pathview&>(item));
    const text_arg t = text_view_of_arg(item);
    return ps.contains_text(t.view);
}

/// The iterator behind `for v in ps` (a fresh pathview object per element,
/// ~200-300 ns: the pybind11 instance is the cost) and `ps.scan()` (ONE
/// pathview object whose row advances, ~100 ns — do not keep it across
/// iterations). `owner` keeps the set, and so the table, alive.
struct pathset_iter {
    py::object owner;      // keeps the set (and so the table) alive
    const PathSet* set;
    bool reuse;
    std::size_t i = 0;
    py::object current;    // the one view object scan() hands out

    pathset_iter(py::object o, const PathSet* s, bool r) : owner(std::move(o)), set(s), reuse(r) {}
};

/// A lazy filter over a set: `ps & f`. Holds the set's Python object (so the
/// set and its table outlive the query however it is chained) and the
/// combined predicate; `& g` / `| g` return a new query with the predicate
/// narrowed / widened; eval() runs it once, as a set over the same table.
///
///     path_query q{owner, &ps, ext_filter(".rst")};
///     q.eval()                                -> the .rst members
///     (q | name_filter("*.txt")).eval()       -> .rst or *.txt
struct path_query {
    py::object owner;      // keeps the source set alive
    const PathSet* set;
    row_filter f;

    [[nodiscard]] PathSet eval() const { return set->filter(f); }
    [[nodiscard]] path_query narrowed(const row_filter& g) const { return {owner, set, f & g}; }
    [[nodiscard]] path_query widened(const row_filter& g) const { return {owner, set, f | g}; }
};

/// Binds pathview, the iterator, Filter, Query and PathSet into the pathlike module. Every
/// pathview property reads the table by row; `to_file()` hands the row to the
/// current PathStore, so a view of a set over the store's table becomes the
/// store's object without re-interning (a slot read), and any other view is
/// interned by value.
template <class... Es>
void bind_pathset(engine_list<Es...> es, py::module_& m, py::class_<pathview>& path_cls) {
    using Engines_ = engine_list<Es...>;
    (void)Engines_{};

    // `p.pathset(pattern)`: the glob results as a set over p's own table.
    path_cls.def("pathset", [](const pathview& v, std::string_view pattern) {
                 PathSet ps(v.table);
                 for (const file& m : v.value().glob(pattern)) ps.add_value(m.value());
                 return ps;
             }, py::arg("pattern") = "*",
             "The glob results as a PathSet over this path's table, for set algebra and Filter queries.");

    py::class_<pathset_iter>(m, "_PathSetIterator")
        .def("__iter__", [](py::object self) { return self; })
        .def("__next__", [es](pathset_iter& it) -> py::object {
            if (it.i >= it.set->size()) throw py::stop_iteration();
            pathview v = it.set->view(it.i++);
            if (!it.reuse) return wrap(es, std::move(v));
            if (!it.current) {
                it.current = py::cast(std::move(v));
            } else {
                py::cast<pathview&>(it.current).row = v.row;
            }
            return it.current;
        });

    py::class_<row_filter>(m, "Filter",
        "A predicate over the paths of a PathSet — made by ext(), name(), absolute() — with & (both), "
        "| (either) and ~ (not). Apply it with `ps & f` (a lazy Query) or ps.filter(f). Filters read the "
        "table, never the filesystem.")
        .def("__and__", [](const row_filter& a, const row_filter& b) { return a & b; }, py::is_operator())
        .def("__or__", [](const row_filter& a, const row_filter& b) { return a | b; }, py::is_operator())
        .def("__invert__", [](const row_filter& a) { return !a; }, py::is_operator());

    py::class_<path_query>(m, "Query",
        "A lazy filter over a PathSet: `ps & f`. `& g` narrows, `| g` widens; eval() returns the matching "
        "members as a PathSet over the same table; iterating or len() evaluates it. The query keeps its "
        "source set alive.")
        .def("__and__", [](const path_query& q, const row_filter& g) { return q.narrowed(g); }, py::is_operator())
        .def("__or__", [](const path_query& q, const row_filter& g) { return q.widened(g); }, py::is_operator())
        .def("eval", &path_query::eval, "The matching members as a PathSet over the same table.")
        .def("__len__", [](const path_query& q) { return q.eval().size(); })
        .def("__iter__", [](const path_query& q) { return py::cast(q.eval()).attr("__iter__")(); });

    m.def("ext", [](std::string s) { return ext_filter(std::move(s)); }, py::arg("suffix"),
          "Filter: pathlib's final suffix equals `suffix` ('.yaml'; case-sensitive).");
    m.def("name", [](std::string g) { return name_filter(std::move(g)); }, py::arg("glob"),
          "Filter: the final component matches the glob (`*` and `?`).");
    m.def("absolute", []() { return absolute_filter(); }, "Filter: pathlib's is_absolute().");
    m.def("match_pattern", [](std::string_view pattern, std::string_view text) { return detail::glob_match(pattern, text); },
          py::arg("pattern"), py::arg("string"), "Whether `string` matches the glob `pattern` (`*` and `?`).");

    py::class_<PathSet>(m, "PathSet",
        "Many paths as ONE table: every distinct path component is stored once and every path is "
        "a row (parent, name) in a hash-consed trie, so a path costs a few bytes plus its share of "
        "the unique names. Iterating yields pathviews (nothing copied); scan() reuses one view "
        "object per pass. filter_suffix()/filter_name() and |, &, - work on rows and return sets "
        "sharing the table; `ps & Filter` is a lazy Query; + and - take a PathSet or path(s); == compares "
        "as sets; to_list() renders the members. PathSet(paths, store=s) shares the store's table, so its "
        "members and path(text, store=s) objects are rows of one table. Sets are built, never edited: `ps -= x` "
        "rebinds the name to a new set. Insertion order, append-only.")
        .def(py::init([](py::object paths, py::object store) {
                 PathSet ps = store.is_none() ? PathSet() : PathSet(as_store(store).table());
                 if (!paths.is_none()) add_any(ps, paths);
                 return ps;
             }), py::arg("paths") = py::none(), py::kw_only(), py::arg("store") = py::none(),
             "PathSet(paths=None, *, store=None): `paths` is one path (str, bytes, os.PathLike, file, pathview) "
             "or an iterable of them.")
        .def_static("cwd", &PathSet::cwd, "The set holding the current working directory.")
        .def("add", [](PathSet& ps, py::handle p) { add_one(ps, p); }, py::arg("path"))
        .def("extend", [](PathSet& ps, py::handle it) { extend(ps, it); }, py::arg("paths"))
        .def("reserve", &PathSet::reserve, py::arg("n"),
             "Size the table for `n` more paths up front (what a sized iterable already does).")
        .def("__len__", &PathSet::size)
        .def("__bool__", [](const PathSet& ps) { return ps.size() != 0; })
        .def("__contains__", [](const PathSet& ps, py::handle p) { return contains(ps, p); })
        .def("__eq__", [](const PathSet& a, const PathSet& b) { return a.equals(b); }, py::is_operator())
        .def("__ne__", [](const PathSet& a, const PathSet& b) { return !a.equals(b); }, py::is_operator())
        .def("clone", &PathSet::clone, "A distinct set object with the same members over the same table.")
        .def("__iter__", [](py::object self) {
            return pathset_iter(self, &py::cast<const PathSet&>(self), false);
        })
        .def("scan", [](py::object self) {
            return pathset_iter(self, &py::cast<const PathSet&>(self), true);
        }, "Iterate reusing ONE pathview object (its row advances): no allocation per element. "
           "Do not keep the yielded object across iterations.")
        .def("__getitem__", [es](const PathSet& ps, py::ssize_t i) {
            const auto n = static_cast<py::ssize_t>(ps.size());
            if (i < 0) i += n;
            if (i < 0 || i >= n) throw py::index_error("PathSet index " + std::to_string(i) + " out of range for " + std::to_string(n) + " members");
            return wrap(es, ps.view(static_cast<std::size_t>(i)));
        })
        .def("filter_suffix", [](const PathSet& ps, std::string_view s) { return ps.filter_suffix(s); }, py::arg("suffix"),
             "The members whose final suffix equals `suffix` (pathlib's rule; case-sensitive).")
        .def("filter_name", [](const PathSet& ps, std::string_view g) { return ps.filter_name(g); }, py::arg("glob"),
             "The members whose name matches the glob (`*` and `?`).")
        .def("filter_absolute", &PathSet::filter_absolute)
        .def("filter", &PathSet::filter, py::arg("filter"), "The members a Filter accepts, as a set over the same table.")
        // `ps & Filter` / `ps | Filter` start a lazy Query; with a PathSet they are the set algebra.
        .def("__and__", [](py::object self, const row_filter& f) {
                 return path_query{self, &py::cast<const PathSet&>(self), f};
             }, py::is_operator())
        .def("__or__", [](py::object self, const row_filter& f) {
                 return path_query{self, &py::cast<const PathSet&>(self), f};
             }, py::is_operator())
        .def("__or__", &PathSet::union_with, py::is_operator())
        .def("__and__", &PathSet::intersection, py::is_operator())
        .def("__sub__", &PathSet::difference, py::is_operator())
        // + and - also take one path or an iterable of paths (pygim.pathset's spelling).
        .def("__add__", &PathSet::union_with, py::is_operator())
        .def("__add__", [](const PathSet& ps, py::handle items) { return ps.union_with(operand_over(ps, items, true)); }, py::is_operator())
        .def("__sub__", [](const PathSet& ps, py::handle items) { return ps.difference(operand_over(ps, items, false)); }, py::is_operator())
        .def("read_all_files", [](const PathSet& ps) {
                 py::list out;
                 for (const std::uint32_t r : ps.members()) {
                     const file f(ps.table()->value(r));
                     if (!f.is_file()) continue;
                     const std::string bytes = f.read_bytes();
                     PyObject* text = PyUnicode_DecodeUTF8(bytes.data(), static_cast<py::ssize_t>(bytes.size()), nullptr);
                     if (!text) {
                         PyErr_Clear();
                         throw py::value_error("read_all_files: " + f.fspath() + " is not valid UTF-8");
                     }
                     out.append(py::reinterpret_steal<py::object>(text));
                 }
                 return out;
             }, "The text of every member that is a regular file (UTF-8), one str per file, in member order; "
                "directories and missing paths are skipped.")
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
