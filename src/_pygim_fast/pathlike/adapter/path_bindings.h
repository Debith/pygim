#pragma once
// pathlike/adapter/path_bindings.h — Python's `pygim.path`: the handle of ../pathview.h, bound.
//
// Python's `pygim.path` IS this struct: (table, row, pin). An object carries no
// parsed value of its own — its name, parent, suffix, equality and hash are
// read from the table by row, and the core value (basic_file, core.h) is built
// on demand for the few operations whose rules live there (with_suffix, joins
// with complex text, URIs, filesystem calls), then interned back into the same
// table. The constructor interns: `path(text)` tokenises straight into the
// module's default table (../path_store.h), `path(text, store=s)` into s's, and
// every derived object inherits the table of the object it came from — so a
// program chooses a lifetime once, by argument, and a subtree of work follows
// it, with no global state.
//
// Two handles on one row are equal, hash alike and are NOT promised to be the
// same object — as CPython interns strings for storage and fast equality
// without promising `is`. A handle keeps its table alive (shared_ptr), so a
// table lives as long as any handle or set over it.
//
// Threading: a table is append-only with a single writer — every insert
// happens on the thread holding the GIL (a constructor, a derived-path
// operation, a set being built) — and reads never lock. A read of many files (PathSet's
// read_all_files, read_each) and every glob release the GIL around their I/O (a glob that may list
// many directories owning Ctrl-C too); a single file's read or write and iterdir hold it.
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
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "../../utils/interrupted.h"
#include "../../wiring/registry/core.h"
#include "../../utils/parallel.h"
#include "../parallel_glob.h"
#include "../path_table.h"
#include "../pathview.h"
#include "adapter.h"
#include "path_store_bindings.h"

namespace pygim::pathlike {

/// `bytes` made `n` bytes long, its content kept up to n, and where its bytes lie: a bytes object
/// a file is read into, sized as the read goes. Holds the GIL; MemoryError when it cannot.
inline char* resized_bytes(py::object& bytes, std::size_t n) {
    PyObject* raw = bytes.release().ptr();
    const auto size = static_cast<Py_ssize_t>(n);
    if (raw == nullptr ? (raw = PyBytes_FromStringAndSize(nullptr, size)) == nullptr : _PyBytes_Resize(&raw, size) != 0) {
        throw py::error_already_set();   // _PyBytes_Resize freed it
    }
    bytes = py::reinterpret_steal<py::object>(raw);
    return PyBytes_AS_STRING(raw);
}

/// open()'s newline argument: None (universal newlines), "", "\n", "\r" or "\r\n"; any other a
/// ValueError, as open() raises, and a non-str a TypeError.
inline std::optional<std::string> newline_of(py::handle newline) {
    if (newline.is_none()) return std::nullopt;
    if (!PyUnicode_Check(newline.ptr())) throw py::type_error("newline must be str or None");
    const std::string n = py::cast<std::string>(newline);
    if (n != "" && n != "\n" && n != "\r" && n != "\r\n") throw py::value_error("illegal newline value: " + py::repr(newline).cast<std::string>());
    return n;
}

/// `text` with each `from` replaced by `to` (str.replace through the C API).
inline py::object replaced(const py::object& text, const char* from, std::string_view to) {
    py::object out = py::reinterpret_steal<py::object>(
        PyUnicode_Replace(text.ptr(), py::str(from).ptr(), py::str(std::string(to)).ptr(), -1));
    if (!out) throw py::error_already_set();
    return out;
}

/// Two paths in pathlib's order: their texts as str() gives them, rendered into this thread's two
/// strings, whose room a sort reuses comparison after comparison (each rendered into a new string
/// made sorting 100,000 paths 790 ms, pathlib's 434). One row of one table is itself.
inline std::strong_ordering order_of(const pathview& a, const pathview& b) {
    if (a.table() == b.table() && a.row() == b.row()) return std::strong_ordering::equal;
    thread_local std::string a_text, b_text;
    a.fspath_into(a_text);
    b.fspath_into(b_text);
    return native_strategy::text_order(a_text, b_text);
}

/// f() where pybind11 is not the caller — in a type slot — its C++ exception set as the Python one
/// pybind11 would have set; `failed` then.
template <class R, class F>
R in_slot(R failed, F&& f) noexcept {
    try {
        return f();
    } catch (py::error_already_set& e) {
        e.restore();
    } catch (const py::builtin_exception& e) {
        e.set_error();
    } catch (const std::bad_alloc&) {
        PyErr_NoMemory();
    } catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
    }
    return failed;
}

