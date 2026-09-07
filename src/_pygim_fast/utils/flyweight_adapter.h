#pragma once
// utils/flyweight_adapter.h — one live Python object per dense id, held weakly.
//
// ADAPTER layer (pybind11). `weak_slots<T>` maps a 32-bit id (a table row, an
// interner id) to the Python object currently wrapping that value, if any:
// one weak reference per id, so the holder pins nothing — an object dies with
// its last owner and the slot is refilled on the next request. Lookup is an
// array index, no hashing. Every object handed out is stamped with the
// holder's (owner, slot) token (utils/flyweight.h), and `id_of()` validates a
// token by checking that the slot's referent IS the asking object — so a
// token copied into another value (copies carry it along) is harmless.
//
// Who uses it: pathlike's path_store (pathlike/adapter/path_store.h) keeps a
// `weak_slots<file>` beside its path_table, one slot per row. `object_for()`
// asks `live(row)` and, on a miss, wraps the row's value and `remember()`s it;
// `make()` interns a value and does the same for its row; `parent_of()` asks
// `row_of()` — the store's name for `id_of` — whether the object it was given
// is one the store handed out, and if so walks the table by row instead of
// rebuilding the parent value. The identity, lifetime and copied-token tests
// are tests/unittests/test_path_store.py.
//
// A worked example, used in the comments below — a `weak_slots<file>` that is
// the third holder made in this process, so its owner number is 3:
//
//     weak_slots<file> s;            // owner() -> 3, no slots yet
//     s.remember(70, obj);           // slot 70: a weakref to obj; obj's C++ value
//                                    // now carries the token {owner 3, slot 70}
//     s.live(70)                     -> obj, as a NEW reference, while anything holds it
//     del obj; gc.collect()          // Python: the last reference goes
//     s.live(70)                     -> null py::object (the weakref died)
//
//     s.id_of(obj, value)            -> 70    the object this holder made
//     s.id_of(other, other_value)    -> none  (a) stamped by another holder: owner != 3
//     s.id_of(raw, raw_value)        -> none  (b) a raw `file` whose value copied
//                                             {3, 70}: slot 70's referent is obj, not raw
//     s.id_of(obj, value)            -> none  (c) asked of a holder whose vector does not
//                                             reach slot 70 (a moved-from one: same owner,
//                                             no slots)
//     s.live_count()                 -> 1 while obj lives, 0 once it has died
//
// Why weak references work here: pybind11 instances are weak-referenceable
// by default (tp_weaklistoffset is set on the common instance base every
// bound class shares), so any bound T qualifies without opting in.
//
// GIL: every member touches Python reference counts, so all of them — the
// destructor included, which DECREFs every weakref — must run on the thread
// holding the GIL. pybind11 destroys instances under the GIL, so a weak_slots
// owned by a Python-visible object (path_store: a module attribute, a local,
// an IoC singleton) is destroyed correctly, while the interpreter is alive.
// A weak_slots owned by a static would instead be destroyed at process exit,
// after the interpreter is finalised or from a thread without the GIL —
// which is why the owner must be Python-visible, never a static.

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
/// The referent of the weakref `wr` as a NEW reference (a py::object holding
/// it), or a null py::object once the referent has died. Two branches, one
/// result:
///
///   3.13+   PyWeakref_GetRef hands back a new reference (or none, when the
///           referent is dead) — stolen straight into the py::object.
///   before  PyWeakref_GetObject hands back a BORROWED reference — Py_None
///           once the referent is dead — which is owned (INCREF'd) by the
///           py::object at once, so a caller can never see it die under them.
///
/// Throws error_already_set if `wr` is not a weakref (-1 / NULL).
/// Cost: one weakref dereference and one reference-count change. GIL.
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

/// One weak slot per dense id for objects wrapping a `T` — a value type that
/// can carry a flyweight token (utils/flyweight.h: `interned()` and
/// `set_interned()`).
template <flyweight::stamped T>
class weak_slots {
public:
    /// Ids are 32-bit: table rows, interner ids.
    using id_type = std::uint32_t;
    /// The answer of `id_of` for an object this holder did not hand out.
    static constexpr id_type none = std::numeric_limits<id_type>::max();

    // ── lifetime ──────────────────────────────────────────────────────────

    /// An empty holder with a fresh owner number (next_owner()) — the `owner`
    /// half of every token it will stamp. No slots yet. GIL (the counter).
    ///
    ///     weak_slots<file> s;   // owner() -> 3 when two holders were made before it
    weak_slots() : m_owner(next_owner()) {}
    /// Not copyable: two holders with the same owner number would both vouch
    /// for the same tokens, and a copy would have to re-own every weakref.
    weak_slots(const weak_slots&) = delete;
    weak_slots& operator=(const weak_slots&) = delete;
    /// Movable — pybind11 moves a `path_store()` rvalue into its instance —
    /// the slots and the owner number travel together. The moved-from holder
    /// keeps its owner number but has no slots, so it answers `none` to every
    /// token it once stamped (case (c) of the worked example).
    weak_slots(weak_slots&& o) noexcept : m_slots(std::move(o.m_slots)), m_owner(o.m_owner) {}
    /// Not move-assignable: the target's weakrefs would have to be released
    /// first, and nothing needs it.
    weak_slots& operator=(weak_slots&&) = delete;
    /// DECREFs every weakref (the referents are untouched: the holder never
    /// owned them). Cost: O(slots). GIL — see the header comment for why the
    /// owner must be a Python-visible object, never a static.
    ~weak_slots() {
        for (PyObject* wr : m_slots) Py_XDECREF(wr);
    }

