// traits/bindings.cpp — registers pygim.traits. The work lives in adapter.h and core.h.

#include <pybind11/pybind11.h>

#include "adapter.h"

namespace py = pybind11;

PYBIND11_MODULE(traits, m) {
    m.doc() = "Traits: behaviour written once and composed into classes.";

    using pygim::traits::CodeBuilder;

    py::class_<CodeBuilder>(m, "CodeBuilder", R"doc(
A code object rebuilt so that it belongs to another class.

Inside ``class Trait:``, Python compiles ``self.__secret`` as ``self._Trait__secret``. Code moved
into another class must read that class's spelling instead; ``reown`` records the move and
``build`` makes a new code object with it applied — to the code and every code object nested in
it. The original code object is never changed.

>>> code = CodeBuilder(Trait.method.__code__).reown(Trait, Target).build()  # doctest: +SKIP
)doc")
        .def(py::init<py::object>(), py::arg("code"),
             "Start from ``code``, a code object; it is never changed.")
        .def("reown", &CodeBuilder::reown, py::arg("source"), py::arg("target"),
             py::return_value_policy::reference_internal,
             "Move the code from class ``source`` to class ``target``: private and qualified names. "
             "Returns this builder, so steps chain.")
        .def("build", &CodeBuilder::build,
             "A new code object with every step applied, in order, to the code and every code object nested in it.");
}
