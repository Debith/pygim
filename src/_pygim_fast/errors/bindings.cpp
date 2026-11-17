// errors/bindings.cpp — pygim.errors: every exception class pygim raises, made here once, so
// `except GimError` catches what any pygim module raises. Each class is a GimError and the
// Python exception its contract needs (MissingKeyError is a KeyError too), so code written
// against Python's contracts keeps working.
//
// The module imports nothing of pygim. Other extensions take the classes through the C table it
// publishes (errors/table.h, taken by errors/adapter.h), and pygim's Python imports them from here:
// every import between pygim's modules runs from Python to C++.
//
//     from pygim.errors import GimError, GimOptionError
//     GimOptionError("scoped", ["transient", "singleton"])
//     # Given input ``scoped`` not among: transient, singleton

#include "core.h"
#include "table.h"

#include <pybind11/pybind11.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace py = pybind11;

namespace {

namespace errors = pygim::errors;

/// A new class pygim.errors.<name> of `bases` (one class, or a tuple of them).
py::object new_class(const char* qualified_name, const char* doc, py::handle bases) {
    PyObject* made = PyErr_NewExceptionWithDoc(qualified_name, doc, bases.ptr(), nullptr);
    if (made == nullptr) throw py::error_already_set();
    return py::reinterpret_steal<py::object>(made);
}

/// A name, interned once.
py::str interned(const char* name) {
    PyObject* made = PyUnicode_InternFromString(name);
    if (made == nullptr) throw py::error_already_set();
    return py::reinterpret_steal<py::str>(made);
}

/// The names the classes' methods read and write, interned as the module loads.
struct names {
    py::str args = interned("args"), value = interned("value"), options = interned("options"),
            within = interned("within"), name = interned("__name__"), qualname = interned("__qualname__"),
            init = interned("__init__"), str = interned("__str__"), reduce = interned("__reduce__");
};

/// Made once and never destroyed: CPython frees the interned strings it does not keep forever as it
/// finalizes, and a static destroyed after that would release freed memory ("within" is one).
const names& name() {
    static const names* const made = new names();
    return *made;
}

/// `parts` joined by `separator`, each said by str().
py::str joined(const char* separator, py::iterable parts) {
    py::list said;
    for (const py::handle part : parts) said.append(py::str(part));
    PyObject* made = PyUnicode_Join(py::str(separator).ptr(), said.ptr());
    if (made == nullptr) throw py::error_already_set();
    return py::reinterpret_steal<py::str>(made);
}

/// `object` as str() says it, in UTF-8.
std::string said(py::handle object) { return py::str(object).cast<std::string>(); }

/// A key as a message says it: a type by its qualified name (`IService`, not
/// "<class 'app.IService'>"), anything else as str() says it.
std::string key_said(py::handle key) {
    if (PyType_Check(key.ptr())) return said(py::getattr(key, name().qualname, py::getattr(key, name().name)));
    if (PyTuple_Check(key.ptr())) {   // a key of parts, such as (interface, name): each said as a key, or by repr
        std::string parts;
        for (const py::handle part : py::reinterpret_borrow<py::tuple>(key))
            parts += (parts.empty() ? "" : ", ") + (PyType_Check(part.ptr()) ? key_said(part) : py::repr(part).cast<std::string>());
        return "(" + parts + ")";
    }
    return said(key);
}

/// Each key of `keys` as a message says it.
std::vector<std::string> keys_said(py::handle keys) {
    std::vector<std::string> made;
    for (const py::handle key : py::iter(keys)) made.push_back(key_said(key));
    return made;
}

/// Each of `parts` as str() says it, in UTF-8.
std::vector<std::string> each_said(py::handle parts) {
    std::vector<std::string> made;
    for (const py::handle part : py::iter(parts)) made.push_back(said(part));
    return made;
}

/// A message made by PyUnicode_FromFormat.
template <class... Args>
py::str formatted(const char* format, Args... args) {
    PyObject* made = PyUnicode_FromFormat(format, args...);
    if (made == nullptr) throw py::error_already_set();
    return py::reinterpret_steal<py::str>(made);
}

/// Sets `cls`'s method `dunder` to `function`. pybind11 calls its function "initialise" where the
/// method is __init__: one it names __init__ it takes for a constructor of its own classes, and
/// skips on an instance that already exists, as every exception's is when __init__ runs.
template <class Function, class... Extra>
void set_method(py::handle cls, const py::str& dunder, const char* function_name, Function&& function, const Extra&... extra) {
    py::setattr(cls, dunder, py::cpp_function(std::forward<Function>(function), py::name(function_name), py::is_method(cls), extra...));
}

/// GimOptionError(value, options): keeps both, as `value` and `options` (a tuple), and says them.
/// Its args are (value, options), so a pickled error is made again the same way.
void give_option_error_its_values(py::handle option_error) {
    set_method(option_error, name().init, "initialise",
               [](py::handle self, py::object value, py::object options) {
                   const py::tuple kept(options);
                   py::setattr(self, name().args, py::make_tuple(value, kept));
                   py::setattr(self, name().value, value);
                   py::setattr(self, name().options, kept);
               },
               py::arg("value"), py::arg("options"));
    set_method(option_error, name().str, "say", [](py::handle self) {
        return py::str(errors::option_message(said(py::getattr(self, name().value)), each_said(py::getattr(self, name().options))));
    });
}

/// MissingKeyError(key, within=None): the key, as a KeyError's args[0], and what it was looked for
/// in, as `within` — the mapping, container or registry itself, kept by reference and read only
/// when the message is: "Key ``hots`` not among: host, port" (errors/core.h). Without `within` it
/// says the key as a KeyError does. A pickled one keeps the key and drops `within`, as a pickled
/// AttributeError drops `obj`: what was looked in may be large, or not picklable.
void give_missing_key_error_its_place(py::handle missing_key) {
    set_method(missing_key, name().init, "initialise",
               [](py::handle self, py::object key, py::object within) {
                   py::setattr(self, name().args, py::make_tuple(key));
                   py::setattr(self, name().within, within);
               },
               py::arg("key"), py::arg("within") = py::none());
    set_method(missing_key, name().str, "say", [](py::handle self) -> py::str {
        const py::object within = py::getattr(self, name().within, py::none());
        if (within.is_none()) {
            PyObject* made = reinterpret_cast<PyTypeObject*>(PyExc_KeyError)->tp_str(self.ptr());
            if (made == nullptr) throw py::error_already_set();
            return py::reinterpret_steal<py::str>(made);
        }
        const std::string key = key_said(py::tuple(py::getattr(self, name().args))[0]);
        std::vector<std::string> keys;
        try {
            keys = keys_said(within);
        } catch (const py::error_already_set&) {   // the keys cannot be read: say where it looked
            return py::str(errors::missing_key_message(key, said(py::getattr(py::type::of(within), name().name))));
        }
        return py::str(errors::missing_key_message(key, keys));
    });
    set_method(missing_key, name().reduce, "reduce", [](py::handle self) {
        return py::make_tuple(py::type::of(self), py::getattr(self, name().args));
    });
}

/// The names a type check expected: a type's name, or a tuple's, None among them as "None".
py::str expected_names(py::handle expected) {
    if (!PyTuple_Check(expected.ptr())) return py::str(py::getattr(expected, name().name));
    py::list said;
    for (const py::handle each : py::reinterpret_borrow<py::tuple>(expected))
        said.append(each.is_none() ? py::str("None") : py::str(py::getattr(each, name().name)));
    return joined(", ", said);
}

/// UnrecognizedTypeError(given_type, expected_types, subject=None): a value, or a type, that is
/// none of the types expected. `expected_types` is a type, a tuple of them, or what was expected
/// said in words ("a callable"); `subject` says whose value it was, first in the message. Its args
/// are what it was given, so a pickled error is made again the same way.
void give_unrecognized_type_error_its_values(py::handle unrecognized_type_error) {
    set_method(unrecognized_type_error, name().init, "initialise",
               [](py::handle self, py::object given_type, py::object expected_types, py::object subject) {
                   const py::tuple args = subject.is_none() ? py::tuple(py::make_tuple(given_type, expected_types))
                                                            : py::tuple(py::make_tuple(given_type, expected_types, subject));
                   py::setattr(self, name().args, args);
               },
               py::arg("given_type"), py::arg("expected_types"), py::arg("subject") = py::none());
    set_method(unrecognized_type_error, name().str, "say", [](py::handle self) {
        const py::tuple args = py::getattr(self, name().args);
        const py::handle given = args[0];
        const py::handle expected = args[1];
        const py::str wanted = PyUnicode_Check(expected.ptr()) ? py::str(expected)
                                                               : formatted("to get type `%U`", expected_names(expected).ptr());
        const py::str got = PyType_Check(given.ptr())
                                ? formatted("`%U`", py::str(py::getattr(given, name().name)).ptr())
                                : formatted("`%R [%U]`", given.ptr(), py::str(py::getattr(py::type::of(given), name().name)).ptr());
        const py::str said = formatted("Expected %U, got %U", wanted.ptr(), got.ptr());
        if (args.size() < 3) return said;
        return formatted("%S: %U", args[2].ptr(), said.ptr());
    });
}

/// A class pygim.errors makes: where the table keeps it, its name, what it means, the pygim class
/// it derives from (none for the root) and the Python exception its meaning needs (none: pygim's
/// alone).
struct made_class {
    PyObject* errors::table::*slot;
    const char* name;
    const char* doc;
    PyObject* errors::table::*pygim_base;
    PyObject* builtin;
};

/// Every class, in the table's order, which is also the module's __all__ order. Made at run time:
/// the interpreter's own exceptions (PyExc_*) are not constants.
std::vector<made_class> every_class() {
    using T = errors::table;
    return {
        {&T::gim_exception, "GimException", "The root of every exception pygim raises; GimError is the one to catch.",
         nullptr, PyExc_Exception},
        {&T::gim_error, "GimError", "Everything pygim raises is a GimError: `except GimError` catches it all.",
         &T::gim_exception, nullptr},
        {&T::missing_key_error, "MissingKeyError",
         "MissingKeyError(key, within=None): a lookup found nothing; `within` is where it looked. A KeyError too,\n"
         "as a mapping's lookup raises.",
         &T::gim_error, PyExc_KeyError},
        {&T::option_error, "GimOptionError",
         "GimOptionError(value, options): a value none of its options. A ValueError too, as Python raises\n"
         "for a bad value; `value` and `options` say which.",
         &T::gim_error, PyExc_ValueError},
        {&T::dispatch_error, "DispatchError", "A call could not be dispatched.", &T::gim_error, nullptr},
        {&T::unrecognized_type_error, "UnrecognizedTypeError",
         "UnrecognizedTypeError(given_type, expected_types, subject=None): a value, or a type, that is none of\n"
         "the types expected. A TypeError too, as Python raises for a value of the wrong type.",
         &T::gim_error, PyExc_TypeError},
        {&T::argument_error, "ArgumentError",
         "A call pygim cannot take as given: an unknown keyword, an argument missing or too many, two that\n"
         "exclude each other. A TypeError too, as Python raises for a call that does not fit.",
         &T::gim_error, PyExc_TypeError},
        {&T::signature_error, "SignatureError",
         "A callable whose signature pygim cannot use: a positional-only parameter, an annotation missing,\n"
         "type hints that do not resolve. A TypeError too.",
         &T::gim_error, PyExc_TypeError},
        {&T::invalid_value_error, "InvalidValueError",
         "A value of the right type, outside what is accepted: a count below one, an empty name. A\n"
         "ValueError too.",
         &T::gim_error, PyExc_ValueError},
        {&T::parse_error, "ParseError",
         "Text that is not its format, or bytes that are not UTF-8; the message names the file and line.\n"
         "A ValueError too, as json.JSONDecodeError is.",
         &T::gim_error, PyExc_ValueError},
        {&T::invalid_state_error, "InvalidStateError",
         "An operation the object's state does not allow: a lap on a finished timer. A RuntimeError too.",
         &T::gim_error, PyExc_RuntimeError},
        {&T::duplicate_key_error, "DuplicateKeyError",
         "A key registered twice without override. A ValueError, not a KeyError: Mapping.get() and `in`\n"
         "catch a KeyError, and must not swallow this.",
         &T::gim_error, PyExc_ValueError},
        {&T::circular_dependency_error, "CircularDependencyError",
         "Wiring that depends on itself. A ValueError too, as graphlib.CycleError is.",
         &T::gim_error, PyExc_ValueError},
        {&T::unsupported_column_type_error, "UnsupportedColumnTypeError",
         "A column whose Arrow or SQL type pygim cannot map. A TypeError too.", &T::gim_error, PyExc_TypeError},
        {&T::backend_error, "BackendError",
         "A library or driver pygim calls (ODBC, BCP, Arrow) reported a failure; the message carries what\n"
         "it said. A RuntimeError too.",
         &T::gim_error, PyExc_RuntimeError},
        {&T::os_error, "GimOSError",
         "GimOSError(errno, strerror, filename): the system refused a file operation, as pathlib raises it.\n"
         "An OSError too; the errnos Python names have a class of their own below it.",
         &T::gim_error, PyExc_OSError},
        {&T::file_not_found_error, "GimFileNotFoundError", "No such file or directory. A FileNotFoundError too.",
         &T::os_error, PyExc_FileNotFoundError},
        {&T::file_exists_error, "GimFileExistsError", "The file or directory exists already. A FileExistsError too.",
         &T::os_error, PyExc_FileExistsError},
        {&T::not_a_directory_error, "GimNotADirectoryError", "Not a directory. A NotADirectoryError too.",
         &T::os_error, PyExc_NotADirectoryError},
        {&T::permission_error, "GimPermissionError", "The system refused access. A PermissionError too.",
         &T::os_error, PyExc_PermissionError},
        {&T::overflow_error, "GimOverflowError", "A quantity past what its type holds. An OverflowError too.",
         &T::gim_error, PyExc_OverflowError},
        {&T::index_out_of_range_error, "IndexOutOfRangeError",
         "A position past the end. An IndexError too, which iteration by index stops at.", &T::gim_error,
         PyExc_IndexError},
        {&T::missing_attribute_error, "MissingAttributeError",
         "An attribute that is not there. An AttributeError too, which getattr(obj, name, default) and\n"
         "hasattr catch.",
         &T::gim_error, PyExc_AttributeError},
    };
}

/// The classes, made on the module's first load and kept for the process's life; a second load
/// (a reload, another interpreter's import) hands out the same classes, so there is only ever one
/// GimError.
errors::table& the_classes() {
    static errors::table made{};
    if (made.version != 0) return made;
    const std::vector<made_class> classes = every_class();
    constexpr std::size_t slots = (sizeof(errors::table) - offsetof(errors::table, gim_exception)) / sizeof(PyObject*);
    if (classes.size() != slots)   // a class the table keeps and the list forgot would load as null
        throw py::import_error("pygim.errors lists " + std::to_string(classes.size()) + " classes; its table keeps " +
                               std::to_string(slots));
    errors::table building{};
    for (const made_class& each : classes) {
        const std::string qualified = std::string(errors::module_name) + "." + each.name;
        const py::handle pygim_base = each.pygim_base ? py::handle(building.*each.pygim_base) : py::handle();
        const py::object bases = !pygim_base     ? py::reinterpret_borrow<py::object>(each.builtin)
                                 : !each.builtin ? py::reinterpret_borrow<py::object>(pygim_base)
                                                 : py::object(py::make_tuple(pygim_base, py::handle(each.builtin)));
        building.*each.slot = new_class(qualified.c_str(), each.doc, bases).release().ptr();
    }
    give_missing_key_error_its_place(building.missing_key_error);
    give_option_error_its_values(building.option_error);
    give_unrecognized_type_error_its_values(building.unrecognized_type_error);
    building.version = errors::table_version;
    made = building;
    return made;
}

}  // namespace

PYBIND11_MODULE(errors, m) {
    m.doc() = "Every exception pygim raises, each a GimError and the Python exception its contract needs.";
    errors::table& classes = the_classes();
    py::list names;
    for (const made_class& each : every_class()) {
        m.attr(each.name) = py::handle(classes.*each.slot);
        names.append(each.name);
    }
    m.attr("__all__") = py::tuple(names);
    m.attr("_C_API") = py::capsule(&classes, errors::table_name);
}
