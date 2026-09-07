#pragma once
// utils/ambient_adapter.h — the current instance of a service, and a scope
// that swaps it for a block.
//
// ADAPTER layer (pybind11). Some services are ambient: a hot path reads
// "the current X" through a pointer rather than resolving it per call (a
// container resolve per call would cost more than the work it saves). The
// current X is an ordinary Python object — so an IoC container may own it as
// a singleton — and `with use_x(obj): ...` makes it current for a block,
// nesting and restoring on exit. The holder is a leaked py::object: it is
// never destroyed after the interpreter is gone, which is the only safe
// choice for a static that owns a Python reference.

#include <string>
#include <utility>

#include <pybind11/pybind11.h>

namespace pygim::adapter {

namespace py = pybind11;

template <class T>
class ambient {
public:
    // The current instance (the hot path: one pointer read).
    [[nodiscard]] static T& current() noexcept { return *state().ptr; }
    [[nodiscard]] static py::object object() { return state().object; }

    // Makes `obj` current; returns the previous one. TypeError unless it is a T.
    static py::object set(py::object obj) {
        T& t = as(obj);
        state_t& s = state();
        py::object prev = std::move(s.object);
        s.object = std::move(obj);
        s.ptr = &t;
        return prev;
    }
    [[nodiscard]] static T& as(py::handle obj) {
        if (!py::isinstance<T>(obj)) {
            throw py::type_error("expected a " + py::str(py::type::of<T>()).template cast<std::string>() + ", got " +
                                 py::str(py::type::of(obj)).template cast<std::string>());
        }
        return py::cast<T&>(obj);
    }

    // `with use(obj) as active: ...`
    struct scope {
        py::object target;
        py::object previous;
    };

    // Binds `<getter>()` (the current instance), `<user>(obj)` (the context
    // manager) and the scope class, and makes `initial` current.
    static void bind(py::module_& m, const char* scope_class, const char* getter, const char* getter_doc,
                     const char* user, const char* user_doc, py::object initial) {
        py::class_<scope>(m, scope_class)
            .def("__enter__", [](scope& s) {
                s.previous = set(s.target);
                return s.target;
            })
            .def("__exit__", [](scope& s, py::handle, py::handle, py::handle) {
                set(std::move(s.previous));
                return false;
            });
        m.def(getter, []() { return object(); }, getter_doc);
        m.def(user, [](py::object obj) {
                (void)as(obj);   // validate now, not at __enter__
                return scope{std::move(obj), py::object()};
            }, py::arg("target"), user_doc);
        set(std::move(initial));
    }

private:
    struct state_t {
        py::object object;
        T* ptr = nullptr;
    };
    static state_t& state() {
        static auto* s = new state_t{};   // leaked on purpose (see the header comment)
        return *s;
    }
};

}  // namespace pygim::adapter
