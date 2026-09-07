#pragma once
// utils/ambient_adapter.h — the current instance of a service, and a scope
// that swaps it for a block.
//
// ADAPTER layer (pybind11). Some services are ambient: a hot path reads "the
// current X" through a pointer rather than resolving it per call. The current
// X is an ordinary Python object — so an IoC container may own it as a
// singleton — and `with use_x(obj): ...` makes it current for a block, nesting
// and restoring the previous one on exit. There is one current X per T per
// process (the state is a function-local static of the instantiation).
//
// Why a pointer and not a container resolve: pathlike's path() and every
// derived-path operation (parent, /, with_suffix, glob results) need the
// current store before doing their real work — a table probe of a few tens
// of ns. A resolve per call would be a lookup keyed by type plus a lifecycle
// check plus the Python calls to get there — more than the probe it precedes.
// Resolve once (`container.resolve(PathStore)`), then `use_store(...)` it.
//
// Who uses it: pathlike/adapter/path_store.h — `using current =
// adapter::ambient<path_store>`; `current::current()` is the store behind
// path(), `current::as(obj)` validates a `store=` argument, and
// `current::bind(m, "_StoreScope", "store", ..., "use_store", ..., default)`
// puts the Python face on the module. The scope tests are
// tests/unittests/test_path_store.py (test_use_store_scopes_identity_to_the_
// block, test_use_store_rejects_non_stores, test_an_ioc_container_owns_a_store).
//
// A worked example, used in the comments below — `ambient<path_store>`,
// bound in the module `m` with `default` (a PathStore module attribute):
//
//     using current = ambient<path_store>;
//     current::bind(m, "_StoreScope", "store", doc, "use_store", doc, default);
//         // m gains three things: the class _StoreScope, store(), use_store(target)
//         // and `default` is now current
//     current::current()      -> path_store&  (one pointer read)
//     current::object()       -> default      (a new reference to the object)
//     current::set(private)   -> default      (the previous one; private is now current)
//     current::as(object())   -> TypeError: expected a <class '...PathStore'>, got <class 'object'>
//
// and, from Python, the nesting the tests exercise (outer is the fixture's
// store, current when the block starts):
//
//     with use_store(private) as active:      # __enter__: set(private) -> previous = outer
//         store() is private and active is private
//         with use_store(PathStore()):        # __enter__: set(another) -> previous = private
//             store() is another
//         # __exit__: set(private)             # back to private
//         store() is private
//     # __exit__: set(outer)                   # back to outer
//     use_store(object())                     # TypeError at the call, not at `with`
//
// The state is a leaked `new` on purpose. A function-local static that owns a
// py::object is destroyed during static destruction at process exit — after
// Py_Finalize has run, on whatever thread exits, without the GIL — and the
// DECREF it would do then is a crash or a use-after-free. Leaking it means the
// current object's last reference is simply never dropped; the interpreter
// tears the object down with everything else (the default store is a module
// attribute, so it is the module's to clean up). The cost is one pointer and
// one reference per T, for the life of the process.

#include <string>
#include <utility>

#include <pybind11/pybind11.h>

namespace pygim::adapter {

namespace py = pybind11;

/// The current instance of the service `T` (a pybind11-bound class), and the
/// Python-facing scope that swaps it for a block.
template <class T>
class ambient {
public:
    // ── reading ───────────────────────────────────────────────────────────

    /// The current instance: one pointer read, no reference-count change, the
    /// hot path. Valid once `bind()` (or `set()`) has run — importing the
    /// module does that — and the reference is good for as long as the object
    /// stays current or the caller otherwise holds it.
    ///
    ///     current::current()   // the path_store behind path()
    [[nodiscard]] static T& current() noexcept { return *state().ptr; }
    /// The current instance as a Python object — a new reference. What the
    /// bound getter (`store()`) returns. GIL.
    ///
    ///     current::object()   // default, after bind(); private, inside its scope
    [[nodiscard]] static py::object object() { return state().object; }

    // ── swapping ──────────────────────────────────────────────────────────

    /// Makes `obj` current and returns the previous current object, so a
    /// scope can put it back. The previous reference is handed to the caller
    /// rather than dropped here: the caller decides when it dies. TypeError
    /// (from `as`) unless `obj` is a T, in which case nothing changes.
    ///
    ///     py::object prev = current::set(private);   // prev is default; current() is private's
    ///     current::set(std::move(prev));             // default again
    ///
    /// Cost: an isinstance check, a cast and two object moves. GIL.
    static py::object set(py::object obj) {
        T& t = as(obj);
        state_t& s = state();
        py::object prev = std::move(s.object);
        s.object = std::move(obj);
        s.ptr = &t;
        return prev;
    }
    /// `obj` as a T&, or a TypeError naming both types — the expected one and
    /// the one given:
    ///
    ///     current::as(default)    // the path_store& inside it
    ///     current::as(object())   // TypeError: expected a <class '...PathStore'>, got <class 'object'>
    ///
    /// The check `set` and `use_x()` rely on, and what path_store.h's
    /// `as_store()` is. GIL.
    [[nodiscard]] static T& as(py::handle obj) {
        if (!py::isinstance<T>(obj)) {
            throw py::type_error("expected a " + py::str(py::type::of<T>()).template cast<std::string>() + ", got " +
                                 py::str(py::type::of(obj)).template cast<std::string>());
        }
        return py::cast<T&>(obj);
    }

    // ── the Python face ───────────────────────────────────────────────────

    /// `with use_x(obj) as active: ...` — the context manager `use_x()` hands
    /// back. `target` is the object to make current (validated when the scope
    /// was made); `previous` is filled by `__enter__` and consumed by
    /// `__exit__`, which is what lets scopes nest: each level restores exactly
    /// the object it displaced (private -> another -> private -> outer in the
    /// header's example).
    struct scope {
        py::object target;
        py::object previous;
    };

    /// Registers exactly three things on `m` and makes `initial` current:
    ///
    ///   scope_class  the `scope` struct as a Python class (`_StoreScope`): its
    ///                `__enter__` sets `target` current, keeps the displaced
    ///                object in `previous`, and returns `target` (the `as`
    ///                value); its `__exit__` sets `previous` back and returns
    ///                false, so an exception in the block propagates;
    ///   getter       `<getter>()` (`store()`): the current instance, `object()`;
    ///   user         `<user>(target)` (`use_store(target)`): validates `target`
    ///                with `as` NOW — `use_store(object())` raises TypeError at
    ///                the call, before any `with` — and returns a scope over it.
    ///
    ///     current::bind(m, "_StoreScope", "store", doc, "use_store", doc, default);
    ///     // m._StoreScope, m.store, m.use_store exist; current() is default's
    ///
    /// Call it once per module, at import. GIL.
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
    /// The current object and, cached, the T* inside it — so `current()` is a
    /// read of `ptr` and never a cast.
    struct state_t {
        py::object object;
        T* ptr = nullptr;
    };
    /// The one state per T: allocated on first use and never freed — see the
    /// header comment on static destruction after the interpreter is gone.
    static state_t& state() {
        static auto* s = new state_t{};   // leaked on purpose (see the header comment)
        return *s;
    }
};

}  // namespace pygim::adapter
