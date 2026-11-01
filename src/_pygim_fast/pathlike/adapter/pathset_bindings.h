#pragma once
// pathlike/adapter/pathset_bindings.h — PathSet, Filter and Query for Python: the core PathSet
// (../pathset.h) bound, with what only Python needs — taking paths from any Python object, the
// iterators, the lazy Query that keeps its source set alive — and read_all_files / read_each over
// the core batch_read (../batch_read.h).

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <pybind11/pybind11.h>

#include "../../utils/adapter_utils.h"
#include "../../utils/interrupted.h"
#include "../batch_read.h"
#include "../path_table.h"
#include "../pathset.h"
#include "adapter.h"
#include "path_bindings.h"
#include "path_store_bindings.h"

namespace pygim::pathlike {

// ── Python glue ─────────────────────────────────────────────────────────────

/// Adds one Python item: str/bytes as text, a pathview by row, a file by
/// value, anything else through os.fspath (a TypeError when it cannot).
inline void add_one(PathSet& ps, py::handle item) {
    if (PyUnicode_Check(item.ptr()) || PyBytes_Check(item.ptr())) {
        const text_arg t = text_view_of_arg(item);
        ps.add_text(t.view());
        return;
    }
    if (py::isinstance<pathview>(item)) {
        ps.add_view(py::cast<const pathview&>(item));
        return;
    }
    const text_arg t = text_view_of_arg(item);
    ps.add_text(t.view());
}
/// Whether a Python object is ONE path rather than an iterable of them: str,
/// bytes, anything with __fspath__ (pathlib.Path), a file or a pathview. A
/// str is a path, never a sequence of characters — `PathSet("a/b")` is one
/// member.
[[nodiscard]] inline bool is_single_path(py::handle item) {
    return PyUnicode_Check(item.ptr()) || PyBytes_Check(item.ptr()) || py::isinstance<pathview>(item) ||
           has_attribute(item, interned<"__fspath__">());
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
        if (PyUnicode_Check(item.ptr()) || PyBytes_Check(item.ptr()) || has_attribute(item, interned<"__fspath__">())) {
            const text_arg t = text_view_of_arg(item);
            const std::uint32_t r = base.table()->find<native_strategy>(t.view());
            if (r != path_table::none) out.add_text(t.view());
        } else if (py::isinstance<pathview>(item)) {
            const auto& v = py::cast<const pathview&>(item);
            const std::uint32_t r = v.table().get() == base.table().get() ? v.row() : base.table()->find(*v.table(), v.row());
            if (r != path_table::none) out.add_view(v);
        } else {
            const text_arg t = text_view_of_arg(item);
            if (base.table()->find<native_strategy>(t.view()) != path_table::none) out.add_text(t.view());
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
        return ps.contains_text(t.view());
    }
    if (py::isinstance<pathview>(item)) return ps.contains_view(py::cast<const pathview&>(item));
    const text_arg t = text_view_of_arg(item);
    return ps.contains_text(t.view());
}

/// pathset()'s kind: None, 'file' or 'dir'; anything else a ValueError naming what is taken.
[[nodiscard]] inline glob_kind glob_kind_of(std::optional<std::string_view> kind) {
    if (!kind) return glob_kind::any;
    if (*kind == "file") return glob_kind::file;
    if (*kind == "dir") return glob_kind::directory;
    throw py::value_error("kind must be 'file', 'dir' or None, not '" + std::string(*kind) + "'");
}

/// pathset()'s skip: names from any iterable of str — a list, a tuple, a set. One str alone is a
/// TypeError: iterated, it would skip every folder named by one of its letters.
[[nodiscard]] inline std::vector<std::string> skip_names(const py::object& skip) {
    if (PyUnicode_Check(skip.ptr()) || PyBytes_Check(skip.ptr())) {
        throw py::type_error("skip takes folder names, not one string: skip=[" + py::repr(skip).cast<std::string>() + "]");
    }
    std::vector<std::string> out;
    for (const py::handle name : py::iter(skip)) {
        if (!PyUnicode_Check(name.ptr())) throw py::type_error("skip takes folder names as str, not " + std::string(Py_TYPE(name.ptr())->tp_name));
        out.push_back(name.cast<std::string>());
    }
    return out;
}

/// The UTF-8 text of `bytes` as a str, or a ValueError naming the file they came from: name(),
/// asked only then, decoded as os.fsdecode does, since a POSIX name need not be UTF-8 either.
template <std::invocable Name>
[[nodiscard]] py::str utf8_text(std::string_view bytes, const Name& name_of, std::string_view who) {
    PyObject* text = PyUnicode_DecodeUTF8(bytes.data(), static_cast<py::ssize_t>(bytes.size()), nullptr);
    if (!text) {
        PyErr_Clear();
        const py::str name = str_from_text(name_of());
        PyErr_Format(PyExc_ValueError, "%s: %U is not valid UTF-8", std::string(who).c_str(), name.ptr());
        throw py::error_already_set();
    }
    return py::reinterpret_steal<py::str>(text);
}

/// read_all_files' answer, in member order: each file's bytes as a str (a ValueError naming it when
/// they are not UTF-8); a member that is no file left out, a read the OS refused raised (file_bytes).
[[nodiscard]] inline py::list texts_of_files(std::vector<read_result>& results, const path_list& paths) {
    py::list texts;
    for (std::size_t at = 0; at < results.size(); ++at) {
        const os_path path = paths.at(at);
        if (const std::optional<arena_string> bytes = file_bytes(std::move(results[at]), path)) {
            texts.append(utf8_text(*bytes, [path] { return path.text(); }, "read_all_files"));
        }
    }
    return texts;
}

/// One member's bytes, owned here and shown to Python as a read-only buffer — memoryview(content)
/// copies nothing — with the member it came from. Python's pathlike.FileContent.
class file_content {
public:
    file_content(arena_string bytes, table_ptr table, std::uint32_t row) noexcept
        : m_bytes(std::move(bytes)), m_table(std::move(table)), m_row(row) {}

