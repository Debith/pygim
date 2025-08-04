//
// py_each.cpp — pybind11 bindings with `each` factory + descriptor
//
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "each.h"          // header-only C++ helpers

namespace py = pybind11;

//───────────────────────────────────────────────────────────────
// PyEach  – proxy over a Python iterable
//───────────────────────────────────────────────────────────────
class PyEach {
    std::vector<py::object> m_data;

public:
    explicit PyEach(py::iterable iterable) {
        for (py::handle h : iterable)
            m_data.emplace_back(py::reinterpret_borrow<py::object>(h));
    }

    // dynamic attribute access
    py::object getattr(const std::string& name) {
        py::list out;
        for (const py::object& obj : m_data) {
            py::object attr = obj.attr(name.c_str());
            if (PyCallable_Check(attr.ptr()))
                out.append(attr());        // call and collect result
            else
                out.append(attr);          // just collect the attribute
        }
        return out;
    }

    // lambda-style mapping
    py::list call(py::function fn) {
        py::list out;
        for (const py::object& obj : m_data) out.append(fn(obj));
        return out;
    }
};

//───────────────────────────────────────────────────────────────
// EachDescriptor :  data-descriptor for fluent `.each`
//───────────────────────────────────────────────────────────────
class EachDescriptor {
    std::string m_cacheName;

public:
    void set_name(py::object /*owner*/, py::str name) {
        m_cacheName = "_" + std::string(name) + "_proxy";
    }

    py::object get(py::object instance, py::object /*owner*/) {
        if (!instance || instance.is_none())          // accessed via class
            return py::cast(this);

        if (py::hasattr(instance, m_cacheName.c_str()))
            return instance.attr(m_cacheName.c_str());

        py::object proxy = py::module::import("pyeach").attr("each")(instance);
        instance.attr(m_cacheName.c_str()) = proxy;
        return proxy;
    }
};

//───────────────────────────────────────────────────────────────
// module definition
//───────────────────────────────────────────────────────────────
PYBIND11_MODULE(each, m) {
    py::class_<PyEach>(m, "Each", py::dynamic_attr())
        .def("__getattr__", &PyEach::getattr)
        .def("__call__",    &PyEach::call);

    m.def("each",
          [](py::iterable it) { return PyEach(it); },
          py::arg("iterable"),
          R"doc(each(iterable) → proxy

Examples
--------
    each(files).read_text()
    each(paths)(lambda p: p.stat().st_size)
)doc");

    py::class_<EachDescriptor>(m, "EachDescriptor")
        .def(py::init<>())
        .def("__set_name__", &EachDescriptor::set_name)
        .def("__get__",      &EachDescriptor::get);
}
