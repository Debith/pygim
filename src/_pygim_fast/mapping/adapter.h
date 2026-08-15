#pragma once
// mapping/adapter.h — the Python value domain of the mapping toolkit.
//
// ADAPTER layer (pybind-dependent). The core owns per-key resolution and the
// arithmetic merge_combine; this header adds what only the Python domain has:
//
//  * py_folder — resolution extended with TYPE strategies (per-key > rhs type
//    > lhs type > explicit default > type default), and application over
//    py::object (PyNumber_Add, deep-dict recursion, list union) — ported from
//    the former PyGimDict, whose semantics test_gimdict.py pins unchanged.
//  * the adapter classes behind the gimdict() factory: pyg_frozen (Mapping,
//    hashable) and pyg_gimdict (MutableMapping, merge surface). Both derive
//    from the pyg_gimmap tag so isinstance(x, utils.gimmap) spans the family.
//
// Surface honesty: the frozen class has no set/del bindings at all — absent,
// not raising stand-ins. Keys are str (documents and registries have string
// keys); other key types are a TypeError, not a silent coercion.

#include <pybind11/pybind11.h>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "layers.h"

namespace pygim::mapping {

namespace py = pybind11;

// ── strategy spellings ──────────────────────────────────────────────────────

inline MergeStrategy parse_merge_strategy(const std::string& value) {
    if (value == "sum") return MergeStrategy::Sum;
    if (value == "max") return MergeStrategy::Max;
    if (value == "min") return MergeStrategy::Min;
    if (value == "replace") return MergeStrategy::Replace;
    if (value == "extend") return MergeStrategy::Extend;
    if (value == "union") return MergeStrategy::Union;
    if (value == "deep") return MergeStrategy::Deep;
    if (value == "multiply") return MergeStrategy::Multiply;
    throw py::value_error("invalid merge strategy: " + value);
}

inline std::string merge_strategy_name(MergeStrategy strategy) {
    switch (strategy) {
        case MergeStrategy::Sum: return "sum";
        case MergeStrategy::Max: return "max";
        case MergeStrategy::Min: return "min";
        case MergeStrategy::Replace: return "replace";
        case MergeStrategy::Extend: return "extend";
        case MergeStrategy::Union: return "union";
        case MergeStrategy::Deep: return "deep";
        case MergeStrategy::Multiply: return "multiply";
    }
    return "replace";
}

inline MergeStrategy parse_merge_strategy_obj(py::handle obj) {
    if (py::isinstance<py::str>(obj)) {
        return parse_merge_strategy(obj.cast<std::string>());
    }
    if (py::hasattr(obj, "__name__")) {
        return parse_merge_strategy(py::str(obj.attr("__name__")).cast<std::string>());
    }
    throw py::value_error("invalid merge strategy object");
}

inline std::string py_type_name(py::handle obj) {
    return py::str(py::type::of(obj).attr("__name__")).cast<std::string>();
}

inline MergeStrategy default_strategy_for_type(const std::string& type_name) {
    if (type_name == "int" || type_name == "float" || type_name == "bool") {
        return MergeStrategy::Sum;
    }
    return MergeStrategy::Replace;
}

// ── the Python value domain: resolution + application ───────────────────────

using PyFlat = flat_storage<std::string, py::object>;
using PyFrozenMap = gimmap<PyFlat>;
// One C++ map type backs BOTH Python classes: a layered map with zero
// contributions behaves exactly like a plain gimdict (base channel only), so
// the plain class simply does not EXPOSE the layered surface — the adapter
// inheritance (pyg_layered : pyg_gimdict) then works without dual state.
using PyMap = gimmap<PyFlat, mutable_trait, merge_trait<std::string>,
                     layer_trait<std::string, py::object>>;

// Deep recursion needs the resolver mid-application, so resolution and
// application travel together. Order (pinned by test_gimdict.py): per-key
// (from the core trait's table) > type-of-rhs > type-of-lhs > explicit
// default > type default of rhs.
struct py_folder {
    const PyMap* map;                                             // per-key table
    const std::unordered_map<std::string, MergeStrategy>* type_strategies;
    const std::optional<MergeStrategy>* explicit_default;