    /// The holder's owner number.                                 s.owner() -> 3
    [[nodiscard]] std::uint64_t owner() const noexcept { return m_owner; }

    // ── the slot protocol ─────────────────────────────────────────────────
    // A reader asks `live(id)`; on a miss it wraps the value itself and
    // `remember(id, obj)`s the result (path_store.h: object_for, make). An
    // object that claims to be "row id of this holder" is checked with
    // `id_of` before its token is trusted (path_store.h: parent_of and the
    // other derived-paths-by-row entry points).

    /// The live object for `id` as a NEW reference, or a null py::object when
    /// the slot was never filled, is past the vector, or its referent died.
    /// No growth, no exception for an id past the vector.
    ///
    ///     s.live(70)      // obj while anything holds it; null after del obj; gc.collect()
    ///     s.live(9000)    // null (past the vector)
    ///
    /// Cost: an index and one weakref dereference (detail::weak_referent). GIL.
    [[nodiscard]] py::object live(id_type id) const {
        if (id >= m_slots.size() || !m_slots[id]) return py::object();
        return detail::weak_referent(m_slots[id]);
    }
    /// Records `obj` as the object for `id` — a weakref to it in slot `id`,
    /// replacing whatever was there (a dead weakref, or a previous object's,
    /// which is simply forgotten) — and stamps obj's C++ value with the token
    /// {owner, id}. The holder takes no strong reference: obj still dies with
    /// its last owner, and the next `live(id)` then misses.
    ///
    ///     s.remember(70, obj);   // slots grow to 71; slot 70 -> weakref(obj);
    ///                            // py::cast<file&>(obj).interned() == {3, 70}
    ///
    /// Throws error_already_set when obj cannot be weakly referenced (never
    /// for a pybind11 instance, see the header) or is not a T. Cost: a
    /// weakref allocation, plus a vector grow to `id + 1` when `id` is the
    /// highest so far. GIL.
    void remember(id_type id, py::handle obj) {
        if (id >= m_slots.size()) m_slots.resize(static_cast<std::size_t>(id) + 1, nullptr);
        PyObject* wr = PyWeakref_NewRef(obj.ptr(), nullptr);
        if (!wr) throw py::error_already_set();
        Py_XDECREF(m_slots[id]);
        m_slots[id] = wr;
        py::cast<T&>(obj).set_interned({m_owner, id});
    }
    /// The id of an object THIS holder handed out, validated through the
    /// slot; `none` for anything else. `value` is obj's C++ value (the caller
    /// has it already, so nothing is cast twice); its token is trusted only
    /// when it names this owner, its slot is within the vector and filled,
    /// and that slot's referent IS `obj`:
    ///
    ///     s.id_of(obj, value)           // 70
    ///     s.id_of(other, other_value)   // none: another holder's token (owner != 3)
    ///     s.id_of(raw, raw_value)       // none: raw's value copied {3, 70}, but slot 70
    ///                                   //       refers to obj — a copied token proves nothing
    ///     moved_from.id_of(obj, value)  // none: slot 70 is past its (empty) vector
    ///
    /// The (b) case is test_a_copied_token_in_a_raw_object_is_not_trusted:
    /// a raw `file` derives its parent by value, never by another object's row.
    /// Cost: a token compare, an index and one weakref dereference. GIL.
    [[nodiscard]] id_type id_of(py::handle obj, const T& value) const {
        const flyweight::token t = value.interned();
        if (t.owner != m_owner || t.slot >= m_slots.size() || !m_slots[t.slot]) return none;
        const py::object referent = detail::weak_referent(m_slots[t.slot]);
        return referent && referent.ptr() == obj.ptr() ? t.slot : none;
    }

    // ── accounting ────────────────────────────────────────────────────────

    /// How many slots hold a live object: every slot is visited and every
    /// weakref dereferenced — O(slots), for statistics (PathStore.stats()
    /// ["live"]), never for a hot path.
    ///
    ///     s.live_count()   // 1 while obj lives; 0 after del obj; gc.collect()
    [[nodiscard]] std::size_t live_count() const {
        std::size_t n = 0;
        for (PyObject* wr : m_slots) {
            if (wr && detail::weak_referent(wr)) ++n;
        }
        return n;
    }
    /// The bytes the slot vector holds: its capacity in pointers — exact, so
    /// a component can report its own size (PathStore.stats()["slot_bytes"]).
    /// The weakref objects themselves are the interpreter's, not counted.
    ///
    ///     s.bytes()   // 71 slots * 8 = 568 (with an exact-fit capacity)
    [[nodiscard]] std::size_t bytes() const noexcept { return m_slots.capacity() * sizeof(PyObject*); }
    /// Room for `n` MORE slots beyond the current size, so a burst of
    /// `remember`s (PathStore.reserve(n) before interning n paths) grows the
    /// vector once.
    void reserve(std::size_t n) { m_slots.reserve(m_slots.size() + n); }

private:
    /// The next owner number: a process-wide counter, read and bumped under
    /// the GIL (a holder is only ever constructed there), so two holders in
    /// one process never share a number and a token can never be mistaken
    /// for another holder's. Starts at 1 — 0 is flyweight::token's "never
    /// stamped".
    [[nodiscard]] static std::uint64_t next_owner() noexcept {
        static std::uint64_t n = 0;
        return ++n;   // under the GIL
    }

    std::vector<PyObject*> m_slots;   // per id: a weakref (owned) or nullptr
    std::uint64_t m_owner;
};

}  // namespace pygim::adapter