/// The Python type `path`, set as it is made (custom_type_setup, below): what compare_slot asks
/// the other operand to be.
inline PyTypeObject* path_type = nullptr;

/// The pathview an object of `path_type` holds, read from pybind11's instance: a py::cast looks
/// the C++ type up by its name, in two maps, on every call — two thirds of a comparison's time
/// once the dispatcher was gone. A typed class's value is a pathview at its start (bind_typed_one).
/// Null, with TypeError set, when no constructor filled the object.
inline const pathview* held(PyObject* obj) noexcept {
    const auto v_h = reinterpret_cast<py::detail::instance*>(obj)->get_value_and_holder();
    if (v_h.holder_constructed()) return v_h.value_ptr<pathview>();
    PyErr_Format(PyExc_TypeError, "this %s holds no path: no constructor made it", Py_TYPE(obj)->tp_name);
    return nullptr;
}

/// The type's own comparison and hash, its tp_richcompare and tp_hash, where they were pybind11
/// methods: a sort, a set and a dict call them straight, not through pybind11's dispatcher (D85:
/// one `a < b` 307 ns, pathlib's 214). Two paths compare by equals() and pathlib's order
/// (order_of); anything else is NotImplemented, so == is False and < a TypeError, as pathlib's
/// are. CPython hands a slot its own type's object first.
inline PyObject* compare_slot(PyObject* self, PyObject* other, int op) noexcept {
    if (!PyObject_TypeCheck(other, path_type)) Py_RETURN_NOTIMPLEMENTED;
    const pathview* a = held(self);
    const pathview* b = a ? held(other) : nullptr;
    if (!b) return nullptr;
    return in_slot<PyObject*>(nullptr, [&] {
        if (op == Py_EQ || op == Py_NE) return PyBool_FromLong(a->equals(*b) == (op == Py_EQ));
        const std::strong_ordering order = order_of(*a, *b);
        return PyBool_FromLong(op == Py_LT ? order < 0 : op == Py_LE ? order <= 0 : op == Py_GT ? order > 0 : order >= 0);
    });
}

/// == the value's hash, as the table keeps it.
inline Py_hash_t hash_slot(PyObject* self) noexcept {
    const pathview* v = held(self);
    if (!v) return -1;
    const auto h = static_cast<Py_hash_t>(v->table()->hash(v->row()));
    return h == -1 ? Py_hash_t(-2) : h;
}

