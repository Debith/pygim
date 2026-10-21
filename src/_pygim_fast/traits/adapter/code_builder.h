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
//   co_names     — the attribute and global names, each moved by the rules' respelt()
//   co_qualname  — moved by the rules' requalified(), where the Python has it (3.11 and later)
// The rules are utils/python_names.h's Reowning, kept apart from the builder: a builder is one code
// object, and reads them. A nested code object (a lambda, an inner def, a generator expression) gets
// its own builder, reading the same rules — code objects never change, so a new nested one means a
// new parent holding it, built from the leaves up. The fields are read and replaced through
// code_object.h: code.replace(), the language's own way to derive a code object, so no table of
// CodeType's constructor arguments per Python version is kept here.
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
// Lifetime and threading: a builder holds a reference to its code object and its own Reowning; the
// builder of a nested code object gets a copy.
// build() allocates new code objects and holds the GIL throughout, as every pybind11 call does.

#pragma once

#include <cstddef>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <pybind11/pybind11.h>

#include "../../utils/python_names.h"
#include "../../utils/adapter_utils.h"
#include "code_object.h"

namespace pygim::traits {

namespace py = pybind11;

class CodeBuilder {
public:
    /// Start from `code`, a code object, with no moves. It is never changed: build() returns a new one.
    /// Cost: one type check. Example: CodeBuilder(Trait.nested.__code__).
    explicit CodeBuilder(py::object code) : CodeBuilder(std::move(code), python_names::Reowning{}) {
        if (!PyCode_Check(m_code.ptr()))
            throw py::type_error(std::format("CodeBuilder takes a code object, not {}",
                                             py::str(py::type::of(m_code).attr("__name__")).cast<std::string>()));
    }

    /// Move the code from class `source` to class `target`: its private names (by the classes'
    /// names) and its qualified names (by their qualified names). Returns this builder, so steps
    /// chain. Nothing is rewritten until build().
    /// Example: .reown(Trait, Target) turns '_Trait__secret' into '_Target__secret'.
    CodeBuilder& reown(const py::type& source, const py::type& target) {
        using ::pygim::adapter::name_policy_of;
        m_rules = m_rules.then(name_policy_of(source), name_policy_of(target));
        return *this;
    }

    /// A new code object with every step applied, in order, to the code and every code object nested
    /// in it. With no steps it is an equal copy. Cost: one code.replace() per code object in the
    /// tree; per name and step one comparison, and an allocation only for a name that is re-spelt.
    /// Example: build().co_names is ('_Target__secret',).
    py::object build() const {
        const RecursionGuard guard;  // a nested code object's build() runs inside this one's
        return code_object::ReplaceCall::execute(m_code, moved_names(), built_constants(), moved_qualname());
    }

private:
    /// A builder with the rules already set: the builder of a code object nested in another.
    CodeBuilder(py::object code, python_names::Reowning rules) : m_code(std::move(code)), m_rules(std::move(rules)) {}

    /// The UTF-8 text of a Python str, borrowed: CPython keeps it with the string, so this copies
    /// nothing (it is made once, on the first request, for a str that is not ASCII).
    static std::string_view utf8(const py::handle& str) {
        Py_ssize_t size = 0;
        const char* data = PyUnicode_AsUTF8AndSize(str.ptr(), &size);
        if (data == nullptr) throw py::error_already_set();
        return {data, static_cast<std::size_t>(size)};
    }

    /// co_names after every step: ('_Trait__secret',) is ('_Target__secret',).
    py::object moved_names() const {
        return mapped(code_object::field(m_code, code_object::co_names()), &CodeBuilder::moved);
    }

    /// co_consts with each nested code object (a lambda, an inner def) built by its own builder.
    py::object built_constants() const {
        return mapped(code_object::field(m_code, code_object::co_consts()), &CodeBuilder::built_if_code);
    }

    /// co_qualname after every step: 'Trait.f' is 'Target.f'. Nothing before Python 3.11, where a code
    /// object has none (the function carries its qualified name, and compose() moves that).
    py::object moved_qualname() const {
        if constexpr (code_object::has_qualname)
            return requalified(code_object::field(m_code, code_object::co_qualname()));
        else
            return {};
    }

    /// `name` after every step, in order: the same object when no step moves it, a new str when one does.
    py::object moved(const py::handle& name) const {
        const auto spelling = m_rules.respelt(utf8(name));
        return spelling ? py::object(py::str(*spelling)) : py::reinterpret_borrow<py::object>(name);
    }

    /// `qualname` after every step: the same object when no step moves it, a new str when one does.
    py::object requalified(const py::handle& qualname) const {
        const std::string_view original = utf8(qualname);
        const std::string moved = m_rules.requalified(original);
        return moved != original ? py::object(py::str(moved)) : py::reinterpret_borrow<py::object>(qualname);
    }

    /// `constant` built by its own builder, reading the same rules, when it is a code object (a lambda,
    /// a nested def, a generator expression); anything else as it is.
    py::object built_if_code(const py::handle& constant) const {
        if (!PyCode_Check(constant.ptr())) return py::reinterpret_borrow<py::object>(constant);
        return CodeBuilder(py::reinterpret_borrow<py::object>(constant), m_rules).build();
    }

    /// A new tuple holding `(this->*each)(item)` for every item of `tuple`, in order — Python's
    /// `tuple(self.each(x) for x in tuple)`. Example: mapped(names, &CodeBuilder::moved) is the names
    /// after every step. Written straight into the new tuple: measured on a 40-name method, 3.46 us
    /// through pybind11's item accessors, 1.55 us this way.
    template <class Each>
    py::object mapped(const py::handle& tuple, Each each) const {
        const Py_ssize_t count = PyTuple_GET_SIZE(tuple.ptr());
        auto out = py::reinterpret_steal<py::object>(PyTuple_New(count));
        if (!out) throw py::error_already_set();
        for (Py_ssize_t i = 0; i < count; ++i) {
            py::object item = std::invoke(each, this, py::handle(PyTuple_GET_ITEM(tuple.ptr(), i)));
            PyTuple_SET_ITEM(out.ptr(), i, item.release().ptr());
        }
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

    py::object m_code;
    python_names::Reowning m_rules;  // each nested builder gets a copy: a few names, once per class at import (#255)
};

}  // namespace pygim::traits
