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

#include "table.h"

#include <pybind11/pybind11.h>

#include <utility>

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
            name = interned("__name__"), init = interned("__init__"), str = interned("__str__");
};

const names& name() {
    static const names made;
    return made;
}

/// `parts` joined by `separator`, each said by str().
py::str joined(const char* separator, py::iterable parts) {
    py::list said;
    for (const py::handle part : parts) said.append(py::str(part));
    PyObject* made = PyUnicode_Join(py::str(separator).ptr(), said.ptr());
    if (made == nullptr) throw py::error_already_set();
    return py::reinterpret_steal<py::str>(made);
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
        const py::object value = py::getattr(self, name().value);
        return formatted("Given input ``%S`` not among: %U", value.ptr(), joined(", ", py::getattr(self, name().options)).ptr());
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

/// UnrecognizedTypeError(given_type, expected_types): a value, or a type, that is none of the types
/// expected. Its args are both, so a pickled error is made again the same way.
void give_unrecognized_type_error_its_values(py::handle unrecognized_type_error) {
    set_method(unrecognized_type_error, name().init, "initialise",
               [](py::handle self, py::object given_type, py::object expected_types) {
                   py::setattr(self, name().args, py::make_tuple(given_type, expected_types));
               },
               py::arg("given_type"), py::arg("expected_types"));
    set_method(unrecognized_type_error, name().str, "say", [](py::handle self) {
        const py::tuple args = py::getattr(self, name().args);
        const py::handle given = args[0];
        const py::str expected = expected_names(args[1]);
        if (PyType_Check(given.ptr()))
            return formatted("Expected to get type `%U`, got `%U`", expected.ptr(), py::str(py::getattr(given, name().name)).ptr());
        const py::str given_type = py::getattr(py::type::of(given), name().name);
        return formatted("Expected to get type `%U`, got `%R [%U]`", expected.ptr(), given.ptr(), given_type.ptr());
    });
}

/// The classes, made on the module's first load and kept for the process's life; a second load
/// (a reload, another interpreter's import) hands out the same classes, so there is only ever one
/// GimError.
errors::table& the_classes() {
    static errors::table made{};
    if (made.version != 0) return made;
    py::object gim_exception = new_class("pygim.errors.GimException",
        "The root of every exception pygim raises; GimError is the one to catch.", PyExc_Exception);
    py::object gim_error = new_class("pygim.errors.GimError",
        "Everything pygim raises is a GimError: `except GimError` catches it all.", gim_exception);
    py::object missing_key = new_class("pygim.errors.MissingKeyError",
        "A lookup found nothing: a key with no provider, a name never registered. A KeyError too, as a\n"
        "mapping's lookup raises.",
        py::make_tuple(gim_error, py::handle(PyExc_KeyError)));
    py::object option_error = new_class("pygim.errors.GimOptionError",
        "GimOptionError(value, options): a value none of its options. A ValueError too, as Python raises\n"
        "for a bad value; `value` and `options` say which.",
        py::make_tuple(gim_error, py::handle(PyExc_ValueError)));
    give_option_error_its_values(option_error);
    py::object dispatch_error = new_class("pygim.errors.DispatchError", "A call could not be dispatched.", gim_error);
    py::object unrecognized_type_error = new_class("pygim.errors.UnrecognizedTypeError",
        "UnrecognizedTypeError(given_type, expected_types): a value, or a type, that is none of the types\n"
        "expected.",
        dispatch_error);
    give_unrecognized_type_error_its_values(unrecognized_type_error);
    made = {errors::table_version,
            gim_exception.release().ptr(),
            gim_error.release().ptr(),
            missing_key.release().ptr(),
            option_error.release().ptr(),
            dispatch_error.release().ptr(),
            unrecognized_type_error.release().ptr()};
    return made;
}

}  // namespace

PYBIND11_MODULE(errors, m) {
    m.doc() = "Every exception pygim raises, each a GimError and the Python exception its contract needs.";
    errors::table& classes = the_classes();
    m.attr("GimException") = py::handle(classes.gim_exception);
    m.attr("GimError") = py::handle(classes.gim_error);
    m.attr("MissingKeyError") = py::handle(classes.missing_key_error);
    m.attr("GimOptionError") = py::handle(classes.option_error);
    m.attr("DispatchError") = py::handle(classes.dispatch_error);
    m.attr("UnrecognizedTypeError") = py::handle(classes.unrecognized_type_error);
    m.attr("__all__") = py::make_tuple("GimException", "GimError", "MissingKeyError", "GimOptionError", "DispatchError",
                                       "UnrecognizedTypeError");
    m.attr("_C_API") = py::capsule(&classes, errors::table_name);
}
