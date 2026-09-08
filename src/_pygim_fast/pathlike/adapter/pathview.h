#pragma once
// pathlike/adapter/pathview.h — the path object: a handle on a row of a path_table.
//
// Python's `pygim.path` IS this struct: (table, row, pin). An object carries no
// parsed value of its own — its name, parent, suffix, equality and hash are
// read from the table by row, and the core value (basic_file, core.h) is built
// on demand for the few operations whose rules live there (with_suffix, joins
// with complex text, URIs, filesystem calls), then interned back into the same
// table. The constructor interns: `path(text)` tokenises straight into the
// module's default table (path_store.h), `path(text, store=s)` into s's, and
// every derived object inherits the table of the object it came from — so a
// program chooses a lifetime once, by argument, and a subtree of work follows
// it with no ambient state.
//
// Two handles on one row are equal, hash alike and are NOT promised to be the
// same object — as CPython interns strings for storage and fast equality
// without promising `is`. A handle keeps its table alive (shared_ptr), so a
// table lives as long as any handle or set over it.
//
// Typed classes: `path("x.yaml")` is a `yamlpath`, `path("x.json")` a
// `jsonpath` — one subclass per engine the build discovered (adapter.h's
// engine list), chosen by the pin or the extension exactly as read() chooses
// the decoder. A Python constructor cannot return a subclass instance, so the
// classes share a metaclass whose __call__ builds the object through wrap();
// `yamlpath("x")` pins its engine. isinstance(p, pygim.path) holds for all.
//
// A worked example, used in the comments below (POSIX; rows as path_table.h
// numbers them):
//
//     p = path("a/b/c.yaml")        table rows: 0 ".", 1 a, 2 a/b, 3 a/b/c.yaml   p = {T, 3}, a yamlpath
//     path("a//b/c.yaml/") == p     -> True    (row 3 again; a second handle)
//     p.parent                      -> {T, 2}, a plain path ("a/b" has no engine)
//     p / "d.json"                  -> {T, 4}, a jsonpath  (row 4 created under row 3)
//     p.with_suffix(".json")        -> value built, ".json" applied, interned: {T, 5}
//     path("x", store=mine)         -> {mine's table, row}; its parent is in mine's table too
//     yamlpath("notes.txt")         -> pinned: reads as YAML whatever the extension

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "../path_table.h"
#include "adapter.h"
#include "path_store.h"

namespace pygim::pathlike {

/// The handle. Copies nothing; keeps the table alive.
struct pathview {
    table_ptr table;
    std::uint32_t row = 0;
    const engine_info* pin = nullptr;   // the engine pinned at construction (inherited by derived paths)

