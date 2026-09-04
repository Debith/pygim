// Compile-time proofs for mapping/storage.h, mapping/hash_storage.h and
// wiring/registry/core.h — the storage laws and the registry laws.
//
// This TU is part of the pygim.registry extension's SOURCES (ext.registry.toml):
// it is compiled by every build, so a violated invariant cannot produce a
// binary. It contributes no runtime code.
//
// The storage-law suite is the one from the mapping toolkit (core/gimdict-merge),
// so the flat engine keeps the same contract here as there. The registry suite
// proves that ONE RegistryCore serves both phases: over flat_storage it is a
// literal type that is built, deduplicated and queried in constant evaluation;
// over hash_storage it is the same code at run time.

#include "../../src/_pygim_fast/mapping/hash_storage.h"
#include "../../src/_pygim_fast/mapping/storage.h"
#include "../../src/_pygim_fast/wiring/registry/core.h"

#include <string_view>
#include <type_traits>

namespace {

using pygim::mapping::flat_storage;
using pygim::mapping::hash_storage;
using pygim::mapping::storage;
using pygim::core::DynamicRegistryCore;
using pygim::core::NoHooks;
using pygim::core::RegistryCore;
using pygim::core::StaticRegistryCore;

using FS = flat_storage<int, int>;

// ── storage laws (flat engine) — verbatim from the mapping toolkit ─────────
static_assert(storage<FS>);
static_assert(FS::ordered);

consteval bool find_hits_and_misses() {
    FS s{{2, 20}, {1, 10}};
    return s.find(1) != nullptr && *s.find(1) == 10 &&
           s.find(2) != nullptr && *s.find(2) == 20 &&
           s.find(3) == nullptr;
}
static_assert(find_hits_and_misses());

consteval bool insert_is_insert_or_assign() {
    FS s;
    s.insert(1, 10);
    s.insert(1, 11);   // same key: assigns, does not duplicate
    return s.size() == 1 && *s.find(1) == 11;
}
static_assert(insert_is_insert_or_assign());

consteval bool erase_removes_and_reports() {
    FS s{{1, 10}, {2, 20}};
    const bool hit = s.erase(1);
    const bool miss = s.erase(99);
    return hit && !miss && s.size() == 1 && s.find(1) == nullptr && s.find(2) != nullptr;
}
static_assert(erase_removes_and_reports());

consteval bool items_stay_key_sorted() {
    FS s;
    for (int key : {5, 1, 4, 2, 3}) s.insert(key, key * 10);
    int previous = 0;
    for (const auto& [key, value] : s.items()) {
        if (key <= previous || value != key * 10) return false;
        previous = key;
    }
    return s.size() == 5;
}
static_assert(items_stay_key_sorted());

consteval bool clear_empties() {
    FS s{{1, 10}};
    s.clear();
    return s.empty() && s.size() == 0 && s.find(1) == nullptr;
}
static_assert(clear_empties());

// assign_bulk sorts with std::stable_sort, constexpr only from C++26 (P2562):
// proven where the library supports it, exercised at run time everywhere.
#if defined(__cpp_lib_constexpr_algorithms) && __cpp_lib_constexpr_algorithms >= 202306L
consteval bool bulk_assign_keeps_last_write() {
    FS s{{9, 90}};
    s.assign_bulk({{3, 30}, {1, 10}, {3, 31}, {2, 20}});   // replaces; later write to 3 wins
    return s.size() == 3 && *s.find(3) == 31 && s.find(9) == nullptr && s.items()[0].first == 1;
}
static_assert(bulk_assign_keeps_last_write());
#endif

// ── the hashed engine has the same surface; only its ordering promise differs ─
static_assert(storage<hash_storage<int, int>>);
static_assert(!hash_storage<int, int>::ordered);
static_assert(storage<hash_storage<std::string_view, const int*>>);

// ── registry laws, both phases ─────────────────────────────────────────────
using Static = StaticRegistryCore<std::string_view, int>;
using Dynamic = DynamicRegistryCore<std::string_view, int>;

// The static registry is a literal type (the consteval proofs below are the
// test of that); the dynamic one is the same template over the hashed engine.
static_assert(Static::ordered && !Dynamic::ordered);
static_assert(std::is_same_v<Static::storage_type, flat_storage<std::string_view, int>>);
static_assert(std::is_same_v<Dynamic::storage_type, hash_storage<std::string_view, int>>);

consteval Static three() {
    Static r;
    r.register_or_override("yaml", 1, false);
    r.register_or_override("json", 2, false);
    r.register_value("toml", 3);
    return r;
}

consteval bool registers_and_finds() {
    const Static r = three();
    return r.size() == 3 && r.contains("json") && !r.contains("csv") &&
           r.try_get_const("toml") != nullptr && *r.try_get_const("toml") == 3 && r.try_get_const("csv") == nullptr;
}
static_assert(registers_and_finds());

consteval bool keys_are_sorted_on_the_flat_engine() {
    const Static r = three();
    const auto k = r.keys();
    return k.size() == 3 && k[0] == "json" && k[1] == "toml" && k[2] == "yaml";
}
static_assert(keys_are_sorted_on_the_flat_engine());

consteval bool register_value_never_replaces() {
    Static r = three();
    r.register_value("yaml", 9);   // silently keeps the first registration
    return *r.try_get_const("yaml") == 1;
}
static_assert(register_value_never_replaces());

consteval bool override_replaces_in_place() {
    Static r = three();
    r.register_or_override("yaml", 9, true);
    r.upsert_value("json", 8);
    return *r.try_get_const("yaml") == 9 && *r.try_get_const("json") == 8 && r.size() == 3;
}
static_assert(override_replaces_in_place());

consteval bool try_get_is_mutable_and_hook_free() {
    Static r = three();
    if (int* v = r.try_get("toml")) *v = 30;
    return *r.try_get_const("toml") == 30 && r.try_get("nope") == nullptr;
}
static_assert(try_get_is_mutable_and_hook_free());

consteval bool reserve_is_a_no_op_on_the_flat_engine() {
    Static r;
    r.reserve(64);
    return r.size() == 0;
}
static_assert(reserve_is_a_no_op_on_the_flat_engine());

// A duplicate registration throws; in constant evaluation that is a build
// error. With C++26 constexpr exceptions (GCC 16) the rejection itself is
// provable; older compilers prove it by refusing to build the offending line.
#if defined(__cpp_constexpr_exceptions) && __cpp_constexpr_exceptions >= 202411L
consteval bool duplicate_is_rejected() {
    Static r = three();
    try {
        r.register_or_override("yaml", 9, false);
    } catch (const std::runtime_error&) {
        return r.size() == 3 && *r.try_get_const("yaml") == 1;
    }
    return false;
}
static_assert(duplicate_is_rejected());

consteval bool override_of_missing_is_rejected() {
    Static r = three();
    try {
        r.register_or_override("csv", 4, true);
    } catch (const std::runtime_error&) {
        return !r.contains("csv");
    }
    return false;
}
static_assert(override_of_missing_is_rejected());
#endif

[[maybe_unused]] constexpr bool kRegistryProofsCompiled = true;

}  // namespace
