#pragma once
// utils/initialised.h — no pygim object is used before it is constructed.
//
// ADAPTER layer, shared by every extension. Python can make an instance without
// running __init__ — `Cls.__new__(Cls)`, which copy and pickle machinery also do —
// and when a method is then called on it, pybind11 "lazily allocates" the C++
// value (pybind11/detail/type_caster_base.h, load_value): uninitialised memory of
// the type's size, handed to the method as the object. The first member read is
// garbage and the process dies. pybind11 allocates that memory through one hook,
// the type's `operator_new`, which it reads nowhere else; pointing it at a function
// that throws turns the crash into a TypeError before the method runs, and costs
// nothing on any other path — a constructor never calls it.
//
//     PYBIND11_MODULE(pathlike, m) {
//         ...every binding...
//         pygim::adapter::refuse_uninitialised(m);   // last: every class is registered by now
//     }
//
//     pathlike.path.__new__(pathlike.path).name
//         -> TypeError: path was made by __new__ without running __init__ (...); construct it as path(...)
//
// operator_new receives only a size, so each class gets its own refusal from a
// table of `slots` functions, the slot remembering the class's name; a module that
// binds more classes than that refuses the rest without naming them.
//
// Lifetime and threading: runs once per module, at import, holding the GIL; the
// names live as long as the process (pybind11's types do too).

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>

namespace pygim::adapter {

namespace initialised_detail {

namespace py = pybind11;

inline constexpr std::size_t slots = 256;

/// The class each slot's refusal names.
inline std::vector<std::string>& names() {
    static std::vector<std::string> n;
    return n;
}

[[noreturn]] inline void refuse_named(const std::string& name) {
    throw py::type_error(name + " was made by __new__ without running __init__ (by copy or pickle machinery, or "
                         "a direct call), so it holds no value yet; construct it as " + name + "(...)");
}

template <std::size_t I>
void* refuse(std::size_t) {
    refuse_named(names()[I]);
}
/// Past the table: the class is still refused, without its name.
inline void* refuse_unnamed(std::size_t) {
    throw py::type_error("this object was made by __new__ without running __init__ (by copy or pickle machinery, "
                         "or a direct call), so it holds no value yet; construct it by calling its class");
}

template <std::size_t... I>
constexpr std::array<void* (*)(std::size_t), sizeof...(I)> refusals(std::index_sequence<I...>) {
    return {&refuse<I>...};
}
inline constexpr auto table = refusals(std::make_index_sequence<slots>{});

[[nodiscard]] inline bool ours(void* (*f)(std::size_t)) {
    for (auto* r : table) {
        if (r == f) return true;
    }
    return f == &refuse_unnamed;
}

/// The module a Python type says it belongs to ("" when it says none).
[[nodiscard]] inline std::string module_of(PyTypeObject* type) {
    py::object m = py::reinterpret_steal<py::object>(PyObject_GetAttrString(reinterpret_cast<PyObject*>(type), "__module__"));
    if (!m) {
        PyErr_Clear();
        return {};
    }
    return py::str(m).cast<std::string>();
}

}  // namespace initialised_detail

/// Every class registered under module `m` (its submodules included) refuses to
/// be used before its __init__ ran, naming itself. Call it last in PYBIND11_MODULE.
inline void refuse_uninitialised(const pybind11::module_& m) {
    namespace d = initialised_detail;
    const std::string prefix = pybind11::str(m.attr("__name__")).cast<std::string>();
    const auto under = [&](const std::string& name) {
        return name == prefix || (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0 &&
                                  name[prefix.size()] == '.');
    };
    pybind11::detail::with_internals([&](pybind11::detail::internals& internals) {
        for (auto& [type, infos] : internals.registered_types_py) {
            for (pybind11::detail::type_info* info : infos) {
                if (info->type != type || d::ours(info->operator_new) || !under(d::module_of(type))) continue;
                const std::size_t slot = d::names().size();
                d::names().push_back(type->tp_name ? std::string(std::string_view(type->tp_name).substr(
                                                         std::string_view(type->tp_name).rfind('.') + 1))
                                                   : std::string("this object"));
                info->operator_new = slot < d::slots ? d::table[slot] : &d::refuse_unnamed;
            }
        }
    });
}

}  // namespace pygim::adapter