    [[nodiscard]] MergeStrategy resolve(py::handle key, py::handle lhs, py::handle rhs) const {
        if (map != nullptr && py::isinstance<py::str>(key)) {
            if (const MergeStrategy* s = map->key_strategy(py::str(key).cast<std::string>())) {
                return *s;
            }
        }
        const auto rhs_type = py_type_name(rhs);
        if (auto it = type_strategies->find(rhs_type); it != type_strategies->end()) {
            return it->second;
        }
        if (auto it = type_strategies->find(py_type_name(lhs)); it != type_strategies->end()) {
            return it->second;
        }
        if (explicit_default->has_value()) return **explicit_default;
        return default_strategy_for_type(rhs_type);
    }

    [[nodiscard]] py::object combine(MergeStrategy strategy, py::handle lhs,
                                     py::handle rhs) const {
        switch (strategy) {
            case MergeStrategy::Replace:
                return py::reinterpret_borrow<py::object>(rhs);
            case MergeStrategy::Sum:
            case MergeStrategy::Extend: {
                // Extend rides PyNumber_Add: list/tuple/str concatenate there too.
                PyObject* out = PyNumber_Add(lhs.ptr(), rhs.ptr());
                if (out == nullptr) {
                    throw py::type_error("sum/extend strategy failed for incompatible values");
                }
                return py::reinterpret_steal<py::object>(out);
            }
            case MergeStrategy::Multiply: {
                PyObject* out = PyNumber_Multiply(lhs.ptr(), rhs.ptr());
                if (out == nullptr) {
                    throw py::type_error("multiply strategy failed for incompatible values");
                }
                return py::reinterpret_steal<py::object>(out);
            }
            case MergeStrategy::Max:
                return py::module_::import("builtins").attr("max")(lhs, rhs);
            case MergeStrategy::Min:
                return py::module_::import("builtins").attr("min")(lhs, rhs);
            case MergeStrategy::Union:
                return combine_union(lhs, rhs);
            case MergeStrategy::Deep:
                return combine_deep(lhs, rhs);
        }
        return py::reinterpret_borrow<py::object>(rhs);
    }

private:
    // Order-preserving union of lists; non-lists fall back to replace.
    [[nodiscard]] static py::object combine_union(py::handle lhs, py::handle rhs) {
        if (!py::isinstance<py::list>(lhs) || !py::isinstance<py::list>(rhs)) {
            return py::reinterpret_borrow<py::object>(rhs);
        }
        py::list merged;
        for (const auto& item : py::reinterpret_borrow<py::list>(lhs)) merged.append(item);
        for (const auto& item : py::reinterpret_borrow<py::list>(rhs)) {
            if (!merged.contains(item)) merged.append(item);
        }
        return merged;
    }

    // Recursive mapping merge: shared keys resolve through this folder, so
    // nested dicts keep deep-merging and numeric leaves keep stacking.
    [[nodiscard]] py::object combine_deep(py::handle lhs, py::handle rhs) const {
        if (!py::isinstance<py::dict>(lhs) || !py::isinstance<py::dict>(rhs)) {
            return py::reinterpret_borrow<py::object>(rhs);
        }
        py::dict out;
        for (const auto& item : py::reinterpret_borrow<py::dict>(lhs)) {
            out[item.first] = item.second;
        }
        for (const auto& item : py::reinterpret_borrow<py::dict>(rhs)) {
            if (!out.contains(item.first)) {
                out[item.first] = item.second;
                continue;
            }
            auto l = py::reinterpret_borrow<py::object>(out[item.first]);
            auto r = py::reinterpret_borrow<py::object>(item.second);
            out[item.first] = combine(resolve(item.first, l, r), l, r);
        }
        return out;
    }
};

// ── adapter classes ─────────────────────────────────────────────────────────

inline std::string require_str_key(py::handle key) {
    if (!py::isinstance<py::str>(key)) {
        throw py::type_error("gimdict keys must be str; got " + py_type_name(key));
    }
    return py::str(key).cast<std::string>();
}

// Family tag: isinstance(x, utils.gimmap) spans everything the factory makes.
struct pyg_gimmap {};

struct pyg_gimdict;
struct pyg_layered;
inline py::dict mapping_items(py::handle other);

struct pyg_frozen : pyg_gimmap {
    PyFrozenMap map;
    // Merge configuration carried ACROSS the freeze: a merged result must
    // still know how to keep merging (chained assembly: acc = acc | frag),
    // and thaw() must restore what freeze() saw.
    flat_storage<std::string, MergeStrategy> key_strategies;
    std::unordered_map<std::string, MergeStrategy> type_strategies;
    std::optional<MergeStrategy> explicit_default;