    [[nodiscard]] std::string_view bytes() const noexcept { return m_bytes; }
    /// The member, as a view of its row.
    [[nodiscard]] pathview member() const { return {m_table, m_row, nullptr}; }
    /// The bytes as Python's buffer protocol sees them: one dimension of unsigned bytes, read-only.
    [[nodiscard]] py::buffer_info buffer() const {
        return py::buffer_info(const_cast<char*>(m_bytes.data()), 1, py::format_descriptor<unsigned char>::format(), 1,
                               {static_cast<py::ssize_t>(m_bytes.size())}, {py::ssize_t{1}}, /*readonly=*/true);
    }

private:
    arena_string m_bytes;   // as read: on the heap
    table_ptr m_table;      // keeps the member's row readable
    std::uint32_t m_row;
};

/// PathSet.read_each(): a batch_read over the members and the rows they came from. Each next()
/// hands over the next member that is a file, as its file_content. The GIL is let go only while a
/// result is not ready: when the readers are ahead, taking one keeps it, so another Python thread
/// cannot take the GIL between files.
class contents_reading {
public:
    contents_reading(const PathSet& ps, chosen_width width, window_limits limits)
        : m_table(ps.table()), m_rows(ps.members()), m_job(std::make_unique<batch_read>(ps.paths(), std::move(width), limits)) {}

    /// The next member's content; nothing once every member is read — or after one raised.
    /// ValueError while another thread's next() is still running, as a generator re-entered.
    [[nodiscard]] std::optional<file_content> next() {
        if (m_busy) throw py::value_error("read_each iterator already executing");   // checked and set holding the GIL
        class one_reader {
        public:
            explicit one_reader(bool& busy) noexcept : m_busy(busy) { m_busy = true; }
            ~one_reader() { m_busy = false; }

        private:
            bool& m_busy;
        } reading(m_busy);
        try {
            while (m_job && !m_job->exhausted()) {
                const std::size_t at = m_at++;
                read_result got = take();
                if (std::optional<arena_string> bytes = file_bytes(std::move(got), m_job->path_at(at))) {
                    return file_content(std::move(*bytes), m_table, m_rows[at]);
                }
            }
        } catch (...) {
            stop_reading();
            throw;
        }
        stop_reading();   // its threads gone as soon as the reading is done
        return std::nullopt;
    }

