#pragma once
// code/adapter/corpus_bindings.h — pygim.code's parse: one call starts a parse, and views show
// its result. Registered by register_corpus(m) from code/bindings.cpp.
//
//     corpus = code.parse("Lib", "**/*.py")      # walk, read, parse: C++, the GIL released, Ctrl-C owned
//     tree = corpus["Lib/ast.py"]                # a view: no Python object per node
//     tree.root.kind, tree.root.children[0].field("names")
//     tree.dump(include_attributes=True)         # == ast.dump(ast.parse(...), include_attributes=True)
//
// Translation only (#270): the corpus is built in the core (../corpus.h); here a Python call
// starts it and views read it. A view holds the corpus alive and an index; a Python value is made
// only when one is asked for — a constant's value, a name, a list of children — through the C API.
//
// No to_ast(): dump() is the completeness check (Q38, D40). Not here yet: refuse_uninitialised(m)
// (#150), which comes with utils/initialised.h when this branch meets the one that has it.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../pathlike/adapter/path_text.h"
#include "../../utils/interrupted.h"
#include "../corpus.h"
#include "../python/dump.h"
#include "../python/literals.h"

namespace py = pybind11;

namespace pygim::code {
namespace detail {

using python::constant_kind;
using python::field_info;
using python::kind;
using python::node;
using python::shape;
using python::storage;
using python::syntax_tree;

using corpus_ptr = std::shared_ptr<corpus>;   // pybind11 holds no shared_ptr<const T>; the views only read it

// ── values through the C API ────────────────────────────────────────────────
[[nodiscard]] inline py::object steal(PyObject* o) {
    if (o == nullptr) throw py::error_already_set();
    return py::reinterpret_steal<py::object>(o);
}
[[nodiscard]] inline py::str str_of(std::string_view wtf8) {   // a str's value, lone surrogates included
    return py::reinterpret_steal<py::str>(steal(PyUnicode_DecodeUTF8(wtf8.data(), static_cast<Py_ssize_t>(wtf8.size()), "surrogatepass")).release());
}
[[nodiscard]] inline double double_of(const std::string& digits) {
    const double v = PyOS_string_to_double(digits.c_str(), nullptr, nullptr);
    if (v == -1.0 && PyErr_Occurred()) throw py::error_already_set();
    return v;
}

/// The character a \N{NAME} escape names, from CPython's own table (Q37: none in C++ yet).
/// CPython's decoder loads unicodedata's name table (its _ucnhash_CAPI capsule) the first time it
/// meets a \N: the one module the code component makes Python import, inside CPython's own C API,
/// accepted until the names are generated into C++ (D42), not an import of ours (#248).
[[nodiscard]] inline std::optional<std::uint32_t> character_named(std::string_view name) {
    const std::string escape = "\\N{" + std::string(name) + "}";
    PyObject* s = PyUnicode_DecodeUnicodeEscape(escape.data(), static_cast<Py_ssize_t>(escape.size()), nullptr);
    if (s == nullptr) {
        PyErr_Clear();
        return std::nullopt;
    }
    const py::object held = steal(s);
    if (PyUnicode_GetLength(s) != 1) return std::nullopt;
    return static_cast<std::uint32_t>(PyUnicode_ReadChar(s, 0));
}

/// repr() as CPython writes it: what ast.dump writes for a constant. Where plain_repr writes the
/// same bytes (ASCII text, any bytes, a decimal integer) it writes them, with no Python object.
class python_repr {
public:
    void str(std::string_view s, std::string& out) const {
        if (python::plain_repr::exact_str(s)) return python::plain_repr{}.str(s, out);
        append_repr(str_of(s), out);
    }
    void bytes(std::string_view s, std::string& out) const { python::plain_repr{}.bytes(s, out); }
    void integer(std::string_view s, std::string& out) const {
        if (python::plain_repr::exact_integer(s)) return python::plain_repr{}.integer(s, out);
        append_repr(steal(PyLong_FromString(std::string(s).c_str(), nullptr, 0)), out);
    }
    void floating(std::string_view s, std::string& out) const { append_repr(steal(PyFloat_FromDouble(double_of(std::string(s)))), out); }
    void imaginary(std::string_view s, std::string& out) const { append_repr(steal(PyComplex_FromDoubles(0.0, double_of(std::string(s)))), out); }
    [[nodiscard]] std::optional<std::uint32_t> character(std::string_view name) const { return character_named(name); }

private:
    static void append_repr(const py::handle& v, std::string& out) {
        const py::object r = steal(PyObject_Repr(v.ptr()));
        Py_ssize_t n = 0;
        const char* utf8 = PyUnicode_AsUTF8AndSize(r.ptr(), &n);
        if (utf8 == nullptr) throw py::error_already_set();
        out.append(utf8, static_cast<std::size_t>(n));
    }
};
static_assert(python::constant_repr<python_repr>);

// A Constant node's value as a Python object.
[[nodiscard]] inline py::object constant_value(const syntax_tree& t, std::uint32_t i) {
    const node& n = t[i];
    switch (python::constant_of(n.small())) {
        case constant_kind::none: return py::none();
        case constant_kind::true_: return py::bool_(true);
        case constant_kind::false_: return py::bool_(false);
        case constant_kind::ellipsis: return py::reinterpret_borrow<py::object>(Py_Ellipsis);
        case constant_kind::integer: return steal(PyLong_FromString(python::number_text(t, i).c_str(), nullptr, 0));
        case constant_kind::floating: return steal(PyFloat_FromDouble(double_of(python::number_text(t, i))));
        case constant_kind::imaginary: {
            std::string digits = python::number_text(t, i);
            digits.pop_back();
            return steal(PyComplex_FromDoubles(0.0, double_of(digits)));
        }
        case constant_kind::string:
        case constant_kind::bytes: {
            std::string s;
            python::decode_text(t, n.value(), s, character_named);
            if (python::constant_of(n.small()) == constant_kind::bytes) return steal(PyBytes_FromStringAndSize(s.data(), static_cast<Py_ssize_t>(s.size())));
            return str_of(s);
        }
    }
    return py::none();
}

// ── views ───────────────────────────────────────────────────────────────────
class tree_view;

/// One node of a parsed file.
class node_view {
public:
    node_view(corpus_ptr c, std::size_t file, std::uint32_t index) : m_corpus(std::move(c)), m_file(file), m_index(index) {}

