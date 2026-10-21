// traits/adapter/function_copy.h — FunctionCopy: a Python function made again with new code and a new
// qualified name, carrying everything else as it is.

#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <utility>

#include <pybind11/pybind11.h>

#include "introspection.h"

namespace pygim::traits {

namespace py = pybind11;

namespace detail {

/// A Python function made again with a new code object and qualified name, as types.FunctionType
/// makes one, through the C API. Everything else it carries comes along as it is: defaults, keyword
/// defaults, docstring, module, __dict__, annotations, type parameters. With a class cell, the
/// closure's __class__ cell holds that class.
/// Example: FunctionCopy(f).with_code(code).with_qualname("Person.greet").with_class_cell(Person).make().
class FunctionCopy {
public:
    explicit FunctionCopy(const py::handle& function) : m_function(py::reinterpret_borrow<py::object>(function)) {}

    FunctionCopy& with_code(py::object code) {
        m_code = std::move(code);
        return *this;
    }
    FunctionCopy& with_qualname(std::string qualname) {
        m_qualname = std::move(qualname);
        return *this;
    }
    FunctionCopy& with_class_cell(py::type cls) {
        m_class = std::move(cls);
        return *this;
    }

    [[nodiscard]] py::object make() const {
        py::object copy = made();
        set_closure(copy);
        carry_attributes(copy);
        return copy;
    }

private:
    /// The new function: the code, the original's globals, the qualified name.
    [[nodiscard]] py::object made() const {
        const py::object globals = m_function.attr("__globals__");
        const py::str qualname(m_qualname);
        auto copy = py::reinterpret_steal<py::object>(
            PyFunction_NewWithQualName(m_code.ptr(), globals.ptr(), qualname.ptr()));
        if (!copy) throw py::error_already_set();
        return copy;
    }

    /// The original's closure, its __class__ cell replaced by one holding the class; checked against
    /// the new code's free variables, as types.FunctionType checks and PyFunction_SetClosure does not.
    void set_closure(const py::object& copy) const {
        const py::object closure = m_function.attr("__closure__");
        const py::tuple freevars = m_code.attr("co_freevars");
        if (closure.is_none()) {
            if (!freevars.empty())
                throw py::value_error(std::format("{} needs a closure of {} cells and has none",
                                                  text(m_function.attr("__qualname__")), freevars.size()));
            return;
        }
        const py::tuple cells = closure;
        if (cells.size() != freevars.size())
            throw py::value_error(std::format("{} has {} closure cells for {} free variables",
                                              text(m_function.attr("__qualname__")), cells.size(), freevars.size()));
        py::tuple rebound(cells.size());
        for (std::size_t i = 0; i < cells.size(); ++i)
            rebound[i] = m_class && text(freevars[i]) == "__class__" ? class_cell() : py::object(cells[i]);
        if (PyFunction_SetClosure(copy.ptr(), rebound.ptr()) != 0) throw py::error_already_set();
    }

    [[nodiscard]] py::object class_cell() const {
        PyObject* cell = PyCell_New(m_class.ptr());
        if (cell == nullptr) throw py::error_already_set();
        return py::reinterpret_steal<py::object>(cell);
    }

    /// Everything else the original carries, as it is.
    void carry_attributes(const py::object& copy) const {
        for (const char* attribute : {"__name__", "__defaults__", "__kwdefaults__", "__doc__", "__module__"})
            copy.attr(attribute) = m_function.attr(attribute);
        copy.attr("__dict__").attr("update")(m_function.attr("__dict__"));
        if (py::hasattr(m_function, "__annotate__"))  // Python 3.14: annotations are evaluated lazily
            copy.attr("__annotate__") = m_function.attr("__annotate__");
        else
            copy.attr("__annotations__") = py::dict(m_function.attr("__annotations__"));
        if (py::hasattr(m_function, "__type_params__"))  // Python 3.12 and later
            copy.attr("__type_params__") = m_function.attr("__type_params__");
    }

    py::object m_function;
    py::object m_code;
    std::string m_qualname;
    py::object m_class;   // none: the closure's cells are kept as they are
};

}  // namespace detail

}  // namespace pygim::traits
