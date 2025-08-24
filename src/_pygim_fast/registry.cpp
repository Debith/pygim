
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include "registry.h"

namespace py = pybind11;

// --- pybind11 bindings for matrix wrapper -----------------------------------
PYBIND11_MODULE(ioc_registry, m) {
    namespace py = pybind11;

    py::enum_<pygim::KeyPolicyKind>(m, "KeyPolicyKind")
        .value("qualname", pygim::KeyPolicyKind::Qualname)
        .value("identity", pygim::KeyPolicyKind::Identity)
        .export_values();

    py::class_<pygim::Registry>(m, "Registry")
        .def(py::init<bool, pygim::KeyPolicyKind>(), py::arg("hooks")=false, py::arg("policy")=pygim::KeyPolicyKind::Qualname)
        .def("__getitem__", &pygim::Registry::getitem)
        .def("__len__", &pygim::Registry::size)
        .def("register", &pygim::Registry::reg, py::arg("py_type"), py::arg("name") = py::none(), py::arg("value"))
        .def("get", &pygim::Registry::get, py::arg("key"), py::arg("name") = py::none())
        .def("on_register", &pygim::Registry::on_register)
        .def("on_pre", &pygim::Registry::on_pre)
        .def("on_post", &pygim::Registry::on_post)
        ;
};