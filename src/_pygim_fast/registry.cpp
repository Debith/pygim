
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include "registry.h"

namespace py = pybind11;

PYBIND11_MODULE(registry, m) {
    m.doc() = "Simple registry for callables only.";

    py::class_<Registry>(m, "Registry")
        .def(py::init<>())
        .def("register", [](Registry &self, const std::string& name, py::object func_or_none, bool override) -> py::object {
            // If func_or_none is None, return a decorator
            if (func_or_none.is_none()) {
                py::cpp_function deco([&self, name, override](py::function f) {
                    self.register_callable(name, f, override);
                    return f;
                });
                return py::object(std::move(deco));
            } else {
                // Direct registration
                self.register_callable(name, func_or_none.cast<py::function>(), override);
                return func_or_none;
            }
        }, py::arg("name"), py::arg("func") = py::none(), py::kw_only(), py::arg("override") = false,
           R"pbdoc(Register a callable by name, or use as a decorator. Use 'override=True' to override.)pbdoc")
        .def("__delitem__", &Registry::remove, py::arg("name"), "Remove a registered callable by name.")
        .def("__contains__", &Registry::contains, py::arg("name"), "Check if a callable is registered.")
        .def("__len__", &Registry::size, "Number of registered callables.")
        .def("__getitem__", &Registry::getitem, py::arg("name"), "Get a registered callable by name.");
}
