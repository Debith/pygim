#pragma once
// each/adapter.h — `each`: an attribute read or a method call broadcast over the elements of an
// iterable, as a factory (`each(items).name`) or as a descriptor (`each = each()` in the body of
// an iterable class: `group.each.name`).
//
//     each([a, b]).value          -> [a.value, b.value]
//     each([a, b]).scale(2)       -> [a.scale(2), b.scale(2)]
//     group.each is group.each    -> True: one proxy per live instance
//
// A Proxy fans one name out: if the first element's attribute is callable it waits for the call
// (`__call__` then calls it on every element), otherwise it returns the list of values. A
// descriptor keeps one proxy per live instance in a table of its own, keyed by the instance: a
// weakref's callback evicts the entry as the instance dies, and the proxy holds the instance
// weakly too, so caching it never keeps the instance alive (#246). A generator is refused: the
// broadcast walks the elements twice.
//
// ADAPTER layer (pybind11).

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "../utils/adapter_utils.h"

namespace py = pybind11;

// ---------------------------------------------------------------------------
//                               Proxy helper
// ---------------------------------------------------------------------------
class Proxy : public std::enable_shared_from_this<Proxy> {
public:
    /// A proxy over `iterable`, held: each(iterable)'s.
    [[nodiscard]] static std::shared_ptr<Proxy> holding(py::object iterable) {
        return std::shared_ptr<Proxy>(new Proxy(std::move(iterable), py::none()));
    }
    /// A proxy over the object `weak` refers to, held weakly: a descriptor's, cached by its instance.
    [[nodiscard]] static std::shared_ptr<Proxy> watching(py::object weak) {
        return std::shared_ptr<Proxy>(new Proxy(py::none(), std::move(weak)));
    }

    /// __getattr__(name): every element's attribute `name`, as a list; or, when the first
    /// element's is callable, this proxy, ready for the call that broadcasts it. An element
    /// without the attribute is an AttributeError.
    py::object getattr(const std::string& name) {
        const py::object items = iterable();
        const py::str key(name);
        std::vector<py::object> collected;
        for (const py::handle& item : items) {
            py::object attr = optional_attribute(item, key);   // asked once
            if (!attr) attr = py::getattr(item, key);           // it has none: Python's own AttributeError, in its words
            if (PyCallable_Check(attr.ptr())) {
                m_method = name;   // method mode: __call__ comes next
                return py::cast(shared_from_this());
            }
            collected.emplace_back(std::move(attr));
        }
        return collected.empty() ? py::none() : py::cast(collected);
    }

    /// __call__(*args, **kwargs): the method named last called on every element, in order.
    py::list call(const py::args& args, const py::kwargs& kwargs) {
        if (!m_method) throw py::type_error("Proxy object is not in callable mode (missing attribute access)");
        const std::string method = std::exchange(m_method, std::nullopt).value();   // once: no accidental reuse
        py::list results;
        for (const py::handle& item : iterable()) results.append(item.attr(method.c_str())(*args, **kwargs));
        return results;
    }

    [[nodiscard]] std::string representation() const {
        const py::object items = m_weak.is_none() ? m_iterable : m_weak();
        return "<Each-proxy bound to " + py::repr(items).cast<std::string>() + ">";
    }

private:
    Proxy(py::object iterable, py::object weak) : m_iterable(std::move(iterable)), m_weak(std::move(weak)) {}

    // What it broadcasts over: the iterable held, or the one watched while it lives.
    [[nodiscard]] py::object iterable() const {
        if (m_weak.is_none()) return m_iterable;
        py::object items = m_weak();
        if (items.is_none()) throw py::value_error("the instance this each-proxy was bound to is gone");
        return items;
    }

    py::object m_iterable;                 // each(iterable)'s, held; None for a descriptor's
    py::object m_weak;                     // a descriptor's instance, by a plain weakref; None otherwise
    std::optional<std::string> m_method;   // the callable attribute read last, until it is called
};

// ---------------------------------------------------------------------------
//                             Each helper / descriptor
// ---------------------------------------------------------------------------
class Each {
public:
    /// Factory mode (`each(iterable)`), or a descriptor (`each()`: iterable None).
    explicit Each(const py::object& iterable = py::none()) : m_iterable(iterable) {}

    // --------------------------- factory mode --------------------------- //

    py::object getattr(const std::string& name) {
        if (m_iterable.is_none()) throw std::runtime_error("Direct use: call each(iterable).attr");
        if (pygim::is_dunder(name)) throw py::attribute_error("Cannot access dunder attributes with `each`");
        return Proxy::holding(m_iterable)->getattr(name);
    }

    // --------------------------- descriptor hooks ----------------------- //

    /// __set_name__: the owner's instances must be iterable, i.e. the class defines __iter__ (the
    /// class object itself is iterable only when its metaclass is).
    void set_name(const py::object& owner, const std::string& name) {
        if (!has_attribute(owner, interned<"__iter__">())) throw py::type_error("`each` can only be placed on iterable classes");
        m_name = name;
    }

    /// __get__(instance, owner): the descriptor through the class; else the instance's one proxy,
    /// made at its first read and kept while the instance lives.
    py::object get(const py::object& instance, const py::object& /*owner*/) {
        if (instance.is_none()) return py::cast(this);
        if (!py::isinstance<py::iterable>(instance)) throw py::type_error("`each` can only be used on iterable instances");
        // A generator is walked once: the broadcast walks twice (to probe, then to call or gather).
        if (is_generator(instance)) throw py::type_error("`each` cannot be used on generator instances");

        PyObject* key = instance.ptr();
        if (const auto known = m_proxies.find(key); known != m_proxies.end()) return known->second.proxy();
        // The table's weakref evicts the entry as the instance dies, before its address can be
        // reused; only the table holds it, so its callback never outlives this descriptor. The
        // proxy watches the instance through a plain weakref of its own.
        const py::cpp_function evict([this, key](const py::handle&) { m_proxies.erase(key); });
        const py::object evicting = steal(PyWeakref_NewRef(key, evict.ptr()));   // TypeError for an instance with no weakref slot
        const py::object plain = steal(PyWeakref_NewRef(key, nullptr));
        py::object proxy = py::cast(Proxy::watching(plain));
        m_proxies.emplace(key, cached(evicting, proxy));
        return proxy;
    }

    [[nodiscard]] std::string representation() const {
        return "<Each bound to " + py::repr(m_iterable).cast<std::string>() + ">";
    }

private:
    // An instance's proxy, and the weakref whose callback evicts it.
    class cached {
    public:
        cached(py::object evicting, py::object proxy) noexcept : m_evicting(std::move(evicting)), m_proxy(std::move(proxy)) {}
        [[nodiscard]] const py::object& proxy() const noexcept { return m_proxy; }

    private:
        py::object m_evicting;   // kept for its callback
        py::object m_proxy;
    };

    [[nodiscard]] static py::object steal(PyObject* made) {
        if (made == nullptr) throw py::error_already_set();
        return py::reinterpret_steal<py::object>(made);
    }

    py::object m_iterable;                              // factory mode's; None for a descriptor
    std::unordered_map<PyObject*, cached> m_proxies;    // a descriptor's: each live instance's proxy
    std::string m_name;                                 // the owner's attribute it is (for messages)
};
