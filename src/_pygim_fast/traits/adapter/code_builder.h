// traits/adapter/code_builder.h — CodeBuilder: a code object rebuilt so that it belongs to another class.
//
// Worked example:
//
//   class Trait:
//       def nested(self):
//           return (lambda: self.__secret)()     # the lambda's co_names: ('_Trait__secret',)
//
//   code = CodeBuilder(Trait.nested.__code__).reown(Trait, Target).build()
//   code.co_qualname                -> 'Target.nested'                    (Python 3.11 and later)
//   code.co_consts[1].co_names      -> ('_Target__secret',)               the nested lambda, rebuilt too
//   code.co_consts[1].co_qualname   -> 'Target.nested.<locals>.<lambda>'
//   Trait.nested.__code__           -> unchanged: a code object never changes; build() makes a new one
//
// What is rewritten, in the code and in every code object nested in its constants:
//   co_names     — the attribute and global names, each moved by python_names.h's respelt()
//   co_qualname  — moved by python_names.h's requalified(), where the Python has it (3.11 and later)
// The rule is utils/python_names.h's; this header only walks Python's code objects and calls
// code.replace(), the language's own way to derive a code object, so no table of CodeType's
// constructor arguments per Python version is kept here.
//
// A name is read in place, through the UTF-8 text CPython keeps with every str, and a name no step
// moves is passed on as the very same object: only a re-spelt name allocates. The walk uses the C
// API directly — interned attribute names, tuples written in place, code.replace() by vectorcall —
// because pybind11's accessors, a str made per attribute lookup and a kwargs dict cost more than
// the work itself (measured). benchmarks/traits_code_builder.py compares the result with
// code.replace() walking the same code objects in Python.
//
// Steps apply in the order given, to each name in turn: reown(A, B).reown(B, C) moves A's private
// names to C. A name that A's code spells as B's own is moved on by the second step too — the
// exception python_names_proofs.cpp states beside the law.
//
// Lifetime and threading: a builder holds a reference to the original code object and the steps as
// plain strings; build() allocates new code objects and holds the GIL throughout, as every pybind11
// call does.

#pragma once

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>

#include "../../utils/python_names.h"

namespace pygim::traits {

namespace py = pybind11;

class CodeBuilder {
public:
    /// Start from `code`, a code object. It is never changed: build() returns a new one.
    /// Cost: one type check. Example: CodeBuilder(Trait.nested.__code__).
    explicit CodeBuilder(py::object code) : m_code(std::move(code)) {
        if (!PyCode_Check(m_code.ptr()))
            throw py::type_error(std::format("CodeBuilder takes a code object, not {}",
                                             text(py::type::of(m_code), "__name__")));
    }

    /// Move the code from class `source` to class `target`: its private names (by the classes'
    /// names) and its qualified names (by their qualified names). Returns this builder, so steps
    /// chain. Cost: two strings copied per class; nothing is rewritten until build().
    /// Example: .reown(Trait, Target) turns '_Trait__secret' into '_Target__secret'.
    CodeBuilder& reown(const py::type& source, const py::type& target) {
        m_steps.push_back({text(source, "__name__"), text(target, "__name__"),
                           text(source, "__qualname__"), text(target, "__qualname__")});
        return *this;
    }

    /// A new code object with every step applied, in order, to the code and every code object nested
    /// in it. With no steps it is an equal copy. Cost: one code.replace() per code object in the
    /// tree; per name and step one comparison, and an allocation only for a name that is re-spelt.
    /// Example: build().co_names is ('_Target__secret',).
    py::object build() const { return rebuilt(m_code); }

private:
    struct Step {
        std::string from_name, to_name;  // what mangling uses
        std::string from_qual, to_qual;  // what qualified names start with
    };

    static std::string text(const py::handle& owner, const char* attribute) {
        return py::str(owner.attr(attribute)).cast<std::string>();
    }

    /// The UTF-8 text of a Python str, borrowed: CPython keeps it with the string, so this copies
    /// nothing (it is made once, on the first request, for a str that is not ASCII).
    static std::string_view utf8(const py::handle& str) {
        Py_ssize_t size = 0;
        const char* data = PyUnicode_AsUTF8AndSize(str.ptr(), &size);
        if (data == nullptr) throw py::error_already_set();
        return {data, static_cast<std::size_t>(size)};
    }