    /// Dropped before its end: the readers stopped as next() stops them.
    ~contents_reading() {
        if (!interpreter_finalizing()) stop_reading();
    }
    contents_reading(contents_reading&&) noexcept = default;   // the moved-from holds no job: its destructor stops nothing
    contents_reading& operator=(contents_reading&&) = delete;

private:
    // The readers stopped and joined with the GIL let go: a read still running on a slow mount then
    // holds up no other Python thread while it finishes.
    void stop_reading() noexcept {
        if (!m_job) return;
        py::gil_scoped_release nogil;
        m_job.reset();
    }
    [[nodiscard]] static bool interpreter_finalizing() noexcept {
#if PY_VERSION_HEX >= 0x030D0000
        return Py_IsFinalizing() != 0;
#else
        return _Py_IsFinalizing() != 0;
#endif
    }

    [[nodiscard]] read_result take() {
        if (m_job->ready()) return m_job->next();
        py::gil_scoped_release nogil;   // to wait for the readers, or to read it here
        return m_job->next();
    }

    table_ptr m_table;
    std::vector<std::uint32_t> m_rows;
    std::unique_ptr<batch_read> m_job;   // m_job, m_at and the job's consumer side: one next() at a time (m_busy)
    std::size_t m_at = 0;
    bool m_busy = false;                 // a next() is running, its GIL maybe let go
};

/// The iterator behind `for v in ps` (a fresh pathview object per element,
/// ~200-300 ns: the pybind11 instance is the cost) and `ps.scan()` (ONE
/// pathview object whose row advances, ~100 ns — do not keep it across
/// iterations). It keeps the set, and so the table, alive.
class pathset_iter {
public:
    /// Over the set `owner` is; `reuse` for scan()'s one object.
    pathset_iter(py::object owner, bool reuse) : m_owner(std::move(owner)), m_set(&py::cast<const PathSet&>(m_owner)), m_reuse(reuse) {}

    /// The next member as a Python object — wrap(view) a fresh one, or scan()'s one object moved
    /// onto it; nothing at the end.
    template <class Wrap>
    [[nodiscard]] std::optional<py::object> next(const Wrap& wrap) {
        if (m_at >= m_set->size()) return std::nullopt;
        const std::size_t at = m_at++;
        pathview v = m_set->view(at);
        if (!m_reuse) return wrap(std::move(v));
        if (!m_current) {
            m_current = py::cast(std::move(v));
        } else {
            py::cast<pathview&>(m_current).move_to(v.row());
        }
        return m_current;
    }

private:
    py::object m_owner;      // the set (and so the table), kept alive
    const PathSet* m_set;    // m_owner's
    bool m_reuse;
    std::size_t m_at = 0;
    py::object m_current;    // the one view object scan() hands out
};

/// A lazy filter over a set: `ps & f`. Holds the set's Python object (so the
/// set and its table outlive the query however it is chained) and the
/// combined predicate; `& g` / `| g` return a new query with the predicate
/// narrowed / widened; eval() runs it once, as a set over the same table.
///
///     path_query q(owner, ext_filter(".rst"));   // owner: the PathSet object
///     q.eval()                                -> the .rst members
///     (q | name_filter("*.txt")).eval()       -> .rst or *.txt
class path_query {
public:
    /// The set `owner` is, filtered by `f`.
    path_query(py::object owner, row_filter f) : m_owner(std::move(owner)), m_set(&py::cast<const PathSet&>(m_owner)), m_filter(std::move(f)) {}

    [[nodiscard]] PathSet eval() const { return m_set->filter(m_filter); }
    [[nodiscard]] path_query narrowed(const row_filter& g) const { return {m_owner, m_filter & g}; }
    [[nodiscard]] path_query widened(const row_filter& g) const { return {m_owner, m_filter | g}; }

private:
    py::object m_owner;      // the source set, kept alive
    const PathSet* m_set;    // m_owner's
    row_filter m_filter;
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