/// What a glob finds under `root`, walked with the GIL released, touching only its own values. The
/// pattern is parsed by the caller, so a bad one raises before any thread starts. One that lists one
/// directory at most (`src/*.py`) walks here. One that may list many — a `**`, or a wildcard before
/// its last segment (`*/*/*.py`) — walks its first glob_head_start directories here too, and if it
/// is still going, the rest on worker threads, owning Ctrl-C and SIGTERM (#272).
[[nodiscard]] inline std::vector<file> glob_of(const file& root, glob_pattern parsed) {
    if (!parsed.lists_many_directories()) {
        const py::gil_scoped_release nogil;
        return root.glob(parsed);
    }
    glob_walk started(parsed, root.to_fs());
    {
        const py::gil_scoped_release nogil;
        if (started.walk(parsed, glob_head_start)) return root.found_by_glob(std::move(started).found());
    }
    const std::size_t cores = parallel::this_machines_cores();
    parallel::chosen_width width(parallel::elastic_width(cores, parallel::elastic_cap(cores)));
    return run_interruptible("glob", [&] { return parallel_glob(root, std::move(parsed), std::move(width), std::move(started)); },
                             [](parallel_glob& walk) { return walk.results(); });
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
/// consults to pin `yamlpath("x")`. A registry (#131): a class registered twice is an error.
using typed_registry = core::DynamicRegistryCore<PyTypeObject*, const engine_info*>;
inline typed_registry& typed_types() {
    static auto* r = new typed_registry();   // never destroyed: classes are registered once, read to the end
    return *r;
}

/// `path` itself or a typed class: pygim's own, which path() chooses, and whose objects are values.
inline bool is_pygims_own(PyTypeObject* tp) {
    return tp == path_type || typed_types().value_or(tp, nullptr) != nullptr;
}

/// pickle's recipe for a path, as pathlib's and Decimal's are: its class called with its text, and
/// with its pinned engine's name when it has one. A path of pygim's own class is made by path(),
/// which chooses the class again (the typed class itself would pin what the suffix only chose); a
/// user's subclass is called as itself. The store is not kept: the path loads into the default one.
inline py::tuple reduced(py::handle self) {
    const pathview* v = held(self.ptr());   // an object no constructor filled is refused (memory #307)
    if (!v) throw py::error_already_set();
    PyTypeObject* tp = Py_TYPE(self.ptr());
    const py::handle cls(reinterpret_cast<PyObject*>(is_pygims_own(tp) ? path_type : tp));
    py::object text = str_from_text(v->fspath());
    const py::tuple args = v->pin() ? py::tuple(py::make_tuple(text, std::string(v->pin()->name))) : py::tuple(py::make_tuple(text));
    return py::make_tuple(cls, args);
}

/// copy and deepcopy as Decimal's: a path of pygim's own class is a value, so the object itself,
/// its store kept; a user's subclass may not be one, so it is made again from its recipe. An
/// object no constructor filled is refused, as every other method refuses it (memory #307).
inline py::object copied(py::handle self) {
    if (!held(self.ptr())) throw py::error_already_set();
    if (is_pygims_own(Py_TYPE(self.ptr()))) return py::reinterpret_borrow<py::object>(self);
    const py::tuple recipe = reduced(self);
    return recipe[0](*py::tuple(recipe[1]));
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
    char buf[pathview::ext_key_room];
    const engine_info* e = v.pin() ? v.pin() : engine_list<Es...>::for_ext_at_runtime(v.ext_key_into(buf));
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
    const std::uint32_t r = intern_text(*table, t.view());
    return {std::move(table), r, pin};
}

// ── derived paths ───────────────────────────────────────────────────────────
/// `p / other`: one plain component is a child row (one interner lookup, one
/// trie probe); anything else — absolute right-hand sides, several
/// components, "." and "" — takes the core's joined() and is interned.
template <Engine... Es>
[[nodiscard]] py::object joined(engine_list<Es...> es, const pathview& v, std::string_view other) {
    if (detail::plain_component<native_strategy>(other)) return wrap(es, v.at(v.table()->child_of(v.row(), other)));
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
    static_assert(sizeof(typed_view<E>) == sizeof(pathview) && !std::is_polymorphic_v<typed_view<E>>,
                  "held() reads a typed path's value as the pathview at its start");
    static const std::string name = std::string(E::info.name) + "path";
    static const std::string doc = std::string(E::info.doc) + " Constructing one pins the engine (" +
                                   std::string(E::info.label) + ").";
    py::class_<typed_view<E>, pathview> cls(m, name.c_str(), py::metaclass(meta), doc.c_str());
    cls.def(py::init([](py::handle p, py::object store) { return typed_view<E>(make_view(p, &E::info, store)); }),
            py::arg("path"), py::kw_only(), py::arg("store") = py::none());
    typed_types().register_or_override(reinterpret_cast<PyTypeObject*>(cls.ptr()), &E::info, false);
}

/// Binds `path` (the handle) and its typed subclasses. Returns the class so
/// pathset_bindings.h can add `pathset()` to it.
template <Engine... Es>
py::class_<pathview> bind_path(engine_list<Es...> es, py::module_& m, const path_docs& docs) {
    using L = engine_list<Es...>;

    // The metaclass: `path(...)` and `yamlpath(...)` build through wrap(), so
    // the constructed object is the typed subclass its engine resolves to —
    // something a Python constructor cannot do on its own. Other subclasses
    // (a user's `class Mine(path)`) construct as any pybind11 class does.
    py::object base_meta = py::type::of<path_store>().attr("__class__");   // pybind11's metaclass
    const ternaryfunc pybind11_call = reinterpret_cast<PyTypeObject*>(base_meta.ptr())->tp_call;
    py::dict ns;
    ns["__module__"] = "pygim.pathlike";
    ns["__call__"] = py::cpp_function([es, pybind11_call](py::object cls, py::args args, py::kwargs kwargs) -> py::object {
        static PyTypeObject* const base_type = reinterpret_cast<PyTypeObject*>(py::type::of<pathview>().ptr());
        auto* tp = reinterpret_cast<PyTypeObject*>(cls.ptr());
        const bool is_base = tp == base_type;
        const engine_info* typed = is_base ? nullptr : typed_types().value_or(tp, nullptr);
        if (!is_base && typed == nullptr) {
            // A user's subclass, made by pybind11's metaclass call, which raises TypeError when its
            // __init__ skipped the base one. type's own call did not, and the object held memory
            // nothing wrote (until 2026-11-01). A NULL here is that error, or one its __new__ or
            // __init__ raised: handed on as itself; returned as an empty object, pybind11 would
            // replace it with a TypeError.
            py::object built = py::reinterpret_steal<py::object>(pybind11_call(cls.ptr(), args.ptr(), kwargs.ptr()));
            if (!built) throw py::error_already_set();
            return built;
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
            pin = typed;
        }
        return wrap(es, make_view(text, pin, store ? store : py::handle(py::none().ptr())));
    }, py::is_method(base_meta));   // a method descriptor, so Python binds `cls` as the first argument
    py::object meta = py::reinterpret_steal<py::object>(
        PyObject_CallFunction(reinterpret_cast<PyObject*>(&PyType_Type), "sOO", "_PathMeta", py::make_tuple(base_meta).ptr(), ns.ptr()));
    if (!meta) throw py::error_already_set();

    py::class_<pathview> cls(m, "path", py::metaclass(meta), docs.cls.c_str(), py::custom_type_setup([](PyHeapTypeObject* heap) {
        path_type = &heap->ht_type;
        path_type->tp_richcompare = compare_slot;   // PyType_Ready, next, adds __eq__ ... __ge__ and __hash__
        path_type->tp_hash = hash_slot;             // over them; the typed classes inherit both
    }));
    cls.def(py::init([es](py::handle p, py::handle engine, py::object store) {
                return make_view(p, engine_from_arg(es, engine), store);
            }), py::arg("path"), py::arg("engine") = py::none(), py::kw_only(), py::arg("store") = py::none())
        .def_property_readonly("engine", [](const pathview& v) -> py::object {
                const engine_info* e = v.pin() ? v.pin() : L::for_ext_at_runtime(v.ext_key());
                if (!e) return py::none();
                return py::str(std::string(e->label));
            }, py::doc(docs.engine.c_str()))
        .def_property_readonly("store", [](const pathview& v) { return path_store(v.table()); },
                               "A PathStore over this path's table (the one derived paths share).")
        // -- text, identity --
        .def("__fspath__", [](const pathview& v) { return str_from_text(v.fspath()); },
             "os.PathLike protocol: the plain path string.")
        .def("__str__", [](const pathview& v) { return str_from_text(v.fspath()); })
        .def("__repr__", [](const pathview& v) { return v.value().repr(); })
        // ==, !=, <, <=, >, >= and hash(): the type's own slots (compare_slot, hash_slot)
        // -- pickle and copy (pathlib parity, D89) --
        .def("__reduce__", [](py::handle self) { return reduced(self); },
             "pickle's recipe: the class, the text, and the engine when one is pinned. It loads into the default store.")
        .def("__copy__", [](py::handle self) { return copied(self); }, "The path itself: a path is a value.")
        .def("__deepcopy__", [](py::handle self, py::handle) { return copied(self); }, py::arg("memo"),
             "The path itself: a path is a value.")
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
        .def_property_readonly("depth", [](const pathview& v) { return v.table()->depth(v.row()); },
                               "Named components below the anchor.")
        // -- composition: by row when one component, by value otherwise --
        .def("__truediv__", [es](const pathview& v, py::handle other) {
                const text_arg t = text_view_of_arg(other);
                return joined(es, v, t.view());
            }, py::is_operator())
        .def("__rtruediv__", [es](const pathview& v, py::handle other) {
                const text_arg t = text_view_of_arg(other);
                return wrap(es, v.intern(v.value().rjoined(t.view())));
            }, py::is_operator())
        .def("joinpath", [es](py::object self, const py::args& parts) {
                py::object cur = std::move(self);
                for (const py::handle& part : parts) {
                    const text_arg t = text_view_of_arg(part);
                    cur = joined(es, py::cast<const pathview&>(cur), t.view());
                }
                return cur;
            }, "Append one or more path components, like pathlib's joinpath().")
        .def_property_readonly("parent", [es](const pathview& v) { return wrap(es, v.at(v.table()->parent(v.row()))); },
                               "The parent directory (pathlib's rule: the anchor is its own parent).")
        .def_property_readonly("parents", [es](const pathview& v) {
                py::list out;
                for (const std::uint32_t r : v.table()->parents(v.row())) out.append(wrap(es, v.at(r)));
                return out;
            }, "Ancestor directories, closest first.")
        .def("relative_to", [es](const pathview& v, py::handle other) {
                const text_arg t = text_view_of_arg(other);
                const file base(t.view());
                if (const auto rest = v.value().relative_to(base)) return wrap(es, v.intern(*rest));
                throw py::value_error("'" + v.fspath() + "' is not in the subpath of '" + base.fspath() + "'");
            }, py::arg("other"),
             "This path with *other* taken off its front, like pathlib's relative_to(): `path('/a/b/c')`\n"
             "relative to '/a' is `b/c`, and a path relative to itself is '.'. *other* is text, an\n"
             "os.PathLike or a path. Raises ValueError when this path is neither *other* nor beneath it.\n"
             "Compared as written, as == compares: on Windows letter case is not folded.")
        .def("is_relative_to", [](const pathview& v, py::handle other) {
                const text_arg t = text_view_of_arg(other);
                return v.value().is_relative_to(file(t.view()));
            }, py::arg("other"), "Whether relative_to(other) would succeed: this path is *other* or beneath it.")
        // -- derived paths whose rules live in the core value --
        .def("with_suffix", [es](const pathview& v, const std::string& s) { return wrap(es, v.intern(v.value().with_suffix(s))); },
             py::arg("suffix"), "A copy with the final suffix replaced.")
        .def("with_name", [es](const pathview& v, const std::string& n) { return wrap(es, v.intern(v.value().with_name(n))); },
             py::arg("name"), "A copy with the final component replaced.")
        .def("with_stem", [es](const pathview& v, const std::string& s) { return wrap(es, v.intern(v.value().with_stem(s))); },
             py::arg("stem"), "A copy with the stem replaced (suffix kept).")
        .def("absolute", [es](const pathview& v) { return wrap(es, v.intern(v.value().absolute())); },
             "An absolute copy (does not resolve symlinks/..).")
        .def("resolve", [es](const pathview& v, bool strict) { return wrap(es, v.intern(v.value().resolve(strict))); },
             py::arg("strict") = false,
             "pathlib's resolve(): an absolute copy, its symlinks and '..' resolved; strict, a missing part is FileNotFoundError.")
        .def("expanduser", [es](const pathview& v) {
                const file f = v.value();
                file expanded;
                {
                    py::gil_scoped_release nogil;   // the password database may be asked
                    expanded = f.expanduser();
                }
                return wrap(es, v.intern(expanded));
            }, "pathlib's expanduser(): a leading ~ or ~user replaced by that user's home; "
               "RuntimeError when no home is known.")
        // -- filesystem --
        .def("is_absolute", [](const pathview& v) { return v.table()->is_absolute<native_strategy>(v.row()); },
             "Whether the path is absolute.")
        .def("exists", [](const pathview& v) { return v.value().exists(); }, "Whether the path exists on disk.")
        .def("is_file", [](const pathview& v) { return v.value().is_file(); }, "Whether it is a regular file.")
        .def("is_dir", [](const pathview& v) { return v.value().is_dir(); }, "Whether it is a directory.")
        .def("is_symlink", [](const pathview& v) { return v.value().is_symlink(); }, "Whether it is a symbolic link.")
        .def("size", [](const pathview& v) { return v.value().size(); }, "File size in bytes (raises if it does not exist).")
        .def("read_bytes", [](const pathview& v) {
                const file f = v.value();
                py::object bytes;   // read into where it lies, with the GIL released: no copy
                std::size_t got = 0;
                {
                    py::gil_scoped_release nogil;
                    got = f.read_bytes_into([&bytes](std::size_t n) {
                        py::gil_scoped_acquire gil;
                        return resized_bytes(bytes, n);
                    });
                }
                resized_bytes(bytes, got);
                return bytes;
            }, "The raw file bytes, undecoded.")
        .def("read_text", [](const pathview& v, const std::string& encoding, const std::optional<std::string>& errors, py::handle newline) {
                const std::optional<std::string> nl = newline_of(newline);   // checked before the read, as open() does
                const file f = v.value();
                std::string bytes;
                {
                    py::gil_scoped_release nogil;
                    bytes = f.read_bytes();
                }
                py::object text = py::reinterpret_steal<py::object>(PyUnicode_Decode(
                    bytes.data(), static_cast<Py_ssize_t>(bytes.size()), encoding.c_str(), errors ? errors->c_str() : nullptr));
                if (!text) throw py::error_already_set();
                if (!nl && PyUnicode_FindChar(text.ptr(), '\r', 0, PyUnicode_GetLength(text.ptr()), 1) >= 0) {
                    text = replaced(replaced(text, "\r\n", "\n"), "\r", "\n");   // universal newlines
                }
                return text;
            }, py::arg("encoding") = "utf-8", py::arg("errors") = py::none(), py::arg("newline") = py::none(),
            "pathlib's read_text(): the file decoded (UTF-8 unless encoding says, whatever the locale, as Python 3.15 "
            "does), every line ending read as \\n unless newline is given.")
        .def("write_text", [](const pathview& v, py::handle data, const std::string& encoding, const std::optional<std::string>& errors,
                              py::handle newline, bool atomic) {
                if (!PyUnicode_Check(data.ptr())) throw py::type_error(std::string("data must be str, not ") + Py_TYPE(data.ptr())->tp_name);
                const std::optional<std::string> nl = newline_of(newline);
                const std::string_view ending = nl ? std::string_view(*nl) : platform::line_separator;
                py::object text = py::reinterpret_borrow<py::object>(data);
                if (!ending.empty() && ending != "\n") text = replaced(text, "\n", ending);
                py::object encoded = py::reinterpret_steal<py::object>(
                    PyUnicode_AsEncodedString(text.ptr(), encoding.c_str(), errors ? errors->c_str() : nullptr));
                if (!encoded) throw py::error_already_set();
                const std::string_view bytes(PyBytes_AS_STRING(encoded.ptr()), static_cast<std::size_t>(PyBytes_GET_SIZE(encoded.ptr())));
                const file f = v.value();
                {
                    py::gil_scoped_release nogil;   // `encoded` stays alive, and so its bytes
                    f.write_bytes(bytes, atomic ? write_mode::atomic : write_mode::in_place);
                }
                return PyUnicode_GetLength(data.ptr());   // characters, as pathlib's write_text answers
            }, py::arg("data"), py::arg("encoding") = "utf-8", py::arg("errors") = py::none(), py::arg("newline") = py::none(),
            py::kw_only(), py::arg("atomic") = false,
            "pathlib's write_text(): data encoded (UTF-8 unless encoding says), each \\n written as os.linesep unless "
            "newline is given; the characters written. atomic=True as for write().")
        .def("write_bytes", [](const pathview& v, py::bytes data, bool atomic) {
                const std::string_view sv = data;
                const file f = v.value();
                py::gil_scoped_release nogil;
                f.write_bytes(sv, atomic ? write_mode::atomic : write_mode::in_place);
            }, py::arg("data"), py::kw_only(), py::arg("atomic") = false,
            "Replace the file's contents with the given bytes; atomic=True as for write().")
        .def("mkdir", [](const pathview& v, bool parents, bool exist_ok) { v.value().mkdir(parents, exist_ok); },
             py::arg("parents") = false, py::arg("exist_ok") = false,
             "Create this directory, like pathlib: parents= creates missing ancestors, exist_ok= tolerates an existing directory.")
        .def("unlink", [](const pathview& v, bool missing_ok) { v.value().unlink(missing_ok); },
             py::arg("missing_ok") = false,
             "Remove this file or symbolic link, like pathlib: missing_ok= tolerates nothing being there.")
        .def("read", [](const pathview& v, const std::optional<std::string>& engine, py::ssize_t key_cache) {
                const file f = v.value();
                const std::size_t cap = key_cache < 0 ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(key_cache);
                return load(L{}, L::resolve(f, requested_of(engine)), f, cap);
            }, py::arg("engine") = py::none(), py::arg("key_cache") = 256, py::doc(docs.read.c_str()))
        .def("write", [](const pathview& v, py::handle obj, const std::optional<std::string>& engine, bool atomic, bool append,
                         bool parents) {
                if (append && atomic) {
                    throw py::value_error("append and atomic cannot both be asked: an append adds to the file where it is");
                }
                const file f = v.value();
                write(L{}, L::resolve(f, requested_of(engine)), f, obj, atomic ? write_mode::atomic : write_mode::in_place, append,
                      parents);
            }, py::arg("obj"), py::arg("engine") = py::none(), py::kw_only(), py::arg("atomic") = false, py::arg("append") = false,
            py::arg("parents") = false, py::doc(docs.write.c_str()))
        // -- traversal: results are rows of the same table and inherit the pin --
        .def("iterdir", [es](const pathview& v) { return wrap_all(es, v, v.value().iterdir()); },
             "The directory's children, sorted.")
        .def("glob", [es](const pathview& v, std::string_view p, std::optional<bool> case_sensitive) {
                 return wrap_all(es, v, glob_of(v.value(), glob_pattern(p, case_sensitive)));
             }, py::arg("pattern"), py::kw_only(), py::arg("case_sensitive") = py::none(),
             "What pathlib's glob finds (recurse_symlinks=False): * ? and [seq] within a name (dot names too), "
             "** across directories, yielding the one it starts in and never going through a link; a trailing "
             "'/' keeps directories; '.' is dropped, '..' a step up. case_sensitive: None as the system "
             "(any case on Windows), False any case. Sorted and deduplicated; results inherit the engine pin.")
        .def("rglob", [es](const pathview& v, std::string_view p, std::optional<bool> case_sensitive) {
                 return wrap_all(es, v, glob_of(v.value(), glob_pattern("**/" + std::string(p), case_sensitive)));
             }, py::arg("pattern"), py::kw_only(), py::arg("case_sensitive") = py::none(),
             "glob('**/' + pattern): the pattern anywhere under this directory.");

    (bind_typed_one<Es>(m, meta), ...);
    return cls;
}

}  // namespace pygim::pathlike