    /// `name` after every step, in order: the same object when no step moves it, a new str when one does.
    py::object moved(const py::handle& name) const {
        const std::string_view original = utf8(name);
        std::optional<std::string> spelling;  // set once a step re-spells the name
        for (const auto& step : m_steps) {
            const std::string_view current = spelling ? std::string_view{*spelling} : original;
            if (auto next = python_names::respelt(step.from_name, step.to_name, current)) spelling = std::move(next);
        }
        return spelling ? py::object(py::str(*spelling)) : py::reinterpret_borrow<py::object>(name);
    }

    /// An attribute looked up by an interned name, so no str is made per lookup.
    static py::object attribute(const py::handle& owner, PyObject* name) {
        PyObject* value = PyObject_GetAttr(owner.ptr(), name);
        if (value == nullptr) throw py::error_already_set();
        return py::reinterpret_steal<py::object>(value);
    }

    /// `text` as an interned str, kept for the life of the process as interned names are.
    static PyObject* interned(const char* text) {
        PyObject* name = PyUnicode_InternFromString(text);
        if (name == nullptr) throw py::error_already_set();
        return name;
    }

    /// A new tuple of `count` items, each `item(i)`, written straight into the tuple. Measured on a
    /// 40-name method: 3.46 us through pybind11's item accessors, 1.55 us with these macros.
    template <class Item>
    static py::object tuple_of(Py_ssize_t count, Item&& item) {
        auto out = py::reinterpret_steal<py::object>(PyTuple_New(count));
        if (!out) throw py::error_already_set();
        for (Py_ssize_t i = 0; i < count; ++i) PyTuple_SET_ITEM(out.ptr(), i, item(i).release().ptr());
        return out;
    }

    // Guards the native recursion over nested code objects: a code object nested past CPython's
    // recursion limit raises RecursionError, as deeply nested Python does, instead of overrunning
    // the C stack. One level per nesting depth; the limit is the test budget's, not an input bound.
    struct RecursionGuard {
        RecursionGuard() {
            if (Py_EnterRecursiveCall(" in CodeBuilder.build")) throw py::error_already_set();
        }
        ~RecursionGuard() { Py_LeaveRecursiveCall(); }
        RecursionGuard(const RecursionGuard&) = delete;
        RecursionGuard& operator=(const RecursionGuard&) = delete;
    };

    py::object rebuilt(const py::handle& code) const {
        const RecursionGuard guard;  // left on every exit, including an exception thrown below
        static PyObject* const co_names = interned("co_names");
        static PyObject* const co_consts = interned("co_consts");
        static PyObject* const co_qualname = interned("co_qualname");
        static PyObject* const replace = interned("replace");
        static const bool has_qualname = PyObject_HasAttr(code.ptr(), co_qualname) == 1;  // Python 3.11 and later
        static PyObject* const keywords = has_qualname ? Py_BuildValue("(OOO)", co_names, co_consts, co_qualname)
                                                       : Py_BuildValue("(OO)", co_names, co_consts);
        if (keywords == nullptr) throw py::error_already_set();

        const py::object names = attribute(code, co_names);
        const py::object moved_names = tuple_of(PyTuple_GET_SIZE(names.ptr()), [&](Py_ssize_t i) {
            return moved(PyTuple_GET_ITEM(names.ptr(), i));
        });
        const py::object constants = attribute(code, co_consts);
        const py::object rebuilt_constants = tuple_of(PyTuple_GET_SIZE(constants.ptr()), [&](Py_ssize_t i) {
            const py::handle constant = PyTuple_GET_ITEM(constants.ptr(), i);
            return PyCode_Check(constant.ptr()) ? rebuilt(constant) : py::reinterpret_borrow<py::object>(constant);
        });

        py::object qualname;
        if (has_qualname) {
            qualname = attribute(code, co_qualname);
            std::string requalified{utf8(qualname)};
            for (const auto& step : m_steps)
                requalified = python_names::requalified(step.from_qual, step.to_qual, requalified);
            if (requalified != utf8(qualname)) qualname = py::str(requalified);
        }
        // code.replace(co_names=..., co_consts=..., co_qualname=...) by vectorcall: no dict is built.
        PyObject* arguments[] = {code.ptr(), moved_names.ptr(), rebuilt_constants.ptr(), qualname.ptr()};
        PyObject* result = PyObject_VectorcallMethod(replace, arguments, 1 | PY_VECTORCALL_ARGUMENTS_OFFSET, keywords);
        if (result == nullptr) throw py::error_already_set();
        return py::reinterpret_steal<py::object>(result);
    }

    py::object m_code;
    std::vector<Step> m_steps;
};

}  // namespace pygim::traits