    // `p.pathset(pattern, kind=, skip=)`: the glob results as a set over p's own table.
    path_cls.def("pathset", [](const pathview& v, std::string_view pattern, std::optional<bool> case_sensitive,
                               std::optional<std::string_view> kind, const py::object& skip) {
                 glob_pattern parsed(pattern, case_sensitive, glob_kind_of(kind), skip_names(skip));
                 PathSet ps(v.table());
                 for (const file& m : glob_of(v.value(), std::move(parsed))) ps.add_value(m.value());
                 return ps;
             }, py::arg("pattern") = "*", py::kw_only(), py::arg("case_sensitive") = py::none(), py::arg("kind") = py::none(),
             py::arg("skip") = py::tuple(),
             "What glob(pattern) finds, as a PathSet over this path's table, for set algebra and Filter queries.\n"
             "kind: 'file' keeps what is_file() says is a file, 'dir' what is_dir() says is a directory (a link\n"
             "followed; the walk's listing tells, a link alone is stat'ed); None keeps every match.\n"
             "skip: names of folders to pass over (globs, as the pattern's case_sensitive says): a folder so\n"
             "named, or a link to one, is never entered or matched, nor is anything beneath it; a file so\n"
             "named is kept. `path(root).pathset('**/*.py', kind='file', skip=['.git', 'build'])`.");

    py::class_<file_content>(m, "FileContent", py::buffer_protocol(),
        "One member's bytes, read by PathSet.read_each(): held in C++ and shown as a read-only buffer, so\n"
        "memoryview(content) copies nothing. bytes(content) copies; text() decodes UTF-8; .path is the member.")
        .def_buffer(&file_content::buffer)
        .def_property_readonly("path", [es](const file_content& c) { return wrap(es, c.member()); }, "The member it was read from.")
        .def("__len__", [](const file_content& c) { return c.bytes().size(); })
        .def("__bytes__", [](const file_content& c) { return py::bytes(c.bytes().data(), c.bytes().size()); }, "A copy, as bytes.")
        .def("text", [](const file_content& c) { return utf8_text(c.bytes(), [&c] { return c.member().fspath(); }, "text"); },
             "The bytes decoded as UTF-8; ValueError naming the file when they are not.")
        .def("__repr__", [](const file_content& c) {
            return "FileContent(" + py::repr(str_from_text(c.member().fspath())).cast<std::string>() + ", " +
                   std::to_string(c.bytes().size()) + " bytes)";
        });

    py::class_<contents_reading>(m, "_ContentsReading")
        .def("__iter__", [](py::object self) { return self; })
        .def("__next__", [](contents_reading& reading) {
            std::optional<file_content> content = reading.next();
            if (!content) throw py::stop_iteration();
            return std::move(*content);
        });

