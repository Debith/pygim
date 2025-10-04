
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include "registry.h"

namespace py = pybind11;

// --- pybind11 bindings for matrix wrapper -----------------------------------
PYBIND11_MODULE(registry, m) {
    namespace py = pybind11;

    py::enum_<pygim::KeyPolicyKind>(m, "KeyPolicyKind")
        .value("qualname", pygim::KeyPolicyKind::Qualname)
        .value("identity", pygim::KeyPolicyKind::Identity)
        .export_values();

  py::class_<pygim::Registry>(m, "Registry")
    .def(py::init<bool, pygim::KeyPolicyKind, std::size_t>(),
       py::arg("hooks")=false, py::arg("policy")=pygim::KeyPolicyKind::Qualname, py::arg("capacity")=0,
       "Create a Registry.\n\n"
       "Parameters:\n  hooks: enable lifecycle hooks.\n  policy: qualname|identity key policy.\n  capacity: optional map reserve size.")
        .def("__getitem__", &pygim::Registry::get)
        .def("__len__", &pygim::Registry::size)
        .def("__contains__", &pygim::Registry::contains)
        .def("__setitem__", [](pygim::Registry &r, py::object key, py::object value){ r.register_or_override(key, value, false); }, py::arg("py_type"), py::arg("value"))
        .def("register", [](pygim::Registry &r, py::object key, py::object value, bool override){
                r.register_or_override(key, value, override); return value; },
              py::arg("py_type"), py::arg("value"), py::arg("override")=false,
              "Register a value under key.\n\n"
              "override=False (default) and existing key -> RuntimeError.\n"
              "override=True and missing key -> RuntimeError.\n"
              "Returns the value (supports decorator form).")
        // Decorator form: register(key, override=False)(callable) -> callable
        .def("register", [](pygim::Registry &r, py::object key, bool override){
            return py::cpp_function([&r, key=py::object(key), override](py::object value){
                r.register_or_override(key, value, override); return value; });
        }, py::arg("py_type"), py::arg("override")=false,
         "Decorator form: @registry.register(key, override=False)\n\n"
         "Returns a wrapper that registers the decorated object then returns it.\n"
         "Enforces same override rules as direct call.")
        .def("get", &pygim::Registry::get, py::arg("key"))
      .def("post", &pygim::Registry::post, py::arg("key"), py::arg("value"),
          "Invoke post hooks for a key with an arbitrary Python object.\n\n"
          "No-op if hooks are disabled.")
        .def("on_register", &pygim::Registry::on_register)
        .def("on_pre", &pygim::Registry::on_pre)
        .def("on_post", &pygim::Registry::on_post)
    .def("registered_keys", &pygim::Registry::registered_keys,
      "Return list of (id_or_object, name) for all registered entries.")
    .def("find_id", &pygim::Registry::find_id, py::arg("id"), py::arg("name")=py::none(),
      "Direct lookup by string id (qualname policy only); returns value or None.")
      .def("__repr__", &pygim::Registry::repr,
          "Stable representation exposing policy, hooks flag, and size.\n"
          "Used by tests; do not change field order without updating tests.")
        ;
};