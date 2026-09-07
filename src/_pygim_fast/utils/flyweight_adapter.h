#pragma once
// utils/flyweight_adapter.h — one live Python object per dense id, held weakly.
//
// ADAPTER layer (pybind11). `weak_slots<T>` maps a 32-bit id (a table row, an
// interner id) to the Python object currently wrapping that value, if any:
// one weak reference per id, so the holder pins nothing — an object dies with
// its last owner and the slot is refilled on the next request. Lookup is an
// array index, no hashing. Every object handed out is stamped with the
// holder's (owner, slot) token (utils/flyweight.h) and `row_of()` validates a
// token by checking that the slot's referent IS the asking object, so a token
// copied into another value is harmless.
//
// pybind11 instances are weak-referenceable by default (tp_weaklistoffset is
// set on the shared base), so any bound class qualifies. Mutation happens on
// the thread holding the GIL; the destructor runs under it too (pybind11
// destroys instances there) — which is why a weak_slots must be owned by a
// Python-visible object, never by a static.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>

#include "flyweight.h"

namespace pygim::adapter {

namespace py = pybind11;

namespace detail {
// The referent of a weakref as a new reference, or a null object once it died.
inline py::object weak_referent(PyObject* wr) {
#if PY_VERSION_HEX >= 0x030D0000
    PyObject* obj = nullptr;
    const int rc = PyWeakref_GetRef(wr, &obj);   // 1 alive (new ref), 0 dead, -1 error
    if (rc < 0) throw py::error_already_set();
    return rc == 1 ? py::reinterpret_steal<py::object>(obj) : py::object();
#else
    PyObject* obj = PyWeakref_GetObject(wr);   // borrowed; Py_None once dead
    if (!obj) throw py::error_already_set();
    return obj == Py_None ? py::object() : py::reinterpret_borrow<py::object>(obj);
#endif
}
}  // namespace detail

template <flyweight::stamped T>
class weak_slots {
public:
    using id_type = std::uint32_t;
    static constexpr id_type none = std::numeric_limits<id_type>::max();

    weak_slots() : m_owner(next_owner()) {}
    weak_slots(const weak_slots&) = delete;
    weak_slots& operator=(const weak_slots&) = delete;
    weak_slots(weak_slots&& o) noexcept : m_slots(std::move(o.m_slots)), m_owner(o.m_owner) {}
    weak_slots& operator=(weak_slots&&) = delete;
    ~weak_slots() {
        for (PyObject* wr : m_slots) Py_XDECREF(wr);
    }

    [[nodiscard]] std::uint64_t owner() const noexcept { return m_owner; }

    // The live object for `id` (a new reference), or null.
    [[nodiscard]] py::object live(id_type id) const {
        if (id >= m_slots.size() || !m_slots[id]) return py::object();
        return detail::weak_referent(m_slots[id]);
    }
    // Records `obj` as the object for `id` and stamps it.
    void remember(id_type id, py::handle obj) {
        if (id >= m_slots.size()) m_slots.resize(static_cast<std::size_t>(id) + 1, nullptr);
        PyObject* wr = PyWeakref_NewRef(obj.ptr(), nullptr);
        if (!wr) throw py::error_already_set();
        Py_XDECREF(m_slots[id]);
        m_slots[id] = wr;
        py::cast<T&>(obj).set_interned({m_owner, id});
    }
    // The id of an object THIS holder handed out, validated through the slot;
    // `none` for anything else (another holder's object, a copied token).
    [[nodiscard]] id_type id_of(py::handle obj, const T& value) const {
        const flyweight::token t = value.interned();
        if (t.owner != m_owner || t.slot >= m_slots.size() || !m_slots[t.slot]) return none;
        const py::object referent = detail::weak_referent(m_slots[t.slot]);
        return referent && referent.ptr() == obj.ptr() ? t.slot : none;
    }

    [[nodiscard]] std::size_t live_count() const {
        std::size_t n = 0;
        for (PyObject* wr : m_slots) {
            if (wr && detail::weak_referent(wr)) ++n;
        }
        return n;
    }
    [[nodiscard]] std::size_t bytes() const noexcept { return m_slots.capacity() * sizeof(PyObject*); }
    void reserve(std::size_t n) { m_slots.reserve(m_slots.size() + n); }

private:
    [[nodiscard]] static std::uint64_t next_owner() noexcept {
        static std::uint64_t n = 0;
        return ++n;   // under the GIL
    }

    std::vector<PyObject*> m_slots;   // per id: a weakref (owned) or nullptr
    std::uint64_t m_owner;
};

}  // namespace pygim::adapter
