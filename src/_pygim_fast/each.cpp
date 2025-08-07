// each_bindings.cpp – pybind11 module definition
// ------------------------------------------------------
// This file *only* contains the binding glue.  All runtime logic lives in
// each.h so that this translation unit stays tiny and fast to
// compile.

#include <pybind11/pybind11.h>
#include "each.h"

namespace py = pybind11;

PYBIND11_MODULE(each, m) {
    m.doc() = "Broadcast attribute access / method calls over an iterable";

    // ---------------------- Bind Proxy ---------------------- //
    py::class_<Proxy, std::shared_ptr<Proxy>>(m, "_Proxy", R"doc(
Lightweight helper returned by `each`.  Users never construct it directly;
Python code sees it only when doing `obj.each.attr` or `obj.each.method()`.
)doc")
        .def("__repr__", &Proxy::representation)
        .def("__getattr__", &Proxy::getattr)
        .def("__call__", &Proxy::call);

    // ---------------------- Bind Each ----------------------- //
    py::class_<Each>(m, "each", R"doc(
`each` can be used either as a *descriptor* (declare `each = each()` inside
an iterable class) or as a *factory* (`each(iterable)`).
)doc")
        .def(py::init<py::object>(), py::arg("iterable") = py::none())
        .def("__repr__", &Each::representation)
        .def("__getattr__", &Each::getattr)
        .def("__set_name__", &Each::set_name)
        .def("__get__", &Each::get);
}