    [[nodiscard]] const syntax_tree& tree() const { return (*m_corpus)[m_file].tree(); }
    [[nodiscard]] const node& row() const { return tree()[m_index]; }
    [[nodiscard]] std::uint32_t index() const noexcept { return m_index; }
    [[nodiscard]] std::string_view kind_name() const { return python::info(row().kind()).name(); }

    [[nodiscard]] std::optional<node_view> parent() const {
        if (row().parent() == python::no_value) return std::nullopt;
        return node_view(m_corpus, m_file, row().parent());
    }
    [[nodiscard]] std::vector<node_view> children() const {
        std::vector<node_view> out;
        for (std::uint32_t c : tree().children(m_index)) {
            if (python::is_node(tree()[c].kind())) out.emplace_back(m_corpus, m_file, c);   // the rest are fields' values
        }
        return out;
    }
    [[nodiscard]] std::vector<std::string_view> field_names() const {
        std::vector<std::string_view> out;
        for (const field_info& f : python::info(row().kind()).fields()) out.push_back(f.name());
        return out;
    }

    /// The value of a field, as ast would hold it: a node, a list, a name, a number, None.
    [[nodiscard]] py::object field(std::string_view name) const {
        const auto& fields = python::info(row().kind()).fields();
        for (std::size_t s = 0; s < fields.size(); ++s) {
            if (fields[s].name() == name) return value_of(static_cast<std::uint8_t>(s), fields[s]);
        }
        throw py::attribute_error("'" + std::string(kind_name()) + "' has no field '" + std::string(name) + "'");
    }