    /// The core value of the row, with the pin: what the engines' load/write
    /// and the value-route operations take.
    [[nodiscard]] file value() const { return file(table->value(row), pin); }
    /// pathlib's str() of the row (path_table::render).      p.fspath() -> "a/b/c.yaml"
    [[nodiscard]] std::string fspath() const { return table->render<native_strategy>(row); }
    /// The final component (a view into the arena).          p.name() -> "c.yaml"
    [[nodiscard]] std::string_view name() const noexcept { return table->name(row); }
    /// The lower-cased final suffix: the registry's dispatch key.   p.ext_key() -> ".yaml"
    [[nodiscard]] std::string ext_key() const { return uri::ascii_lower(detail::suffix_of(name())); }
    /// The same into a caller's buffer, no allocation for suffixes up to 31 bytes (all real ones).
    [[nodiscard]] std::string_view ext_key_into(char (&buf)[32]) const noexcept {
        const std::string_view sfx = detail::suffix_of(name());
        if (sfx.size() > sizeof(buf)) return sfx;   // dispatch will miss; a suffix this long has no engine
        for (std::size_t i = 0; i < sfx.size(); ++i) {
            const char c = sfx[i];
            buf[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
        }
        return {buf, sfx.size()};
    }
    /// Same path? Same table: same row. Different tables: `o`'s chain looked up here.
    [[nodiscard]] bool equals(const pathview& o) const {
        if (table.get() == o.table.get()) return row == o.row;
        return table->find(*o.table, o.row) == row;
    }
    /// Another row of the same table, the same pin.           p.at(2) -> p.parent
    [[nodiscard]] pathview at(std::uint32_t r) const { return {table, r, pin}; }
    /// A value interned into this handle's table, the same pin: the tail of
    /// every value-route operation.
    [[nodiscard]] pathview intern(const file& f) const { return {table, table->insert<native_strategy>(f.value()), pin}; }
};

/// The row for path TEXT in a table: native text (no scheme) is tokenised
/// straight in; a file:// URI goes through the core constructor, which owns
/// the URI rules (and raises for a remote host or a relative URI).
[[nodiscard]] inline std::uint32_t intern_text(path_table& t, std::string_view text) {
    if (uri::scheme_of(text).empty()) return t.insert<native_strategy>(text);
    return t.insert<native_strategy>(file(text).value());
}

// ── typed classes ───────────────────────────────────────────────────────────
/// One subclass per engine: `yamlpath`, `jsonpath`, ... Constructing one pins
/// its engine; a plain `path` gets one of these when its pin or extension
/// resolves. No state of its own.
template <Engine E>
struct typed_view : pathview {
    explicit typed_view(pathview v) : pathview(std::move(v)) {}
};

/// The Python type of each typed class -> its engine: what the metaclass
/// consults to pin `yamlpath("x")`.
inline std::unordered_map<PyTypeObject*, const engine_info*>& typed_types() {
    static auto* m = new std::unordered_map<PyTypeObject*, const engine_info*>();
    return *m;
}

template <Engine E>
bool wrap_if(const engine_info* e, pathview& v, py::object& out) {
    if (e != &E::info) return false;
    out = py::cast(typed_view<E>(std::move(v)));
    return true;
}
/// The Python object for a handle: the typed subclass of its resolved engine
/// (the pin, else the extension), or a plain `path` when none resolves.
///
///     wrap(es, {T, 3, nullptr})   -> yamlpath      wrap(es, {T, 2, nullptr}) -> path
template <Engine... Es>
[[nodiscard]] py::object wrap(engine_list<Es...>, pathview v) {
    char buf[32];
    const engine_info* e = v.pin ? v.pin : engine_list<Es...>::for_ext_at_runtime(v.ext_key_into(buf));
    py::object out;
    const bool hit = (wrap_if<Es>(e, v, out) || ...);
    return hit ? out : py::cast(std::move(v));
}
template <Engine... Es>
[[nodiscard]] py::list wrap_all(engine_list<Es...> es, const pathview& like, std::vector<file> values) {
    py::list out;
    for (file& f : values) out.append(wrap(es, like.intern(f)));
    return out;
}

// None or "" -> auto (nullptr); a known selector -> its engine; else ValueError.
template <Engine... Es>
[[nodiscard]] const engine_info* engine_from_arg(engine_list<Es...>, py::handle name) {
    using L = engine_list<Es...>;
    if (name.is_none()) return nullptr;
    if (!PyUnicode_Check(name.ptr())) throw py::type_error("engine must be a str or None");
    py::ssize_t n = 0;
    const char* s = PyUnicode_AsUTF8AndSize(name.ptr(), &n);
    if (!s) throw py::error_already_set();
    const std::string_view sv(s, static_cast<std::size_t>(n));
    if (sv.empty()) return nullptr;
    if (const engine_info* e = L::from_name_at_runtime(sv)) return e;
    throw std::invalid_argument("unknown engine: '" + std::string(sv) + "' (known: " + L::known_text() + ")");
}

/// The handle for a constructor call: text into the chosen table.
[[nodiscard]] inline pathview make_view(py::handle text, const engine_info* pin, py::handle store) {
    table_ptr table = store.is_none() ? default_table() : as_store(store).table();
    const text_arg t = text_view_of_arg(text);
    const std::uint32_t r = intern_text(*table, t.view);
    return {std::move(table), r, pin};
}

// ── derived paths ───────────────────────────────────────────────────────────
/// One plain component: no separator, not empty, not "." (dropped by the join),
/// not a Windows drive spelling. ".." IS a plain component, as in pathlib.
[[nodiscard]] inline bool plain_component(std::string_view s) noexcept {
    if (s.empty() || s == ".") return false;
    if (s.size() >= 2 && s[1] == ':') return false;
    for (const char c : s) {
        if (native_strategy::is_sep(c)) return false;
    }
    return true;
}
/// `p / other`: one plain component is a child row (one interner lookup, one
/// trie probe); anything else — absolute right-hand sides, several
/// components, "." and "" — takes the core's joined() and is interned.
template <Engine... Es>
[[nodiscard]] py::object joined(engine_list<Es...> es, const pathview& v, std::string_view other) {
    if (plain_component(other)) return wrap(es, v.at(v.table->child_of(v.row, other)));
    return wrap(es, v.intern(v.value().joined(other)));
}

/// The docstrings the binding needs from the registry (composed in bindings.cpp).
struct path_docs {
    const std::string& cls;
    const std::string& engine;
    const std::string& read;
    const std::string& write;
};

[[nodiscard]] inline std::string_view requested_of(const std::optional<std::string>& engine) {
    return engine ? std::string_view(*engine) : std::string_view{};
}

template <Engine E>
void bind_typed_one(py::module_& m, py::handle meta) {
    static const std::string name = std::string(E::info.name) + "path";
    static const std::string doc = std::string(E::info.doc) + " Constructing one pins the engine (" +
                                   std::string(E::info.label) + ").";
    py::class_<typed_view<E>, pathview> cls(m, name.c_str(), py::metaclass(meta), doc.c_str());
    cls.def(py::init([](py::handle p, py::object store) { return typed_view<E>(make_view(p, &E::info, store)); }),
            py::arg("path"), py::kw_only(), py::arg("store") = py::none());
    typed_types()[reinterpret_cast<PyTypeObject*>(cls.ptr())] = &E::info;
}

/// Binds `path` (the handle) and its typed subclasses. Returns the class so
/// pathset.h can add `pathset()` to it.
template <Engine... Es>
py::class_<pathview> bind_path(engine_list<Es...> es, py::module_& m, const path_docs& docs) {
    using L = engine_list<Es...>;

    // The metaclass: `path(...)` and `yamlpath(...)` build through wrap(), so
    // the constructed object is the typed subclass its engine resolves to —
    // something a Python constructor cannot do on its own. Other subclasses
    // (a user's `class Mine(path)`) construct the ordinary way.
    py::object base_meta = py::type::of<path_store>().attr("__class__");   // pybind11's metaclass
    py::dict ns;
    ns["__module__"] = "pygim.pathlike";
    ns["__call__"] = py::cpp_function([es](py::object cls, py::args args, py::kwargs kwargs) -> py::object {
        static PyTypeObject* const base_type = reinterpret_cast<PyTypeObject*>(py::type::of<pathview>().ptr());
        auto* tp = reinterpret_cast<PyTypeObject*>(cls.ptr());
        const bool is_base = tp == base_type;
        const auto typed = is_base ? typed_types().end() : typed_types().find(tp);
        if (!is_base && typed == typed_types().end()) {
            return py::reinterpret_steal<py::object>(PyType_Type.tp_call(cls.ptr(), args.ptr(), kwargs.ptr()));
        }
        // One pass over the keywords (a dict of 0-3 entries), compared as C strings:
        // no Python string is built per lookup.
        py::handle text, engine, store;
        Py_ssize_t pos = 0;
        PyObject *key = nullptr, *value = nullptr;
        while (PyDict_Next(kwargs.ptr(), &pos, &key, &value)) {
            if (PyUnicode_CompareWithASCIIString(key, "store") == 0) store = value;
            else if (PyUnicode_CompareWithASCIIString(key, "engine") == 0) engine = value;
            else if (PyUnicode_CompareWithASCIIString(key, "path") == 0) text = value;
            else throw py::type_error("path() got an unexpected keyword argument '" + py::str(key).cast<std::string>() + "'");
        }
        const std::size_t n = args.size();
        if (n >= 1) text = args[0];
        if (!text) throw py::type_error("path() missing the 'path' argument");
        const engine_info* pin = nullptr;
        if (is_base) {
            if (n >= 2) engine = args[1];
            if (n > 2) throw py::type_error("path() takes at most 2 positional arguments");
            pin = engine ? engine_from_arg(es, engine) : nullptr;
        } else {
            if (n > 1 || engine) throw py::type_error("a typed path pins its own engine");
            pin = typed->second;
        }
        return wrap(es, make_view(text, pin, store ? store : py::handle(py::none().ptr())));
    }, py::is_method(base_meta));   // a method descriptor, so Python binds `cls` as the first argument
    py::object meta = py::reinterpret_steal<py::object>(
        PyObject_CallFunction(reinterpret_cast<PyObject*>(&PyType_Type), "sOO", "_PathMeta", py::make_tuple(base_meta).ptr(), ns.ptr()));
    if (!meta) throw py::error_already_set();

    py::class_<pathview> cls(m, "path", py::metaclass(meta), docs.cls.c_str());
    cls.def(py::init([es](py::handle p, py::handle engine, py::object store) {
                return make_view(p, engine_from_arg(es, engine), store);
            }), py::arg("path"), py::arg("engine") = py::none(), py::kw_only(), py::arg("store") = py::none())
        .def_property_readonly("engine", [](const pathview& v) -> py::object {
                const engine_info* e = v.pin ? v.pin : L::for_ext_at_runtime(v.ext_key());
                if (!e) return py::none();
                return py::str(std::string(e->label));
            }, py::doc(docs.engine.c_str()))
        .def_property_readonly("store", [](const pathview& v) { return path_store(v.table); },
                               "A PathStore over this path's table (the one derived paths share).")
        // -- text, identity --
        .def("__fspath__", [](const pathview& v) { return str_from_text(v.fspath()); },
             "os.PathLike protocol: the plain path string.")
        .def("__str__", [](const pathview& v) { return str_from_text(v.fspath()); })
        .def("__repr__", [](const pathview& v) { return v.value().repr(); })
        .def("__eq__", [](const pathview& a, const pathview& b) { return a.equals(b); }, py::is_operator())
        .def("__hash__", [](const pathview& v) {
                const auto h = static_cast<py::ssize_t>(v.table->hash(v.row));   // == the value's hash
                return h == -1 ? py::ssize_t(-2) : h;
            })
        .def_property_readonly("uri", [](const pathview& v) { return v.value().as_uri(); },
                               "The RFC 3986 file URI of an absolute path (percent-encoded: "
                               "file:///a%20b, file://host/share/x); a relative path keeps the "
                               "'file://<path>' spelling.")
        // -- name components, by row --
        .def_property_readonly("name", [](const pathview& v) { return str_from_text(v.name()); }, "The final path component.")
        .def_property_readonly("stem", [](const pathview& v) { return str_from_text(detail::stem_of(v.name())); },
                               "The final component without its suffix.")
        .def_property_readonly("suffix", [](const pathview& v) { return str_from_text(detail::suffix_of(v.name())); },
                               "The final extension incl. dot ('.yaml').")
        .def_property_readonly("suffixes", [](const pathview& v) {
                py::list out;
                for (const std::string& s : v.value().suffixes()) out.append(str_from_text(s));
                return out;
            }, "All extensions of the final component ('.tar.gz' -> ['.tar','.gz']).")
        .def_property_readonly("parts", [](const pathview& v) {
                py::list out;
                for (const std::string& s : v.value().parts()) out.append(str_from_text(s));
                return out;
            }, "The path components as a list.")
        .def_property_readonly("depth", [](const pathview& v) { return v.table->depth(v.row); },
                               "Named components below the anchor.")
        // -- composition: by row when one component, by value otherwise --
        .def("__truediv__", [es](const pathview& v, py::handle other) {
                const text_arg t = text_view_of_arg(other);
                return joined(es, v, t.view);
            }, py::is_operator())
        .def("__rtruediv__", [es](const pathview& v, py::handle other) {
                const text_arg t = text_view_of_arg(other);
                return wrap(es, v.intern(v.value().rjoined(t.view)));
            }, py::is_operator())
        .def("joinpath", [es](py::object self, const py::args& parts) {
                py::object cur = std::move(self);
                for (const py::handle& part : parts) {
                    const text_arg t = text_view_of_arg(part);
                    cur = joined(es, py::cast<const pathview&>(cur), t.view);
                }
                return cur;
            }, "Append one or more path components, like pathlib's joinpath().")
        .def_property_readonly("parent", [es](const pathview& v) { return wrap(es, v.at(v.table->parent(v.row))); },
                               "The parent directory (pathlib's rule: the anchor is its own parent).")
        .def_property_readonly("parents", [es](const pathview& v) {
                py::list out;
                for (const std::uint32_t r : v.table->parents(v.row)) out.append(wrap(es, v.at(r)));
                return out;
            }, "Ancestor directories, closest first.")
        // -- derived paths whose rules live in the core value --
        .def("with_suffix", [es](const pathview& v, const std::string& s) { return wrap(es, v.intern(v.value().with_suffix(s))); },
             py::arg("suffix"), "A copy with the final suffix replaced.")
        .def("with_name", [es](const pathview& v, const std::string& n) { return wrap(es, v.intern(v.value().with_name(n))); },
             py::arg("name"), "A copy with the final component replaced.")
        .def("with_stem", [es](const pathview& v, const std::string& s) { return wrap(es, v.intern(v.value().with_stem(s))); },
             py::arg("stem"), "A copy with the stem replaced (suffix kept).")
        .def("absolute", [es](const pathview& v) { return wrap(es, v.intern(v.value().absolute())); },
             "An absolute copy (does not resolve symlinks/..).")
        .def("resolve", [es](const pathview& v) { return wrap(es, v.intern(v.value().resolve())); },
             "A canonical, absolute copy (resolves symlinks/..).")
        // -- filesystem --
        .def("is_absolute", [](const pathview& v) { return v.table->is_absolute<native_strategy>(v.row); },
             "Whether the path is absolute.")
        .def("exists", [](const pathview& v) { return v.value().exists(); }, "Whether the path exists on disk.")
        .def("is_file", [](const pathview& v) { return v.value().is_file(); }, "Whether it is a regular file.")
        .def("is_dir", [](const pathview& v) { return v.value().is_dir(); }, "Whether it is a directory.")
        .def("is_symlink", [](const pathview& v) { return v.value().is_symlink(); }, "Whether it is a symbolic link.")
        .def("size", [](const pathview& v) { return v.value().size(); }, "File size in bytes (raises if it does not exist).")
        .def("read_bytes", [](const pathview& v) { return py::bytes(v.value().read_bytes()); }, "The raw file bytes, undecoded.")
        .def("write_bytes", [](const pathview& v, py::bytes data) {
                const std::string_view sv = data;
                const file f = v.value();
                py::gil_scoped_release nogil;
                f.write_bytes(sv);
            }, py::arg("data"), "Replace the file's contents with the given bytes.")
        .def("mkdir", [](const pathview& v, bool parents, bool exist_ok) { v.value().mkdir(parents, exist_ok); },
             py::arg("parents") = false, py::arg("exist_ok") = false,
             "Create this directory, like pathlib: parents= creates missing ancestors, exist_ok= tolerates an existing directory.")
        .def("read", [](const pathview& v, const std::optional<std::string>& engine, py::ssize_t key_cache) {
                const file f = v.value();
                const std::size_t cap = key_cache < 0 ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(key_cache);
                return load(L{}, L::resolve(f, requested_of(engine)), f, cap);
            }, py::arg("engine") = py::none(), py::arg("key_cache") = 256, py::doc(docs.read.c_str()))
        .def("write", [](const pathview& v, py::handle obj, const std::optional<std::string>& engine) {
                const file f = v.value();
                write(L{}, L::resolve(f, requested_of(engine)), f, obj);
            }, py::arg("obj"), py::arg("engine") = py::none(), py::doc(docs.write.c_str()))
        // -- traversal: results are rows of the same table and inherit the pin --
        .def("iterdir", [es](const pathview& v) { return wrap_all(es, v, v.value().iterdir()); },
             "The directory's children, sorted.")
        .def("glob", [es](const pathview& v, std::string_view p) { return wrap_all(es, v, v.value().glob(p)); },
             py::arg("pattern"),
             "Relative glob: * and ? within a component, ** across directories. "
             "Sorted and deduplicated; results inherit the engine pin.")
        .def("rglob", [es](const pathview& v, std::string_view p) { return wrap_all(es, v, v.value().rglob(p)); },
             py::arg("pattern"), "glob('**/' + pattern): the pattern anywhere under this directory.");

    (bind_typed_one<Es>(m, meta), ...);
    return cls;
}

}  // namespace pygim::pathlike
