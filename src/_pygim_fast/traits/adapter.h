// traits/adapter.h — CodeBuilder: a code object rebuilt so that it belongs to another class.
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
//   co_names     — the attribute and global names, each moved by core.h's reowned()
//   co_qualname  — moved by core.h's requalified(), where the Python has it (3.11 and later)
// The rule is core.h's; this header only walks Python's code objects and calls code.replace(), the
// language's own way to derive a code object, so no table of CodeType's constructor arguments per
// Python version is kept here.
//
// Steps apply in the order given, to each name in turn: reown(A, B).reown(B, C) moves A's private
// names to C. A name that A's code spells as B's own is moved on by the second step too — the
// exception traits_proofs.cpp states beside the law.
//
// Lifetime and threading: a builder holds a reference to the original code object and the steps as
// plain strings; build() allocates new code objects and holds the GIL throughout, as every pybind11
// call does.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>

#include "core.h"

namespace pygim::traits {

namespace py = pybind11;

class CodeBuilder {
public:
    /// Start from `code`, a code object. It is never changed: build() returns a new one.
    /// Cost: one type check. Example: CodeBuilder(Trait.nested.__code__).
    explicit CodeBuilder(py::object code) : m_code(std::move(code)) {
        if (!PyCode_Check(m_code.ptr()))
            throw py::type_error("CodeBuilder takes a code object, not " +
                                 py::str(py::type::of(m_code).attr("__name__")).cast<std::string>());
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
    /// tree, and one move per name per step. Example: build().co_names is ('_Target__secret',).
    py::object build() const { return rebuilt(m_code); }

private:
    struct Step {
        std::string from_name, to_name;  // what mangling uses
        std::string from_qual, to_qual;  // what qualified names start with
    };

    static std::string text(const py::handle& owner, const char* attribute) {
        return py::str(owner.attr(attribute)).cast<std::string>();
    }

    py::object rebuilt(const py::handle& code) const {
        py::list names;
        for (const auto& name : code.attr("co_names")) {
            auto moved = name.cast<std::string>();
            for (const auto& step : m_steps) moved = reowned(step.from_name, step.to_name, moved);
            names.append(py::str(moved));
        }
        py::list constants;
        for (const auto& constant : code.attr("co_consts"))
            constants.append(PyCode_Check(constant.ptr()) ? rebuilt(constant)
                                                          : py::reinterpret_borrow<py::object>(constant));
        py::dict changes;
        changes["co_names"] = py::tuple(names);
        changes["co_consts"] = py::tuple(constants);
        if (py::hasattr(code, "co_qualname")) {  // Python 3.11 and later
            auto qualname = code.attr("co_qualname").cast<std::string>();
            for (const auto& step : m_steps) qualname = requalified(step.from_qual, step.to_qual, qualname);
            changes["co_qualname"] = py::str(qualname);
        }
        return code.attr("replace")(**changes);
    }

    py::object m_code;
    std::vector<Step> m_steps;
};

}  // namespace pygim::traits