    /// A field's value, or a position by the name ast gives it (position_names).
    [[nodiscard]] py::object named(std::string_view name) const {
        if (name == "lineno") return py::cast(lineno());
        if (name == "col_offset") return py::cast(col_offset());
        if (name == "end_lineno") return py::cast(end_lineno());
        if (name == "end_col_offset") return py::cast(end_col_offset());
        return field(name);
    }

    /// Line and column of the start and end, or None for a kind without a position.
    [[nodiscard]] std::optional<std::uint32_t> lineno() const { return at(row().start(), true); }
    [[nodiscard]] std::optional<std::uint32_t> col_offset() const { return at(row().start(), false); }
    [[nodiscard]] std::optional<std::uint32_t> end_lineno() const { return at(row().end(), true); }
    [[nodiscard]] std::optional<std::uint32_t> end_col_offset() const { return at(row().end(), false); }
    /// The source the node spans.
    [[nodiscard]] std::string_view source() const { return tree().text().substr(row().start(), row().end() - row().start()); }

    [[nodiscard]] std::string dump(bool include_attributes) const {
        return python::dump(tree(), m_corpus->names(), python_repr{}, m_index, include_attributes);
    }
    [[nodiscard]] std::string repr() const {
        std::string out = "<" + std::string(kind_name());
        if (const auto l = lineno()) out += " at " + std::to_string(*l) + ":" + std::to_string(*col_offset());
        return out + ">";
    }
    [[nodiscard]] bool same_as(const node_view& o) const noexcept { return m_corpus == o.m_corpus && m_file == o.m_file && m_index == o.m_index; }
    [[nodiscard]] std::size_t hash() const noexcept { return std::hash<const void*>{}(m_corpus.get()) ^ (m_file * 1000003u) ^ m_index; }

private:
    [[nodiscard]] std::optional<std::uint32_t> at(std::uint32_t offset, bool line) const {
        if (!python::info(row().kind()).positioned()) return std::nullopt;
        const python::position p = tree().position_of(offset);
        return line ? p.line() : p.column();
    }
    [[nodiscard]] py::object name(std::uint32_t id) const { return str_of(m_corpus->names()[id]); }

    [[nodiscard]] py::object value_of(std::uint8_t s, const field_info& f) const {
        const node& n = row();
        switch (f.storage()) {
            case storage::never: return py::none();
            case storage::child: {
                py::list items;
                for (std::uint32_t c : tree().children(m_index)) {
                    if (tree()[c].field() != s) continue;
                    if (tree()[c].kind() == kind::absent) items.append(py::none());
                    else items.append(py::cast(node_view(m_corpus, m_file, c)));
                }
                if (f.shape() == shape::nodes) return std::move(items);
                return items.empty() ? py::none() : py::object(items[0]);
            }
            case storage::value:
                if (f.shape() == shape::constant) {
                    if (n.kind() == kind::Interpolation) {
                        std::string text;
                        python::decode_text(tree(), n.value(), text);
                        return str_of(text);
                    }
                    return constant_value(tree(), m_index);
                }
                return n.value() == python::no_value ? py::none() : name(n.value());
            case storage::small:
                switch (python::meaning_of(n.kind(), f)) {
                    case python::small_meaning::string_prefix: return python::has_unicode_prefix(n.small()) ? py::object(py::str("u")) : py::none();
                    case python::small_meaning::member: return py::str(std::string(python::info(python::member_of(f, n.small())).name()));
                    case python::small_meaning::singleton: {
                        const python::constant_kind c = python::constant_of(n.small());
                        return c == python::constant_kind::none ? py::none() : py::object(py::bool_(c == python::constant_kind::true_));
                    }
                    case python::small_meaning::conversion: return py::int_(python::conversion_of(n.small()));
                    case python::small_meaning::number: return py::int_(n.small());
                }
                return py::none();   // every meaning is answered above
            case storage::leaf: {
                py::list items;
                for (std::uint32_t c : tree().children(m_index)) {
                    if (tree()[c].field() != s) continue;
                    if (f.shape() == shape::operators) items.append(py::str(std::string(python::info(python::member_of(f, tree()[c].small())).name())));
                    else items.append(name(tree()[c].value()));
                }
                if (f.shape() == shape::optional_identifier) return items.empty() ? py::none() : py::object(items[0]);
                return std::move(items);
            }
        }
        return py::none();
    }

