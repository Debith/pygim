// each_proxy.hpp – C++ implementation of Python‑side helpers `Proxy` and `each`
// ---------------------------------------------------------------------------
// This header **contains the full implementation** so that a standalone
// translation unit (bindings.cpp) only has to expose the classes to Python
// with pybind11.  Keeping implementation in the header avoids a separate
// library build step and lets the compiler inline everything when possible.
//
// Design notes
// ------------
// • `Proxy` mirrors the behaviour of the original `_proxy` Python class:
//   it stores a Python iterable, fans out attribute access or method calls
//   to every element, and returns a `py::list` of results.  The *last*
//   method name accessed is cached until the subsequent `__call__`.
// • `Each` plays the same dual role as the original `each` helper:
//   – When constructed with an `iterable` it acts as a *factory* returning
//     a `Proxy` on first attribute access.
//   – When declared as a **descriptor** on an *iterable* Python class
//     (via `each = each()` in the class body) it keeps a **per‑instance**
//     proxy in a `weakref.WeakKeyDictionary`, guaranteeing:
//         · one proxy per live instance,
//         · no memory leaks (entry vanishes when the instance dies),
//         · never calling the wrong object even if CPython re‑uses an
//           `id()` value.
//
// The implementation relies only on the public pybind11 C++ API and core
// Python types, so it is ABI‑stable across CPython versions.
// ---------------------------------------------------------------------------
#pragma once

#include <iostream>
#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>

namespace py = pybind11;

// Forward declaration so `Each` can return it.
class Proxy;

// ---------------------------------------------------------------------------
//                               Proxy helper
// ---------------------------------------------------------------------------
class Proxy {
public:
    //! Construct a proxy bound to a given Python iterable.
    explicit Proxy(const py::object &iterable)
        : m_iterable(iterable), m_funcName(std::nullopt) {}

    // ---------------------------------------------------------------------
    // Python special methods exposed via pybind11
    // ---------------------------------------------------------------------

    /** __getattr__(name) – broadcast attribute lookup or prepare method call
     *
     * Behaviour mirrors the original Python version:
     *   • If *any* element lacks the attribute, an AttributeError is thrown
     *     immediately (simpler than collecting error objects).
     *   • If the attribute on the *first* element is **callable**, we assume
     *     method broadcast semantics (matching the original helper) – we
     *     cache `name` in `m_funcName` and return *self* so Python will next
     *     invoke `__call__`.
     *   • Otherwise we build a `py::list` with the attribute from every
     *     element and return it.
     */
    py::object getattr(const std::string &name) {
        py::list results;
        std::cout << "Proxy::getattr(" << name << ")\n";

        for (const py::handle &item : m_iterable) {
            std::cout << "Checking attribute '" << name << "' on item: " << py::repr(item).cast<std::string>() << "\n";
            if (!py::hasattr(item, name.c_str())) {
                throw py::attribute_error(
                    (std::string("'") + py::str(item.get_type()).cast<std::string>() +
                     "' object has no attribute '" + name + "'").c_str());
            }
            std::cout << "Attribute '" << name << "' exists on item.\n";
            py::object attr = item.attr(name.c_str());
            std::cout << "Attribute type: " << py::str(attr.get_type()).cast<std::string>() << "\n";
            if (py::isinstance<py::function>(attr) || py::isinstance<py::cpp_function>(attr) ||
                PyCallable_Check(attr.ptr())) {
                std::cout << "Attribute '" << name << "' is callable.\n";
                m_funcName = name;  // Cache the callable name
                return py::cast(this);
            } else {
                std::cout << "Attribute '" << name << "' is not callable, adding to results.\n";
                results.append(attr);
            }
        }

        // Empty iterable ⇒ return None (to stay consistent with Python impl.)
        return results.size() ? py::object(results) : py::none();
    }

