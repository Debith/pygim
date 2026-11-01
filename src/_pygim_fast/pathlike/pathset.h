#pragma once
// pathlike/pathset.h — PathSet: many paths as one table.
//
// PathSet owns a shared path_table (path_table.h) and a member list. A member
// is a `pathview` (pathview.h) — a (table, row) pair that copies no text — and
// nothing here is a Python object per path. Value filters and set algebra work on rows; a filtered
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
// Filters (the vocabulary the former pygim.pathset module had, now over rows):
// a row_filter is a predicate on (table, row) with &, | and !, and ps.filter(f)
// is the members it accepts, as a set over the same table.
//
//     row_filter rst = ext_filter(".rst");
//     ps.filter(rst)                        -> the .rst members
//     ps.filter(!rst)                       -> everything else
//     ps.filter(rst | ext_filter(".txt"))   -> .rst or .txt
//
// From Python the same is `ps & ext(".rst")`, a lazy Query that chains more
// filters and evaluates to such a set (adapter/pathset_bindings.h).
//
// CORE layer: pybind-free. Python's PathSet, Filter and Query are adapter/pathset_bindings.h.
//
// CORE layer: pybind-free.

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../mapping/id_set.h"
#include "../utils/chars.h"
#include "core.h"
#include "file_reads.h"
#include "path_store.h"
#include "path_table.h"
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
class row_filter {
public:
    using predicate = std::function<bool(const path_table&, std::uint32_t)>;
    row_filter(predicate holds) noexcept : m_holds(std::move(holds)) {}   // implicit by design: `return {[](...) {...}};`

    [[nodiscard]] bool operator()(const path_table& t, std::uint32_t r) const { return m_holds(t, r); }

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

private:
    predicate m_holds;
};

/// pathlib's final suffix equals `s` (case-sensitive).          ext_filter(".rst")
[[nodiscard]] inline row_filter ext_filter(std::string s) {
    return {[s = std::move(s)](const path_table& t, std::uint32_t r) { return detail::suffix_of(t.name(r)) == s; }};
}
/// pathlib's final suffix is one of `suffixes`, compared exactly — or, case_sensitive false, but for
/// the case of ASCII letters, as similarity.language_of compares one.    ext_filter({".h", ".cpp"}, false)
[[nodiscard]] inline row_filter ext_filter(std::vector<std::string> suffixes, bool case_sensitive) {
    return {[s = std::move(suffixes), case_sensitive](const path_table& t, std::uint32_t r) {
        const std::string_view mine = detail::suffix_of(t.name(r));
        return std::ranges::any_of(s, [&](const std::string& x) { return case_sensitive ? mine == x : chars::same_folded(mine, x); });
    }};
}
/// The name matches a glob (`*`, `?` within the component).       name_filter("read*")
[[nodiscard]] inline row_filter name_filter(std::string glob) {
    return {[g = std::move(glob)](const path_table& t, std::uint32_t r) { return glob_match(g, t.name(r)); }};
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

    /// Room for `n` more paths (path_table::reserve_paths); a sized Python iterable asks for it (extend).
    void reserve(std::size_t n) {
        m_ids.reserve(m_ids.size() + n);
        m_table->reserve_paths(n);
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
        note(v.table().get() == m_table.get() ? v.row() : m_table->insert_from(*v.table(), v.row()));
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
        return has_row(v.table().get() == m_table.get() ? v.row() : m_table->find(*v.table(), v.row()));
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
        return where([&](std::uint32_t r) { return glob_match(glob, m_table->name(r)); });
    }
    /// pathlib's is_absolute() per row.                    ps.filter_absolute() -> [5]
    [[nodiscard]] PathSet filter_absolute() const {
        return where([&](std::uint32_t r) { return m_table->is_absolute<native_strategy>(r); });
    }

    // ── derived sets ──────────────────────────────────────────────────────

    /// Every member with `base` taken off its front (file::relative_to), as a set
    /// over the same table, in member order. A member that is not `base` or beneath
    /// it is an error that names it: a set never drops a member quietly.
    ///
    ///     {"/r/a.py", "/r/pkg/c.py"}.relative_to("/r") -> {"a.py", "pkg/c.py"}
    [[nodiscard]] PathSet relative_to(const file& base) const {
        PathSet out(m_table);
        out.m_ids.reserve(size());
        for (const std::uint32_t r : members()) {
            const auto rest = file(m_table->value(r)).relative_to(base);
            if (!rest) {
                throw std::invalid_argument("'" + m_table->render<native_strategy>(r) +
                                            "' is not in the subpath of '" + base.fspath() + "'");
            }
            out.add_value(rest->value());
        }
        return out;
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

    /// Every member's text as the OS takes it, in member order: what a batch_read owns, so it can
    /// read with the GIL released while another thread writes to this table.
    [[nodiscard]] path_list paths() const {
        path_list out;
        out.reserve(size());
        for (const std::uint32_t r : members()) out.push_back(m_table->render<native_strategy>(r));
        return out;
    }

    /// Every member as std::filesystem takes it, in member order (file::to_fs): what a call that only
    /// stats the members owns, so it can run with the GIL released.
    ///
    ///     ps.fs_paths() -> ["a/b.yaml", "a/c.json", "/d"]
    [[nodiscard]] std::vector<fs::path> fs_paths() const {
        std::vector<fs::path> out;
        out.reserve(size());
        for (const std::uint32_t r : members()) out.push_back(file(m_table->value(r)).to_fs());
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

/// Each file's size in bytes, in order: file::size of each (a link followed). One that is not there,
/// or is not a file, is the OS's error naming it (std::filesystem_error), as path.size() raises it.
///
///     sizes_of(ps.fs_paths()) -> {259, 4096, 12}
[[nodiscard]] inline std::vector<std::uintmax_t> sizes_of(const std::vector<fs::path>& paths) {
    std::vector<std::uintmax_t> out;
    out.reserve(paths.size());
    for (const fs::path& p : paths) out.push_back(fs::file_size(p));
    return out;
}

}  // namespace pygim::pathlike