    corpus_ptr m_corpus;
    std::size_t m_file;
    std::uint32_t m_index;
};

// ── an expected node (D94) ──────────────────────────────────────────────────
/// A node's positions, by the names ast gives them: what named() answers beside the fields.
inline constexpr std::array<std::string_view, 4> position_names{"lineno", "col_offset", "end_lineno", "end_col_offset"};

/// What a test expects a node to be: its kind, and the fields and positions the test names, each a
/// value as field() returns it — a str, a number, None, a list, or an expected node for a node. A
/// field not named is not compared, so `node == code.like("ClassDef", name="Point")` asks only that.
class expected_node {
public:
    /// ValueError for a kind no node has, a field the kind does not have, or a position on a kind
    /// with none: an expectation no node could meet is refused when it is made.
    expected_node(std::string_view kind_name, const py::kwargs& fields) : m_kind(kind_named(kind_name)) {
        for (const auto& [key, value] : fields) {
            std::string name = key.cast<std::string>();
            check_named(name);
            m_fields.emplace_back(std::move(name), py::reinterpret_borrow<py::object>(value));
        }
    }

    [[nodiscard]] bool matches(const node_view& n) const {
        if (n.row().kind() != m_kind) return false;
        for (const auto& [name, expected] : m_fields) {
            if (!same(n.named(name), expected)) return false;
        }
        return true;
    }

    /// As it is written: like('Name', id='x').
    [[nodiscard]] std::string repr() const {
        std::string out = "like(" + py::repr(py::str(std::string(python::info(m_kind).name()))).cast<std::string>();
        for (const auto& [name, expected] : m_fields) out += ", " + name + "=" + py::repr(expected).cast<std::string>();
        return out + ")";
    }

private:
    [[nodiscard]] static kind kind_named(std::string_view name) {
        const std::optional<kind> k = python::kind_named(name);
        if (!k) throw py::value_error("no node kind is named '" + std::string(name) + "'");
        return *k;
    }

    void check_named(std::string_view name) const {
        const auto& about = python::info(m_kind);
        if (std::ranges::find(position_names, name) != position_names.end()) {
            if (!about.positioned()) throw py::value_error("'" + std::string(about.name()) + "' has no position");
            return;
        }
        for (const field_info& f : about.fields()) {
            if (f.name() == name) return;
        }
        throw py::value_error("'" + std::string(about.name()) + "' has no field '" + std::string(name) + "'");
    }

    /// An expected node matches a node; a list or tuple, a list of the same length item by item;
    /// any other value is ==, as field() returns it.
    [[nodiscard]] static bool same(const py::handle& actual, const py::handle& expected) {
        if (py::isinstance<expected_node>(expected)) {
            return py::isinstance<node_view>(actual) && expected.cast<const expected_node&>().matches(actual.cast<const node_view&>());
        }
        if (PyList_Check(expected.ptr()) || PyTuple_Check(expected.ptr())) {
            if (!PyList_Check(actual.ptr())) return false;
            const py::sequence want = py::reinterpret_borrow<py::sequence>(expected);
            const py::list got = py::reinterpret_borrow<py::list>(actual);
            if (got.size() != want.size()) return false;
            for (std::size_t i = 0; i < got.size(); ++i) {
                if (!same(got[i], want[i])) return false;
            }
            return true;
        }
        return actual.equal(expected);
    }

