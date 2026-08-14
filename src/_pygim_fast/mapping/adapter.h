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

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "merge.h"

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
using PyMap = gimmap<PyFlat, mutable_trait, merge_trait<std::string>>;

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

struct pyg_frozen : pyg_gimmap {
    PyFrozenMap map;

    explicit pyg_frozen(PyFrozenMap m) : map(std::move(m)) {}

    [[nodiscard]] py::object getitem(py::handle key) const {
        const py::object* value = map.storage().find(require_str_key(key));
        if (value == nullptr) throw py::key_error("key not found");
        return *value;
    }
    [[nodiscard]] py::object get(py::handle key, py::object fallback) const {
        const py::object* value = map.storage().find(require_str_key(key));
        return value == nullptr ? std::move(fallback) : *value;
    }
    [[nodiscard]] bool contains(py::handle key) const {
        return py::isinstance<py::str>(key) &&
               map.storage().find(py::str(key).cast<std::string>()) != nullptr;
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
        py::dict d(initial);
        for (const auto& item : d) {
            map.set(require_str_key(item.first),
                    py::reinterpret_borrow<py::object>(item.second));
        }
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
        return py::isinstance<py::str>(key) &&
               map.storage().find(py::str(key).cast<std::string>()) != nullptr;
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
        py::object* existing = map.storage().find(k);
        if (existing == nullptr) {
            map.set(k, py::reinterpret_borrow<py::object>(value));
            return;
        }
        const py_folder f = folder();
        *existing = f.combine(f.resolve(key, *existing, value), *existing, value);
    }
    // Merge as an operation: fold into a NEW map, return it FROZEN (the
    // design's "a merged result IS a snapshot"). Accepts the gimdict family
    // or any mapping.
    [[nodiscard]] pyg_frozen merged(py::handle other) const;

    [[nodiscard]] pyg_frozen freeze() const {
        return pyg_frozen(PyFrozenMap(map.storage()));
    }
};

inline pyg_frozen pyg_gimdict::merged(py::handle other) const {
    pyg_gimdict work;
    work.map = map;                          // copies storage + strategy table
    work.type_strategies = type_strategies;
    work.explicit_default = explicit_default;

    py::dict incoming;
    if (py::isinstance<pyg_gimdict>(other)) {
        incoming = other.cast<const pyg_gimdict&>().to_dict();
    } else if (py::isinstance<pyg_frozen>(other)) {
        incoming = other.cast<const pyg_frozen&>().to_dict();
    } else if (PyMapping_Check(other.ptr())) {
        incoming = py::dict(py::reinterpret_borrow<py::object>(other));
    } else {
        throw py::type_error("can only merge with a mapping");
    }
    for (const auto& item : incoming) work.merge_in(item.first, item.second);
    return work.freeze();
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
    std::string engine = "auto";
    if (kwargs.contains("frozen")) {
        frozen = py::bool_(kwargs["frozen"]);
        PyDict_DelItemString(kwargs.ptr(), "frozen");
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
        .def("thaw", [](const pyg_frozen& self) {
                auto d = pyg_gimdict();
                for (const auto& [key, value] : self.map.items()) d.map.set(key, value);
                return d;
            },
            "Copy into a mutable gimdict.")
        .def("__getitem__", &pyg_frozen::getitem)
        .def("__contains__", &pyg_frozen::contains, py::is_operator())
        .def("__iter__", &pyg_frozen::iter_keys)
        .def("__len__", &pyg_frozen::size)
        .def("__eq__", &pyg_frozen::eq, py::is_operator())
        .def("__hash__", &pyg_frozen::hash);

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
        .def("__getitem__", &pyg_gimdict::getitem)
        .def("__setitem__", &pyg_gimdict::setitem)
        .def("__delitem__", &pyg_gimdict::delitem)
        .def("__iter__", &pyg_gimdict::iter_keys)
        .def("__len__", &pyg_gimdict::size)
        .def("__contains__", &pyg_gimdict::contains, py::is_operator())
        .def("__or__", &pyg_gimdict::merged, py::is_operator());

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
