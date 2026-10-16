// traits/bindings.cpp — registers pygim.traits. The work lives in adapter.h, compose.h and core.h.

#include <string>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "adapter.h"
#include "compose.h"

namespace py = pybind11;

PYBIND11_MODULE(traits, m) {
    m.doc() = "Traits: behaviour written once and composed into classes.";

    using pygim::traits::CodeBuilder;
    using pygim::traits::Provenance;
    using pygim::traits::TraitRecord;

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

    py::class_<Provenance>(m, "Provenance", "Where a composed member came from.")
        .def_readonly("trait", &Provenance::trait, "The trait's module and qualified name.")
        .def_readonly("defined", &Provenance::defined, "``file:line`` where the trait defined the member.")
        .def_readonly("composed", &Provenance::composed, "``file:line`` of the ``compose()`` call.")
        .def("__repr__", [](const Provenance& p) {
            return "Provenance(trait=" + py::repr(py::str(p.trait)).cast<std::string>() +
                   ", defined=" + py::repr(py::str(p.defined)).cast<std::string>() +
                   ", composed=" + py::repr(py::str(p.composed)).cast<std::string>() + ")";
        });

    py::class_<TraitRecord>(m, "TraitRecord", "The members composed into one class, by name, in name order.")
        .def("__getitem__", &TraitRecord::at, py::arg("name"))
        .def("__contains__", &TraitRecord::contains, py::arg("name"))
        .def("__len__", &TraitRecord::size)
        .def("__iter__", [](const TraitRecord& r) { return py::iter(py::cast(r.names())); })
        .def("keys", &TraitRecord::names, "The composed members' names, in name order.")
        .def("__repr__", [](const TraitRecord& r) {
            return "TraitRecord(" + py::repr(py::cast(r.names())).cast<std::string>() + ")";
        });

    m.def("compose", &pygim::traits::compose, py::arg("target"), py::kw_only(), py::arg("override") = false,
          R"doc(
Copy every member of every trait into ``target``, rewritten to belong there, and return ``target``.

A trait is a class whose functions, properties, classmethods and staticmethods are written to be
copied; nothing inherits from it. Each copy reads ``target``'s private names, carries ``target``'s
qualified name, and its zero-argument ``super()`` resolves in ``target``. Refused, writing
nothing: a trait that is not a class (``TypeError``), any other member (``TypeError``), a name
``target`` already defines or two traits both define (``ValueError``). ``override=True`` replaces
instead, the later trait winning.
)doc");

    m.def("traits_of", &pygim::traits::traits_of, py::arg("cls"),
          "The record of what was composed into ``cls`` itself: name -> ``Provenance``. Empty when nothing was.");
}