    kind m_kind;
    std::vector<std::pair<std::string, py::object>> m_fields;   // in the order given, for repr
};

/// One parsed file.
class tree_view {
public:
    tree_view(corpus_ptr c, std::size_t file) : m_corpus(std::move(c)), m_file(file) {}
    [[nodiscard]] const syntax_tree& tree() const { return (*m_corpus)[m_file].tree(); }
    [[nodiscard]] const std::string& path() const { return (*m_corpus)[m_file].path(); }
    [[nodiscard]] std::size_t size() const { return tree().size(); }
    [[nodiscard]] node_view root() const { return node_view(m_corpus, m_file, 0); }
    [[nodiscard]] node_view at(std::uint32_t i) const {
        if (i >= tree().size()) throw py::index_error("node index out of range");
        return node_view(m_corpus, m_file, i);
    }
    [[nodiscard]] std::string dump(bool include_attributes) const { return root().dump(include_attributes); }
    [[nodiscard]] py::dict kind_counts() const {
        std::array<std::size_t, python::kind_count> counts{};
        for (const node& n : tree().nodes()) ++counts[static_cast<std::size_t>(n.kind())];
        py::dict out;
        for (std::size_t k = 0; k < counts.size(); ++k) {
            if (counts[k] && k < static_cast<std::size_t>(kind::identifier)) out[py::str(std::string(python::kinds[k].name()))] = counts[k];
        }
        return out;
    }
    /// Every node of the kind ast calls `name`, in source order; a ValueError for a name no node
    /// kind has.
    [[nodiscard]] std::vector<node_view> find(std::string_view name) const {
        const std::optional<kind> k = python::kind_named(name);
        if (!k) throw py::value_error("no node kind is named '" + std::string(name) + "'");
        std::vector<node_view> out;
        for (std::uint32_t i = 0; i < tree().size(); ++i) {
            if (tree()[i].kind() == *k) out.emplace_back(m_corpus, m_file, i);
        }
        return out;
    }

private:
    corpus_ptr m_corpus;
    std::size_t m_file;
};

// ── errors ──────────────────────────────────────────────────────────────────
// A parse_failure's line and column (from 0) as compile() would tell them: None for both on no
// line, 0 and -2 before the text (offset -1, from 1), 1 and -1 on the first line with no column.
[[nodiscard]] inline std::pair<py::object, py::object> line_and_column(const parse_failure& e) {
    if (e.at()) return {py::int_(e.at()->line()), py::int_(e.at()->column())};
    if (e.place() == python::error_place::before_text) return {py::int_(0), py::int_(-2)};
    if (e.place() == python::error_place::first_line) return {py::int_(1), py::int_(-1)};
    return {py::none(), py::none()};
}

// A file's parse_failure as the SyntaxError compile() raises: (msg, (filename, lineno, offset, text)).
[[nodiscard]] inline py::object syntax_error(const corpus_file& f) {
    const parse_failure& e = f.error();
    const auto [line, column] = line_and_column(e);
    const py::object offset = column.is_none() ? py::object(py::none()) : py::object(py::int_(column.cast<long long>() + 1));
    const py::tuple details = py::make_tuple(pathlike::str_from_text(f.path()), line, offset, py::none());
    return py::handle(PyExc_SyntaxError)(e.message(), details);
}

[[noreturn]] inline void raise_syntax_error(const corpus_file& f) {
    PyErr_SetObject(PyExc_SyntaxError, syntax_error(f).ptr());
    throw py::error_already_set();
}

// The corpus of every file under `root` matching `pattern`: built with no Python, the GIL
// released, Ctrl-C and SIGTERM owned for the run (#272).
[[nodiscard]] inline corpus_ptr parse_tree(const py::handle& root, std::string_view pattern, std::optional<std::size_t> threads) {
    const std::string where = pathlike::text_from_arg(root);   // as the OS takes it: a POSIX name need not be UTF-8
    pathlike::glob_pattern parsed(pattern);   // a bad pattern is a ValueError before any thread starts
    parallel::chosen_width width = parallel::width_for<parallel::one_per_core>(threads, "parse");   // parsing computes (Q39)
    // No Python from the run's start to its collect. What the threads share is the run's own: its
    // free workspaces (m_free, m_workspaces under m_lock), the pool's queue under its lock, the
    // process's parked_workers (claimed by m_busy) and the interrupt scope, which this thread waits
    // on and the last task's finished() or a signal wakes. Nothing points into the caller's
    // objects: `where` and the parsed pattern are copies.
    return run_interruptible(
        "code.parse", [&] { return corpus_run(pathlike::file(where).to_fs(), std::move(parsed), std::move(width)); },   // UTF-8 on Windows too
        [](corpus_run& run) { return std::make_shared<corpus>(run.collect()); });
}

// One source text as a corpus of one file.
[[nodiscard]] inline corpus_ptr parse_text(arena_string bytes, std::string path, bool decoded) {
    names_table names;
    python::parse_output scratch;
    std::vector<corpus_file> files;
    files.push_back(parse_one(std::move(path), std::move(bytes), names, scratch, decoded));
    const corpus_stats stats(files, names.size(), 0, duration(0));   // no worker: the caller's thread
    return std::make_shared<corpus>(std::move(files), std::move(names), stats);
}

[[nodiscard]] inline std::size_t file_index(const corpus& c, const py::handle& key) {
    if (PyLong_Check(key.ptr())) {
        auto i = key.cast<std::ptrdiff_t>();
        if (i < 0) i += static_cast<std::ptrdiff_t>(c.size());
        if (i < 0 || static_cast<std::size_t>(i) >= c.size()) throw py::index_error("corpus index out of range");
        return static_cast<std::size_t>(i);
    }
    const std::size_t i = c.find(pathlike::text_from_arg(key));
    if (i == corpus::npos) throw py::key_error(py::str(key));
    return i;
}

// pygim.pathlike, looked up once, at its first use: one file is handed over as a path and many as
// a PathSet, as pathlike holds them. pygim.pathlike is loaded by then (pygim imports it first).
[[nodiscard]] inline py::module_& pathlike_module() {
    PYBIND11_CONSTINIT static py::gil_safe_call_once_and_store<py::module_> found;
    return found.call_once_and_store_result([] { return py::module_::import("pygim.pathlike"); }).get_stored();
}

}  // namespace detail

/// Corpus, Tree, Node, parse() and parse_source() on `m`.
inline void register_corpus(py::module_& m) {
    using namespace detail;



    py::class_<node_view> node_class(m, "Node", "One node of a syntax tree: its kind, its fields, its position. A view; it keeps its corpus alive.");
    py::class_<expected_node> like_class(m, "like",
        "A node a test expects: its kind, and only the fields and positions named, each a value as\n"
        "field() returns it (a like for a node, a list for a list): node == like('ClassDef', name='Point').\n"
        "ValueError for a kind no node has, a field the kind does not have, a position on a kind with none.");
    like_class
        .def(py::init([](const py::args& kind, const py::kwargs& fields) {   // *args: a field may be named kind (Constant's)
                 if (kind.size() != 1 || !py::isinstance<py::str>(kind[0]))
                     throw py::type_error("like() takes a node's kind, then its fields by name: like('Name', id='x')");
                 return expected_node(kind[0].cast<std::string>(), fields);
             }))
        .def("__eq__", [](const expected_node& e, const py::handle& other) -> py::object {
                 if (!py::isinstance<node_view>(other)) return py::reinterpret_borrow<py::object>(Py_NotImplemented);
                 return py::bool_(e.matches(other.cast<const node_view&>()));
             }, py::is_operator(), py::arg("other"), "Whether a node is of the kind and holds each value named.")
        .def("__repr__", &expected_node::repr);
    node_class
        .def_property_readonly("kind", [](const node_view& n) { return std::string(n.kind_name()); }, "The node's kind, as ast names it: 'FunctionDef', 'Name', ...")
        .def_property_readonly("index", &node_view::index, "The node's place in its tree, in source order (preorder).")
        .def_property_readonly("parent", &node_view::parent, "The node this one is a field of; None for the module.")
        .def_property_readonly("children", &node_view::children, "The nodes under this one, in source order.")
        .def_property_readonly("fields", &node_view::field_names, "The kind's fields, as ast's _fields orders them.")
        .def("field", &node_view::field, py::arg("name"), "A field's value as ast holds it: a Node, a list, a str, a number, or None.")
        .def_property_readonly("lineno", &node_view::lineno, "The line the node starts on, from 1; None for a kind ast gives no position (Module, arguments).")
        .def_property_readonly("col_offset", &node_view::col_offset, "Bytes from the start of the line, as ast counts them; None as lineno.")
        .def_property_readonly("end_lineno", &node_view::end_lineno, "The line the node ends on; None as lineno.")
        .def_property_readonly("end_col_offset", &node_view::end_col_offset, "Bytes from the start of the end line to the node's end; None as lineno.")
        .def_property_readonly("source", [](const node_view& n) { return std::string(n.source()); }, "The source text the node spans.")
        .def("dump", &node_view::dump, py::kw_only(), py::arg("include_attributes") = false, "The node as ast.dump writes it.")
        .def("__repr__", &node_view::repr)
        .def("__eq__", [](const node_view& a, const py::handle& other) -> py::object {
                 if (py::isinstance<node_view>(other)) return py::bool_(a.same_as(other.cast<const node_view&>()));
                 if (py::isinstance<expected_node>(other)) return py::bool_(other.cast<const expected_node&>().matches(a));
                 return py::reinterpret_borrow<py::object>(Py_NotImplemented);
             }, py::is_operator(), py::arg("other"),
             "The same node of the same tree; or, against a like(...), of its kind and holding each value it names.")
        .def("__hash__", &node_view::hash);

    py::class_<tree_view>(m, "Tree", "One parsed file. A view; it keeps its corpus alive.")
        .def_property_readonly("path", [](const tree_view& t) { return pathlike_module().attr("path")(pathlike::str_from_text(t.path())); },
                               "The file, as a pygim path; parse_source()'s path argument for one source.")
        .def_property_readonly("root", &tree_view::root, "The Module node.")
        .def("__len__", &tree_view::size, "How many rows the tree has, node() reads: its nodes, and the field values kept\n"
             "as rows of their own ('identifier' in a list of names, 'cmpop', 'absent' for a missing value).")
        .def("node", &tree_view::at, py::arg("index"), "The row at an index (preorder: source order), from 0 to len(tree).")
        .def("find", &tree_view::find, py::arg("kind"), "Every node of a kind (as ast names it: 'Name'), in source order; ValueError for a name no kind has.")
        .def("kind_counts", &tree_view::kind_counts, "How many nodes of each kind.")
        .def("dump", &tree_view::dump, py::kw_only(), py::arg("include_attributes") = false,
             "The tree as ast.dump(ast.parse(source)) writes it, include_attributes as there.")
        .def("__repr__", [](const tree_view& t) { return py::str("<Tree {}: {} nodes>").format(pathlike::str_from_text(t.path()), t.size()); });

    py::class_<corpus, corpus_ptr>(m, "Corpus", "Parsed files, sorted by path, sharing one table of names.")
        .def("__len__", &corpus::size, "How many files, parsed or not.")
        .def("__getitem__", [](const corpus_ptr& c, const py::handle& key) {
                 const std::size_t i = file_index(*c, key);
                 if (!(*c)[i].parsed()) raise_syntax_error((*c)[i]);
                 return tree_view(c, i);
             }, py::arg("file"), "A file's tree, by index or path; a file that is not Python raises its SyntaxError.")
        .def("__iter__", [](const corpus_ptr& c) {
                 std::vector<tree_view> trees;
                 for (std::size_t i = 0; i < c->size(); ++i) if ((*c)[i].parsed()) trees.emplace_back(c, i);
                 return py::iter(py::cast(trees));
             }, "The trees of the files that parsed, in path order.")
        .def_property_readonly("files", [](const corpus& c) {
                 py::list out;
                 for (const corpus_file& f : c.files()) out.append(pathlike::str_from_text(f.path()));
                 return pathlike_module().attr("PathSet")(out);
             }, "Every file, parsed or not, as a PathSet in path order (corpus[i] is files[i]'s tree).")
        .def_property_readonly("errors", [](const corpus& c) {
                 py::list out;
                 for (const corpus_file& f : c.files()) if (!f.parsed()) out.append(syntax_error(f));
                 return out;
             }, "Each file that is not Python, in path order, as the SyntaxError corpus[path] raises for it.")
        .def("stats", [](const corpus& c) {
                 const corpus_stats& s = c.stats();
                 py::dict d;
                 d["files"] = s.files();
                 d["failed"] = s.failed();
                 d["bytes"] = s.bytes();
                 d["nodes"] = s.nodes();
                 d["names"] = s.names();
                 d["workers"] = s.workers();
                 d["seconds"] = static_cast<double>(s.took().count()) / 1e9;
                 return d;
             }, "What the run did: files, failed, bytes, nodes, names, workers, seconds.")
        .def("__repr__", [](const corpus& c) {
            return "<Corpus: " + std::to_string(c.size()) + " files, " + std::to_string(c.stats().failed()) + " not Python, " + std::to_string(c.stats().nodes()) + " nodes>";
        });

    m.def("parse", &parse_tree, py::arg("root"), py::arg("pattern") = "**/*.py", py::kw_only(), py::arg("threads") = py::none(),
          "Every file under root matching pattern, parsed as Python 3.14: one run in C++ that walks,\n"
          "reads and parses with the GIL released. threads: how many work at once; None, one per\n"
          "core. Ctrl-C stops the run and raises KeyboardInterrupt.");
    m.def("parse_source", [](const py::handle& source, const py::handle& path) {
              arena_string bytes;   // on the heap: a tree parsed alone
              const bool decoded = !PyBytes_Check(source.ptr());
              if (!decoded) {
                  bytes.assign(PyBytes_AS_STRING(source.ptr()), static_cast<std::size_t>(PyBytes_GET_SIZE(source.ptr())));
              } else {
                  Py_ssize_t n = 0;
                  const char* utf8 = PyUnicode_AsUTF8AndSize(source.ptr(), &n);
                  if (utf8 == nullptr) throw py::error_already_set();
                  bytes.assign(utf8, static_cast<std::size_t>(n));
              }
              std::string where = pathlike::text_from_arg(path);   // as the corpus holds a file's: the OS's text
              corpus_ptr c;
              {
                  py::gil_scoped_release nogil;   // parse_text touches its arguments and locals only
                  c = parse_text(std::move(bytes), std::move(where), decoded);
              }
              if (!(*c)[0].parsed()) raise_syntax_error((*c)[0]);
              return tree_view(c, 0);
          }, py::arg("source"), py::arg("path") = "<string>",
          "One source (str, or bytes read from a file) parsed as Python 3.14: its Tree, or SyntaxError.\n"
          "path: the file it came from (str or any path), named by the tree and its SyntaxError.");
}

}  // namespace pygim::code
