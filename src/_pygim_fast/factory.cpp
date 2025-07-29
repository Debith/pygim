#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include "factory.h"

namespace py = pybind11;

PYBIND11_MODULE(factory, m) {
    m.doc() = "Factory for creating objects using registered callables, with optional interface enforcement.";

    py::class_<Factory>(m, "Factory")
        .def(py::init<>())
        .def(py::init<py::object>(), py::arg("interface"))
        .def("register", [](Factory &self, const std::string& name, py::object func_or_none, bool override) -> py::object {
            if (func_or_none.is_none()) {
                py::cpp_function deco([&self, name, override](py::function f) {
                    self.register_creator(name, f, override);
                    return f;
                });
                return py::object(std::move(deco));
            } else {
                self.register_creator(name, func_or_none.cast<py::function>(), override);
                return func_or_none;
            }
        }, py::arg("name"), py::arg("func") = py::none(), py::kw_only(), py::arg("override") = false,
           R"pbdoc(Register a creator by name, or use as a decorator. Use 'override=True' to override.)pbdoc")
        .def("create",
             &Factory::create,
             py::arg("name"),
             py::kw_only(),
             R"pbdoc(
                 create(name, *args, **kwargs) -> object

                 Look up the creator registered under `name`, invoke it with the given
                 positional and keyword arguments, verify the resulting object
                 against the optional interface, and return it. Raises RuntimeError
                 if the interface check fails.
             )pbdoc")
        .def("__getitem__", &Factory::getitem, py::arg("name"),
             "Get a callable by name.")
        .def("registered_callables", &Factory::registered_callables, "Return a list of all registered creator names.")
        .def("use_module", &Factory::use_module, py::arg("module_name"),
             "Import a Python module by name to trigger registration side effects.")
        ;
}