    explicit pyg_frozen(PyFrozenMap m) : map(std::move(m)) {}
    pyg_frozen(PyFrozenMap m, flat_storage<std::string, MergeStrategy> keyed,
               std::unordered_map<std::string, MergeStrategy> typed,
               std::optional<MergeStrategy> fallback)
        : map(std::move(m)),
          key_strategies(std::move(keyed)),
          type_strategies(std::move(typed)),
          explicit_default(std::move(fallback)) {}

    [[nodiscard]] py::object getitem(py::handle key) const {
        const py::object* value = map.storage().find(require_str_key(key));
        if (value == nullptr) throw py::key_error("key not found");
        return *value;
    }
    // Family-consistent get(): the historical no-default-RAISES quirk holds on
    // both sides of a freeze, so the same call can't change meaning mid-chain.
    [[nodiscard]] py::object get(py::handle key, py::object fallback) const {
        const py::object* value = map.storage().find(require_str_key(key));
        if (value != nullptr) return *value;
        if (fallback.is_none()) throw py::key_error("key not found");
        return fallback;
    }
    [[nodiscard]] bool contains(py::handle key) const {
        // require_str_key: non-str keys are a TypeError on the whole surface,
        // `in` included — never a silent False.
        return map.storage().find(require_str_key(key)) != nullptr;
    }
    [[nodiscard]] std::size_t size() const { return map.size(); }
    [[nodiscard]] py::dict to_dict() const {
        py::dict out;
        for (const auto& [key, value] : map.items()) out[py::str(key)] = value;
        return out;
    }
    [[nodiscard]] py::object iter_keys() const {
        py::list keys;
        for (const auto& [key, value] : map.items()) keys.append(py::str(key));
        return py::iter(keys);
    }
    // Hashable like tuple: raises if any value is unhashable. items() is
    // key-sorted (flat engine), so equal content hashes equally.
    [[nodiscard]] std::size_t hash() const {
        py::list pairs;
        for (const auto& [key, value] : map.items()) {
            pairs.append(py::make_tuple(py::str(key), value));
        }
        return py::hash(py::tuple(pairs));
    }
    [[nodiscard]] bool eq(py::handle other) const;
    // Defined below (need the mutable type complete):
    [[nodiscard]] pyg_gimdict thawed() const;
    [[nodiscard]] pyg_frozen merged(py::handle other) const;
};

struct pyg_gimdict : pyg_gimmap {
    PyMap map;
    std::unordered_map<std::string, MergeStrategy> type_strategies;
    std::optional<MergeStrategy> explicit_default;

    pyg_gimdict() = default;

    [[nodiscard]] py_folder folder() const {
        return {&map, &type_strategies, &explicit_default};
    }

    void fill_from(py::object initial) {
        if (initial.is_none()) return;
        if (!PyMapping_Check(initial.ptr())) {
            throw py::type_error("gimdict initializer must be a mapping");
        }
        // mapping_items handles the gimdict family natively (dict(x) would
        // mis-parse a keys()-less mapping); assign_bulk loads sorted in one
        // pass instead of n tail-shifting inserts.
        py::dict d = mapping_items(initial);
        std::vector<PyFlat::item_type> items;
        items.reserve(static_cast<std::size_t>(py::len(d)));
        for (const auto& item : d) {
            items.emplace_back(require_str_key(item.first),
                               py::reinterpret_borrow<py::object>(item.second));
        }
        map.storage().assign_bulk(std::move(items));
    }
    void set_type_strategies(const py::kwargs& kwargs) {
        for (const auto& item : kwargs) {
            type_strategies[py::str(item.first).cast<std::string>()] =
                parse_merge_strategy_obj(item.second);
        }
    }