    py::class_<pathset_iter>(m, "_PathSetIterator")
        .def("__iter__", [](py::object self) { return self; })
        .def("__next__", [es](pathset_iter& it) -> py::object {
            std::optional<py::object> next = it.next([es](pathview v) { return wrap(es, std::move(v)); });
            if (!next) throw py::stop_iteration();
            return std::move(*next);
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

    m.def("ext", [](const py::args& suffixes, bool case_sensitive) {
              if (suffixes.empty()) throw py::type_error("ext() takes one suffix or more: ext('.h', '.cpp')");
              std::vector<std::string> wanted;
              for (const py::handle s : suffixes) {
                  if (!PyUnicode_Check(s.ptr())) throw py::type_error("ext() takes suffixes as str, not " + std::string(Py_TYPE(s.ptr())->tp_name));
                  wanted.push_back(s.cast<std::string>());
              }
              return ext_filter(std::move(wanted), case_sensitive);
          }, py::arg("case_sensitive") = true,   // keyword-only: it follows *suffixes
          "Filter: pathlib's final suffix is one of `suffixes` ('.yaml'; ext('.h', '.cpp')), compared\n"
          "exactly, or but for the case of ASCII letters when case_sensitive is False.");
    m.def("name", [](std::string g) { return name_filter(std::move(g)); }, py::arg("glob"),
          "Filter: the final component matches the glob (`*` and `?`).");
    m.def("absolute", []() { return absolute_filter(); }, "Filter: pathlib's is_absolute().");
    m.def("match_pattern", [](std::string_view pattern, std::string_view text) { return glob_match(pattern, text); },
          py::arg("pattern"), py::arg("string"), "Whether `string` matches the glob segment `pattern` (`*`, `?` and `[seq]`), as pathlib reads one.");

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
            return pathset_iter(self, false);
        })
        .def("scan", [](py::object self) {
            return pathset_iter(self, true);
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
                 return path_query(self, f);
             }, py::is_operator())
        .def("__or__", [](py::object self, const row_filter& f) {
                 return path_query(self, f);
             }, py::is_operator())
        .def("__or__", &PathSet::union_with, py::is_operator())
        .def("__and__", &PathSet::intersection, py::is_operator())
        .def("__sub__", &PathSet::difference, py::is_operator())
        // + and - also take one path or an iterable of paths (pygim.pathset's spelling).
        .def("__add__", &PathSet::union_with, py::is_operator())
        .def("__add__", [](const PathSet& ps, py::handle items) { return ps.union_with(operand_over(ps, items, true)); }, py::is_operator())
        .def("__sub__", [](const PathSet& ps, py::handle items) { return ps.difference(operand_over(ps, items, false)); }, py::is_operator())
        .def("read_all_files", [](const PathSet& ps, std::optional<std::size_t> threads) {
                 const shared_paths paths = std::make_shared<const path_list>(ps.paths());   // copied with the GIL held; names a failing file after the run
                 chosen_width width = parallel::width_for(threads, "read_all_files");
                 const window_limits every = window_limits::whole(paths->size());
                 std::vector<read_result> results = run_interruptible(   // texts_of_files moves each text out
                     "read_all_files", [&] { return batch_read(paths, std::move(width), every); },
                     [](batch_read& job) { return job.collect(); });
                 return texts_of_files(results, *paths);
             }, py::kw_only(), py::arg("threads") = py::none(),
             "The text of every member that is a regular file (UTF-8), one str per file, in member order;\n"
             "directories, missing paths, link loops, FIFOs and devices are left out, as pathlib's is_file()\n"
             "says — a FIFO or device is opened (non-blocking) to be told apart, where is_file() opens nothing.\n"
             "The reads run in C++ with the GIL released, as read_each() reads. threads: how many read\n"
             "at once; None starts with one and adds threads while the rest is worth them — up to the cores\n"
             "for reads that compute (a warm cache), past them for reads that wait (a cold disk, a network\n"
             "mount). A file the OS refuses raises as pathlib does; bytes that are not UTF-8 raise ValueError\n"
             "naming the first such member.")
        .def("read_each", [](const PathSet& ps, std::optional<std::size_t> threads, std::size_t ahead_bytes) {
                 return contents_reading(ps, parallel::width_for(threads, "read_each"), reading_ahead(ahead_bytes));
             }, py::kw_only(), py::arg("threads") = py::none(), py::arg("ahead_bytes") = default_bytes_ahead,
             "Each member that is a regular file, as a FileContent in member order: a view of its bytes,\n"
             "held in C++ — memoryview(content) copies nothing; text() decodes UTF-8; .path names the member.\n"
             "Threads read ahead with the GIL released while you work on what they have read, at most 1,024\n"
             "files and about ahead_bytes bytes ahead (a file is started while less than that waits).\n"
             "threads: as read_all_files. Folders, missing paths, link loops, FIFOs and devices are left out\n"
             "(a FIFO or device opened, non-blocking, to be told apart); a file the OS refuses raises in its\n"
             "place, and the reading ends there.")
        .def("sizes", [](const PathSet& ps) {
                 const std::vector<fs::path> paths = ps.fs_paths();   // copied with the GIL held
                 const py::gil_scoped_release nogil;
                 return sizes_of(paths);
             },
             "Each member's size in bytes, in member order, as path.size() gives it (a link followed); the\n"
             "stats run with the GIL released. A member that is not there, or is not a file, raises the\n"
             "OSError path.size() raises, naming it.")
        .def("count_union", &PathSet::count_union, py::arg("other"),
             "len(self | other) without building the set: a popcount over the bitmaps when the tables are shared.")
        .def("count_intersection", &PathSet::count_intersection, py::arg("other"),
             "len(self & other) without building the set.")
        .def("count_difference", &PathSet::count_difference, py::arg("other"),
             "len(self - other) without building the set.")
        .def("relative_to", [](const PathSet& ps, py::handle other) {
                const text_arg t = text_view_of_arg(other);
                return ps.relative_to(file(t.view()));
            }, py::arg("other"),
             "Every member with *other* taken off its front, like pathlib's relative_to() per path, as a\n"
             "set over the same table in member order: `path(d).pathset('**/*').relative_to(d)` names\n"
             "everything beneath d from d. Raises ValueError naming the first member that is neither\n"
             "*other* nor beneath it.")
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
