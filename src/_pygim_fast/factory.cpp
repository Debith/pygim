#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include "factory.h"

namespace py = pybind11;

PYBIND11_MODULE(factory, m) {
    namespace py = pybind11;
    m.doc() = "Factory for creating objects using registered providers, with optional interface enforcement.";

    py::class_<pygim::Factory>(m, "Factory")
        .def(py::init<>(), R"pbdoc(Create a Factory without a fixed interface. You must use register_creator_for to register providers.)pbdoc")
        .def(py::init<py::object>(), py::arg("interface"), R"pbdoc(Create a Factory bound to an interface (Python type).)pbdoc")

        // Register: name + provider or decorator usage
        .def("register",
             [](pygim::Factory &self,
                const std::string& name,
                py::object func_or_none = py::none(),
                bool override_existing = false) -> py::object {
                 if (func_or_none.is_none()) {
                     // decorator form: @factory.register("name")
                     py::cpp_function deco([&self, name, override_existing](py::object f) {
                         self.register_creator(f, name, override_existing);
                         return f;
                     });
                     return py::object(std::move(deco));
                 } else {
                     self.register_creator(func_or_none, name, override_existing);
                     return func_or_none;
                 }
             },
             py::arg("name"), py::arg("func") = py::none(), py::kw_only(), py::arg("override") = false,
             R"pbdoc(Register a provider under 'name'.
If func is omitted, returns a decorator so you can write:

    @factory.register("default")
    class Impl: ...

Set override=True to replace an existing registration.)pbdoc")

        // Explicit interface registration
        .def("register_for",
             [](pygim::Factory &self,
                py::object interface,
                const std::string& name,
                py::object func_or_none = py::none(),
                bool override_existing = false) -> py::object {
                 if (func_or_none.is_none()) {
                     py::cpp_function deco([&self, interface, name, override_existing](py::object f) {
                         self.register_creator_for(interface, f, name, override_existing);
                         return f;
                     });
                     return py::object(std::move(deco));
                 } else {
                     self.register_creator_for(interface, func_or_none, name, override_existing);
                     return func_or_none;
                 }
             },
             py::arg("interface"), py::arg("name"), py::arg("func") = py::none(), py::kw_only(), py::arg("override") = false,
             R"pbdoc(Register a provider for a specific interface (overrides the Factory's bound interface, if any).
Supports decorator usage as well.)pbdoc")

        .def("create", &pygim::Factory::create,
             py::arg("name"), py::kw_only(),
             R"pbdoc(create(name, *args, **kwargs) -> object

Look up the provider registered under 'name' for this Factory's interface,
invoke it with the given positional and keyword arguments, and (if the Factory
has an interface) verify the instance is of that type.)pbdoc")

        .def("__getitem__", &pygim::Factory::getitem, py::arg("name"),
             R"pbdoc(Return the registered provider (callable/class) by name.)pbdoc")

        .def("registered_callables", &pygim::Factory::registered_callables,
             R"pbdoc(Return a list of registered names for this Factory's interface.)pbdoc")

        .def("use_module", &pygim::Factory::use_module, py::arg("module_name"),
             R"pbdoc(Import a module to trigger side-effect registrations.)pbdoc");
}