    // ── mapping protocol (semantics pinned by test_gimdict.py) ──────────────
    [[nodiscard]] py::object getitem(py::handle key) const {
        const py::object* value = map.storage().find(require_str_key(key));
        if (value == nullptr) throw py::key_error("key not found");
        return *value;
    }
    void setitem(py::handle key, py::handle value) {
        map.set(require_str_key(key), py::reinterpret_borrow<py::object>(value));
    }
    void delitem(py::handle key) {
        if (!map.erase(require_str_key(key))) throw py::key_error("key not found");
    }
    // Historical quirk kept verbatim: get() with no default RAISES on a miss.
    [[nodiscard]] py::object get(py::handle key, py::object fallback) const {
        const py::object* value = map.storage().find(require_str_key(key));
        if (value != nullptr) return *value;
        if (fallback.is_none()) throw py::key_error("key not found");
        return fallback;
    }
    [[nodiscard]] bool contains(py::handle key) const {
        return map.storage().find(require_str_key(key)) != nullptr;
    }
    [[nodiscard]] std::size_t size() const { return map.size(); }
    [[nodiscard]] py::object iter_keys() const {
        py::list keys;
        for (const auto& [key, value] : map.items()) keys.append(py::str(key));
        return py::iter(keys);
    }
    [[nodiscard]] py::dict to_dict() const {
        py::dict out;
        for (const auto& [key, value] : map.items()) out[py::str(key)] = value;
        return out;
    }

    // ── strategy surface ────────────────────────────────────────────────────
    void set_strategy(py::handle key, py::handle strategy) {
        map.set_merge_strategy(require_str_key(key), parse_merge_strategy_obj(strategy));
    }
    void set_type_strategy(const std::string& type_name, py::handle strategy) {
        type_strategies[type_name] = parse_merge_strategy_obj(strategy);
    }
    [[nodiscard]] std::string type_strategy(const std::string& type_name) const {
        const auto it = type_strategies.find(type_name);
        if (it == type_strategies.end()) {
            return merge_strategy_name(default_strategy_for_type(type_name));
        }
        return merge_strategy_name(it->second);
    }
    void set_default_strategy(py::handle strategy) {
        explicit_default = parse_merge_strategy_obj(strategy);
    }
    [[nodiscard]] std::string default_strategy() const {
        return explicit_default.has_value() ? merge_strategy_name(*explicit_default)
                                            : "type-default";
    }

    // ── merge surfaces ──────────────────────────────────────────────────────
    void merge_in(py::handle key, py::handle value) {
        const std::string k = require_str_key(key);
        const py::object* existing = map.storage().find(k);
        if (existing == nullptr) {
            map.set(k, py::reinterpret_borrow<py::object>(value));
            return;
        }
        // Own a reference and DROP the pointer before combine(): the combine
        // can run arbitrary Python (a value's __add__), which may mutate this
        // very map and reallocate the storage under a held pointer.
        py::object current = *existing;
        const py_folder f = folder();
        py::object combined = f.combine(f.resolve(key, current, value), current, value);
        map.set(k, std::move(combined));       // re-find inside set: always valid
    }
    // Merge as an operation: fold into a NEW map, return it FROZEN (the
    // design's "a merged result IS a snapshot"). Accepts the gimdict family
    // or any mapping. The frozen result CARRIES the strategy configuration.
    [[nodiscard]] pyg_frozen merged(py::handle other) const;