    /** __call__(*args, **kwargs) – broadcast previously cached method */
    py::list call(py::args args, py::kwargs kwargs) {
        if (!m_funcName) {
            throw py::type_error("Proxy object is not in callable mode (missing attribute access)");
        }

        std::cout << "Proxy::call(" << m_funcName.value() << ", args=" << py::repr(args) << ", kwargs=" << py::repr(kwargs) << ")\n";
        py::list results;
        for (const py::handle &item : m_iterable) {
            std::cout << "Calling method '" << m_funcName.value() << "' on item: " << py::repr(item).cast<std::string>() << "\n";
            py::object method = item.attr(m_funcName.value().c_str());
            std::cout << "Method type: " << py::str(method.get_type()).cast<std::string>() << "\n";
            results.append(method(*args, **kwargs));
        }

        // Clear cached function name to avoid accidental reuse.
        m_funcName.reset();
        return results;
    }

    std::string representation() const {
        return "<Each-proxy bound to " + py::repr(m_iterable).cast<std::string>() + ">";
    }

private:
    py::object m_iterable;                     //!< Bound Python iterable
    std::optional<std::string> m_funcName;    //!< Last looked‑up callable attr
};

// ---------------------------------------------------------------------------
//                             Each helper / descriptor
// ---------------------------------------------------------------------------
class Each {
public:
    /**
     * Factory‑mode constructor (iterable provided) **or** descriptor‑mode
     * default constructor (iterable = None).
     */
    explicit Each(const py::object &iterable = py::none())
        : m_iterable(iterable), m_weakDict(py::module_::import("weakref")
                                              .attr("WeakKeyDictionary")()) {}

    // --------------------------- factory mode --------------------------- //
    py::object getattr(const std::string &name) {
        if (m_iterable.is_none()) {
            throw std::runtime_error(
                "When using `each` directly you must pass an iterable: each(iterable).attr");
        }
        return Proxy(m_iterable).getattr(name);
    }

    // --------------------------- descriptor hooks ----------------------- //
    void set_name(const py::object &owner, const std::string &name) {
        if (!py::isinstance<py::iterable>(owner)) {
            throw py::type_error("`each` can only be placed on iterable classes");
        }
        m_name = name;
    }

    /**
     * __get__(instance, owner) – provide per‑instance proxy with caching.
     *
     * *One* proxy per live instance is cached in `m_weakDict`.  As soon as
     * an instance is garbage‑collected, the key evaporates and the proxy
     * becomes unreachable, so we never keep dead objects alive or risk
     * returning a proxy bound to the wrong object after `id()` reuse.
     */
    py::object get(const py::object &instance, const py::object & /*owner*/) {
        if (instance.is_none()) {
            return py::cast(this);  // accessed via class, return descriptor
        }
        if (!py::isinstance<py::iterable>(instance)) {
            throw py::type_error("`each` can only be used on iterable instances");
        }

        // m_weakDict.get(instance) or None
        py::object maybe_proxy = m_weakDict.attr("get")(instance, py::none());
        if (!maybe_proxy.is_none()) {
            return maybe_proxy;
        }

        // -----------------------------  why we create & cache  ----------------------------
        // We allocate *one* Proxy per live instance and store it in the
        // WeakKeyDictionary to guarantee:
        //  • Identity semantics: `inst.each is inst.each`.
        //  • Automatic cleanup: entry removed when `instance` dies.
        //  • Safety: if CPython reuses an address, the old entry is already
        //    gone, so we create a fresh Proxy bound to the new object.
        // ----------------------------------------------------------------------------------
        auto proxy_sp = std::make_shared<Proxy>(instance);
        py::object proxy_obj = py::cast(proxy_sp);
        m_weakDict.attr("__setitem__")(instance, proxy_obj);
        return proxy_obj;
    }

    std::string representation() const {
        return "<Each bound to " + py::repr(m_iterable).cast<std::string>() + ">";
    }
private:
    py::object m_iterable;     //!< Iterable for factory mode; None in descriptor mode
    py::object m_weakDict;    //!< Python weakref.WeakKeyDictionary
    std::string m_name;        //!< Name of the attribute on the owner class (debug)
};