    [[nodiscard]] pyg_frozen freeze() const {
        return pyg_frozen(PyFrozenMap(map.storage()), map.key_strategies(),
                          type_strategies, explicit_default);
    }
};

inline pyg_frozen pyg_gimdict::merged(py::handle other) const {
    pyg_gimdict work;
    work.map = map;                          // copies storage + strategy table
    work.type_strategies = type_strategies;
    work.explicit_default = explicit_default;
    for (const auto& item : mapping_items(other)) work.merge_in(item.first, item.second);
    return work.freeze();
}

// ── the layered variant: merge with MEMORY (provenance + undo) ──────────────
// Reads OBSERVE (base channel + contributions folded on demand); set/del keep
// writing the base channel through the inherited surface. Contribution
// strategies resolve at apply time: explicit > per-key > type-of-value >
// explicit default > type default.
struct pyg_layered : pyg_gimdict {
    [[nodiscard]] std::vector<std::string> all_keys() const {
        std::vector<std::string> keys;
        for (const auto& [key, value] : map.items()) keys.push_back(key);
        for (const auto& [key, layers] : map.layer_items()) {
            if (map.storage().find(key) == nullptr) keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    [[nodiscard]] py::object observe_key(const std::string& key) const {
        const py_folder f = folder();
        std::optional<py::object> acc;
        if (const py::object* base = map.storage().find(key)) acc = *base;   // copy
        // Fold over an OWNED copy of the contributions: combine() can run
        // arbitrary Python that mutates this map and reallocates the vector
        // a live pointer/iterator would dangle into.
        std::vector<std::remove_cvref_t<decltype(*map.contributions(key))>::value_type>
            layers_copy;
        if (const auto* layers = map.contributions(key)) layers_copy = *layers;
        for (const auto& c : layers_copy) {
            acc = acc.has_value() ? f.combine(c.strategy, *acc, c.value) : c.value;
        }
        if (!acc.has_value()) throw py::key_error("key not found");
        return *acc;
    }
    [[nodiscard]] py::object observed_getitem(py::handle key) const {
        return observe_key(require_str_key(key));
    }
    [[nodiscard]] py::object observed_get(py::handle key, py::object fallback) const {
        const std::string k = require_str_key(key);
        if (!map.holds(k)) {
            if (fallback.is_none()) throw py::key_error("key not found");
            return fallback;
        }
        return observe_key(k);
    }
    [[nodiscard]] bool observed_contains(py::handle key) const {
        return map.holds(require_str_key(key));
    }
    [[nodiscard]] std::size_t observed_size() const { return all_keys().size(); }
    [[nodiscard]] py::object observed_iter() const {
        py::list keys;
        for (const auto& key : all_keys()) keys.append(py::str(key));
        return py::iter(keys);
    }
    [[nodiscard]] py::dict observed_dict() const {
        py::dict out;
        for (const auto& key : all_keys()) out[py::str(key)] = observe_key(key);
        return out;
    }

    void apply(py::handle source, py::handle key, py::handle value, py::handle strategy) {
        const std::string k = require_str_key(key);
        MergeStrategy resolved = MergeStrategy::Replace;
        if (!strategy.is_none()) {
            resolved = parse_merge_strategy_obj(strategy);
        } else if (const MergeStrategy* per_key = map.key_strategy(k)) {
            resolved = *per_key;
        } else if (auto it = type_strategies.find(py_type_name(value));
                   it != type_strategies.end()) {
            resolved = it->second;
        } else if (explicit_default.has_value()) {
            resolved = *explicit_default;
        } else {
            resolved = default_strategy_for_type(py_type_name(value));
        }
        map.apply(py::str(source).cast<std::string>(), k, resolved,
                  py::reinterpret_borrow<py::object>(value));
    }
    void remove(py::handle source) { map.remove(py::str(source).cast<std::string>()); }
    [[nodiscard]] py::list sources(py::handle key) const {
        py::list out;
        for (const auto& s : map.sources(require_str_key(key))) out.append(py::str(s));
        return out;
    }
    [[nodiscard]] py::list footprint(py::handle source) const {
        py::list out;
        for (const auto& k : map.footprint(py::str(source).cast<std::string>())) {
            out.append(py::str(k));
        }
        return out;
    }

    // The observed state as a plain (base-channel) gimdict, strategies kept —
    // both snapshot() and functional merges route through this flattening.
    [[nodiscard]] pyg_gimdict flattened() const {
        pyg_gimdict flat;
        flat.map = map;                      // per-key strategies preserved
        flat.type_strategies = type_strategies;
        flat.explicit_default = explicit_default;
        for (const auto& key : all_keys()) flat.map.set(key, observe_key(key));
        return flat;
    }
    [[nodiscard]] pyg_frozen snapshot() const { return flattened().freeze(); }
    [[nodiscard]] pyg_frozen merged_observed(py::handle other) const {
        return flattened().merged(other);
    }
    // Layered whole-map merge: record other's items as contributions.
    void merge_from(py::handle other, py::handle source) {
        for (const auto& item : mapping_items(other)) {
            apply(source, item.first, item.second, py::none());
        }
    }
};

inline py::dict mapping_items(py::handle other) {
    if (py::isinstance<pyg_layered>(other)) {
        return other.cast<const pyg_layered&>().observed_dict();
    }
    if (py::isinstance<pyg_gimdict>(other)) {
        return other.cast<const pyg_gimdict&>().to_dict();
    }
    if (py::isinstance<pyg_frozen>(other)) {
        return other.cast<const pyg_frozen&>().to_dict();
    }
    if (PyMapping_Check(other.ptr())) {
        return py::dict(py::reinterpret_borrow<py::object>(other));
    }
    throw py::type_error("can only merge with a mapping");
}

// thaw(): one-shot sorted-storage copy, merge configuration RESTORED — the
// freeze/thaw round trip loses nothing.
inline pyg_gimdict pyg_frozen::thawed() const {
    pyg_gimdict d;
    d.map.storage() = map.storage();
    for (const auto& [key, strategy] : key_strategies.items()) {
        d.map.set_merge_strategy(key, strategy);
    }
    d.type_strategies = type_strategies;
    d.explicit_default = explicit_default;
    return d;
}

// Frozen results keep merging: chained assembly (acc = acc | frag | frag)
// works because the carried configuration travels through every fold.
inline pyg_frozen pyg_frozen::merged(py::handle other) const {
    return thawed().merged(other);
}

// lhs | self where lhs is a plain mapping: fold lhs as the base, then self's
// observed items in, under SELF's strategy configuration (the only one there
// is). Serves the __ror__ bindings for the whole family.
inline pyg_frozen fold_reversed(py::handle lhs, const py::dict& self_items,
                                const flat_storage<std::string, MergeStrategy>& keyed,
                                const std::unordered_map<std::string, MergeStrategy>& typed,
                                const std::optional<MergeStrategy>& fallback) {
    pyg_gimdict work;
    for (const auto& [key, strategy] : keyed.items()) {
        work.map.set_merge_strategy(key, strategy);
    }
    work.type_strategies = typed;
    work.explicit_default = fallback;
    py::dict base = mapping_items(lhs);
    std::vector<PyFlat::item_type> items;
    items.reserve(static_cast<std::size_t>(py::len(base)));
    for (const auto& item : base) {
        items.emplace_back(require_str_key(item.first),
                           py::reinterpret_borrow<py::object>(item.second));
    }
    work.map.storage().assign_bulk(std::move(items));
    for (const auto& item : self_items) work.merge_in(item.first, item.second);
    return work.freeze();
}

// Python's binary-operator protocol: an incompatible operand yields
// NotImplemented (so the other side's __ror__ gets its turn), never a throw.
inline bool merge_compatible(py::handle other) {
    return py::isinstance<pyg_gimmap>(other) || PyMapping_Check(other.ptr());
}
inline py::object not_implemented() {
    return py::reinterpret_borrow<py::object>(py::handle(Py_NotImplemented));
}

inline bool pyg_frozen::eq(py::handle other) const {
    if (py::isinstance<pyg_frozen>(other)) {
        return map == other.cast<const pyg_frozen&>().map;
    }
    if (PyMapping_Check(other.ptr())) {
        return to_dict().equal(py::dict(py::reinterpret_borrow<py::object>(other)));
    }
    return false;
}

// ── factory + registration ──────────────────────────────────────────────────

inline py::object gimdict_factory(py::object initial, py::kwargs kwargs) {
    bool frozen = false;
    bool layers = false;
    std::string engine = "auto";
    if (kwargs.contains("frozen")) {
        frozen = py::bool_(kwargs["frozen"]);
        PyDict_DelItemString(kwargs.ptr(), "frozen");
    }
    if (kwargs.contains("layers")) {
        layers = py::bool_(kwargs["layers"]);
        PyDict_DelItemString(kwargs.ptr(), "layers");
    }
    if (kwargs.contains("engine")) {
        engine = py::str(kwargs["engine"]).cast<std::string>();
        PyDict_DelItemString(kwargs.ptr(), "engine");
    }
    if (engine != "auto" && engine != "flat") {
        throw py::value_error("engine '" + engine +
                              "' is not available yet: backends are benchmark-gated "
                              "(see docs/design/mapping_toolkit.md); use 'flat'");
    }
    if (frozen && layers) {
        throw py::value_error("frozen and layers are exclusive: frozen is a RESULT — "
                              "take snapshot() of a layered map instead");
    }

    if (layers) {
        auto d = pyg_layered();
        d.set_type_strategies(kwargs);
        d.fill_from(std::move(initial));       // initial fills the base channel
        return py::cast(std::move(d));
    }
    auto d = pyg_gimdict();
    d.set_type_strategies(kwargs);
    d.fill_from(std::move(initial));
    if (frozen) return py::cast(d.freeze());
    return py::cast(std::move(d));
}

inline void register_mapping(py::module_& m) {
    auto family = py::class_<pyg_gimmap>(m, "gimmap",
        "Family base of the mapping toolkit: everything gimdict() makes is a gimmap.");

    auto frozen_cls = py::class_<pyg_frozen, pyg_gimmap>(m, "frozen_gimmap",
            "Frozen Mapping over the flat engine (key-sorted iteration); hashable "
            "like tuple. Made by gimdict(..., frozen=True), .freeze(), or merges.")
        .def("get", &pyg_frozen::get, py::arg("key"), py::arg("default") = py::none())
        .def("to_dict", &pyg_frozen::to_dict)
        .def("thaw", &pyg_frozen::thawed,
             "Copy into a mutable gimdict; merge configuration is restored.")
        .def("merge", &pyg_frozen::merged, py::arg("other"),
             "Fold other in under the carried strategies; returns a new frozen map.")
        // keys/values/items make the class a REAL mapping to CPython: dict(x)
        // and the ABC mixin methods route through keys(), without which the
        // iterator gets mis-parsed as a sequence of pairs.
        .def("keys", [](const pyg_frozen& self) {
                py::list out;
                for (const auto& [key, value] : self.map.items()) out.append(py::str(key));
                return out;
            })
        .def("values", [](const pyg_frozen& self) {
                py::list out;
                for (const auto& [key, value] : self.map.items()) out.append(value);
                return out;
            })
        .def("items", [](const pyg_frozen& self) {
                py::list out;
                for (const auto& [key, value] : self.map.items()) {
                    out.append(py::make_tuple(py::str(key), value));
                }
                return out;
            })
        .def("__getitem__", &pyg_frozen::getitem)
        .def("__contains__", &pyg_frozen::contains, py::is_operator())
        .def("__iter__", &pyg_frozen::iter_keys)
        .def("__len__", &pyg_frozen::size)
        .def("__eq__", &pyg_frozen::eq, py::is_operator())
        .def("__hash__", &pyg_frozen::hash)
        .def("__or__",
             [](const pyg_frozen& self, py::handle other) -> py::object {
                 if (!merge_compatible(other)) return not_implemented();
                 return py::cast(self.merged(other));
             },
             py::is_operator())
        .def("__ror__",
             [](const pyg_frozen& self, py::handle lhs) -> py::object {
                 if (!merge_compatible(lhs)) return not_implemented();
                 return py::cast(fold_reversed(lhs, self.to_dict(), self.key_strategies,
                                               self.type_strategies, self.explicit_default));
             },
             py::is_operator());

    auto gimdict_cls = py::class_<pyg_gimdict, pyg_gimmap>(m, "gimdict_type",
            "MutableMapping with merge strategies over the flat engine "
            "(key-sorted iteration). Construct via the gimdict() factory.")
        .def("set", &pyg_gimdict::setitem, py::arg("key"), py::arg("value"))
        .def("get", &pyg_gimdict::get, py::arg("key"), py::arg("default") = py::none())
        .def("contains", &pyg_gimdict::contains, py::arg("key"))
        .def("set_strategy", &pyg_gimdict::set_strategy, py::arg("key"), py::arg("strategy"))
        .def("set_type_strategy", &pyg_gimdict::set_type_strategy,
             py::arg("type_name"), py::arg("strategy"))
        .def("type_strategy", &pyg_gimdict::type_strategy, py::arg("type_name"))
        .def("default_strategy", &pyg_gimdict::default_strategy)
        .def("set_default_strategy", &pyg_gimdict::set_default_strategy, py::arg("strategy"))
        .def("merge_in", &pyg_gimdict::merge_in, py::arg("key"), py::arg("value"))
        .def("merge", &pyg_gimdict::merged, py::arg("other"))
        .def("freeze", &pyg_gimdict::freeze)
        .def("to_dict", &pyg_gimdict::to_dict)
        .def("keys", [](const pyg_gimdict& self) {
                py::list out;
                for (const auto& [key, value] : self.map.items()) out.append(py::str(key));
                return out;
            })
        .def("values", [](const pyg_gimdict& self) {
                py::list out;
                for (const auto& [key, value] : self.map.items()) out.append(value);
                return out;
            })
        .def("items", [](const pyg_gimdict& self) {
                py::list out;
                for (const auto& [key, value] : self.map.items()) {
                    out.append(py::make_tuple(py::str(key), value));
                }
                return out;
            })
        .def("__getitem__", &pyg_gimdict::getitem)
        .def("__setitem__", &pyg_gimdict::setitem)
        .def("__delitem__", &pyg_gimdict::delitem)
        .def("__iter__", &pyg_gimdict::iter_keys)
        .def("__len__", &pyg_gimdict::size)
        .def("__contains__", &pyg_gimdict::contains, py::is_operator())
        .def("__or__",
             [](const pyg_gimdict& self, py::handle other) -> py::object {
                 if (!merge_compatible(other)) return not_implemented();
                 return py::cast(self.merged(other));
             },
             py::is_operator())
        .def("__ror__",
             [](const pyg_gimdict& self, py::handle lhs) -> py::object {
                 if (!merge_compatible(lhs)) return not_implemented();
                 return py::cast(fold_reversed(lhs, self.to_dict(),
                                               self.map.key_strategies(),
                                               self.type_strategies, self.explicit_default));
             },
             py::is_operator());

    // Layered variant: reads observe (fold on demand); the inherited mutable
    // surface writes the base channel. isinstance-wise it IS a gimdict.
    py::class_<pyg_layered, pyg_gimdict>(m, "layered_gimmap",
            "gimdict with MEMORY: contributions are recorded per source and "
            "folded on read, so remove(source) brings the old value back and "
            "sources()/footprint() answer who touched what. Made by "
            "gimdict(..., layers=True); snapshot() -> frozen_gimmap.")
        .def("apply", &pyg_layered::apply, py::arg("source"), py::arg("key"),
             py::arg("value"), py::arg("strategy") = py::none())
        .def("remove", &pyg_layered::remove, py::arg("source"))
        .def("sources", &pyg_layered::sources, py::arg("key"))
        .def("footprint", &pyg_layered::footprint, py::arg("source"))
        .def("snapshot", &pyg_layered::snapshot)
        .def("freeze", &pyg_layered::snapshot)   // freeze == snapshot here
        .def("merge",
             [](const pyg_layered& self, py::handle other, py::handle source) {
                 if (source.is_none()) return py::cast(self.merged_observed(other));
                 auto copy = self;                 // functional: record on a copy
                 copy.merge_from(other, source);
                 return py::cast(std::move(copy));
             },
             py::arg("other"), py::arg("source") = py::none(),
             "merge(other) folds the observed state and returns it frozen; "
             "merge(other, source=...) records other's items as removable "
             "contributions from that source and returns the new layered map.")
        .def("merge_from", &pyg_layered::merge_from, py::arg("other"), py::arg("source"),
             "In-place: record other's items as contributions from source.")
        .def("get", &pyg_layered::observed_get, py::arg("key"), py::arg("default") = py::none())
        .def("contains", &pyg_layered::observed_contains, py::arg("key"))
        .def("to_dict", &pyg_layered::observed_dict)
        .def("keys", [](const pyg_layered& self) {
                py::list out;
                for (const auto& key : self.all_keys()) out.append(py::str(key));
                return out;
            })
        .def("values", [](const pyg_layered& self) {
                py::list out;
                for (const auto& key : self.all_keys()) out.append(self.observe_key(key));
                return out;
            })
        .def("items", [](const pyg_layered& self) {
                py::list out;
                for (const auto& key : self.all_keys()) {
                    out.append(py::make_tuple(py::str(key), self.observe_key(key)));
                }
                return out;
            })
        .def("__getitem__", &pyg_layered::observed_getitem)
        .def("__contains__", &pyg_layered::observed_contains, py::is_operator())
        .def("__iter__", &pyg_layered::observed_iter)
        .def("__len__", &pyg_layered::observed_size)
        .def("__or__",
             [](const pyg_layered& self, py::handle other) -> py::object {
                 if (!merge_compatible(other)) return not_implemented();
                 return py::cast(self.merged_observed(other));
             },
             py::is_operator())
        .def("__ror__",
             [](const pyg_layered& self, py::handle lhs) -> py::object {
                 if (!merge_compatible(lhs)) return not_implemented();
                 return py::cast(fold_reversed(lhs, self.observed_dict(),
                                               self.map.key_strategies(),
                                               self.type_strategies, self.explicit_default));
             },
             py::is_operator());

    // gimdict() is the FACTORY (the path() pattern): kwargs name the traits,
    // the returned type is the curated combo, isinstance(x, gimmap) spans all.
    m.def("gimdict", &gimdict_factory, py::arg("initial") = py::none(),
          "Make a toolkit mapping: gimdict({...}) -> MutableMapping with merge "
          "strategies; frozen=True -> frozen Mapping; engine= picks the backend; "
          "remaining kwargs are per-type strategies (int=max, str='replace').");

    py::module_ collections_abc = py::module_::import("collections.abc");
    collections_abc.attr("MutableMapping").attr("register")(gimdict_cls);
    collections_abc.attr("Mapping").attr("register")(frozen_cls);
}

}  // namespace pygim::mapping
