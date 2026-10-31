#pragma once
// similarity/names.h — what a name stands for: imports, aliases and
// module-level definitions resolved to fully qualified names, statically.
//
// CORE layer: pybind-free and constexpr, proven in
// tests/static/similarity_proofs.cpp. Both frontends use it: Python's
// `import` / `from ... import` and module-level definitions
// (python_source.h), C++'s namespace aliases and using-declarations
// (cpp_source.h).
//
// Why. The same call can be spelled many ways:
//
//     import os                         ->  os.path.join(p)
//     from os import path               ->  path.join(p)
//     import os.path as osp             ->  osp.join(p)
//     from os.path import join as j     ->  j(p)
//
// A frontend binds each local spelling to what it names (`osp` ->
// "os.path", `j` -> "os.path.join") and emits every reference by its
// resolved name, so all four become one tree: a `Ref` "os" under
// attributes "path" and "join".
//
// Nothing is imported or executed: resolution is static, and so as
// deterministic as the rest. What cannot be decided statically is left as
// written, never guessed:
//
//   - a name bound to two different targets in one scope (`try: import ujson
//     as json` / `except ImportError: import json`) is ambiguous;
//   - a runtime alias (os.path IS posixpath) is not followed;
//   - a relative import with no known package keeps its dots (".m.n").
//
// A worked example, used in the comments below:
//
//     binding_table::builder b;
//     b.bind("osp", "os.path");     b.bind("j", "os.path.join");
//     b.bind("json", "ujson");      b.bind("json", "json");      // two targets
//     const binding_table t = std::move(b).build();
//     t.resolve("osp")    -> "os.path"
//     t.resolve("json")   -> none (ambiguous)
//     t.resolve("x")      -> none (unbound)
//
//     relative_module("pkg.sub", 1, "m")   -> "pkg.sub.m"      (from .m import ...)
//     relative_module("pkg.sub", 2, "")    -> "pkg"            (from .. import ...)
//     relative_module("pkg", 3, "m")       -> none             (beyond the top)

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../mapping/storage.h"

namespace pygim::similarity {

/// Local names bound to fully qualified names, for one scope. A name bound
/// twice to the same target is one binding; to two targets, ambiguous (it
/// resolves to nothing). Built by a builder, which takes bindings in any order
/// and sorts them once: a table is a flat_storage (mapping/storage.h), looked
/// up by binary search with no string made.
///
///     binding_table::builder b;
///     b.bind("osp", "os.path");
///     const binding_table t = std::move(b).build();
///
/// Cost: bind O(1) amortised, build O(n log n), resolve O(log n). Binding into
/// a sorted table one name at a time cost O(n) a name: O(n²) for a module.
class binding_table {
    // What one local name stands for, until a second, different binding makes it ambiguous.
    class target {
    public:
        constexpr target() = default;
        constexpr explicit target(std::string_view qualified) : m_qualified(qualified) {}

        /// What it stands for: none once ambiguous.
        [[nodiscard]] constexpr std::optional<std::string_view> qualified() const noexcept {
            return m_ambiguous ? std::nullopt : std::optional<std::string_view>(m_qualified);
        }
        /// Bound once more: to another target, it is ambiguous for good.
        constexpr void bind_again(const target& other) noexcept { m_ambiguous = m_ambiguous || other.m_ambiguous || other.m_qualified != m_qualified; }

    private:
        std::string m_qualified;
        bool m_ambiguous = false;
    };
    using entries = mapping::flat_storage<std::string, target>;

public:
    /// Collects a scope's bindings, in any order.
    class builder {
    public:
        constexpr void bind(std::string_view local, std::string_view qualified) { m_bound.emplace_back(std::string(local), target(qualified)); }
        /// The table, every name's bindings merged.
        [[nodiscard]] constexpr binding_table build() && { return binding_table(std::move(m_bound)); }
        /// The table of what is bound so far; binding goes on.
        [[nodiscard]] constexpr binding_table built() const { return binding_table(m_bound); }

    private:
        std::vector<entries::item_type> m_bound;
    };

    constexpr binding_table() = default;

    /// What `local` stands for: none when unbound or ambiguous.
    [[nodiscard]] constexpr std::optional<std::string_view> resolve(std::string_view local) const {
        const target* t = m_entries.find(local);
        return t ? t->qualified() : std::nullopt;
    }
    /// Whether `local` has any binding here, resolvable or not — an inner
    /// scope's binding hides an outer one even when it is ambiguous.
    [[nodiscard]] constexpr bool binds(std::string_view local) const { return m_entries.find(local) != nullptr; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_entries.size(); }

private:
    constexpr explicit binding_table(std::vector<entries::item_type> bound) {
        std::sort(bound.begin(), bound.end(), [](const auto& a, const auto& b) { return a.first < b.first; });   // a merge in any order is one
        std::vector<entries::item_type> merged;
        for (auto& b : bound) {
            if (!merged.empty() && merged.back().first == b.first) merged.back().second.bind_again(b.second);
            else merged.push_back(std::move(b));
        }
        m_entries.assign_bulk(std::move(merged));
    }

    entries m_entries;
};
static_assert([] {   // the worked example above
    binding_table::builder b;
    b.bind("osp", "os.path");
    b.bind("json", "ujson");
    b.bind("json", "json");
    const binding_table t = std::move(b).build();
    return t.resolve("osp") == "os.path" && !t.resolve("json") && t.binds("json") && !t.resolve("x") && t.size() == 2;
}());

/// The absolute module a relative import names: `level` dots in the package
/// `package` (the importing module's package), then `module` (may be empty:
/// `from .. import x`). None when the dots climb above the top package or
/// the package is unknown (empty).
[[nodiscard]] constexpr std::optional<std::string> relative_module(std::string_view package, std::size_t level,
                                                                   std::string_view module) {
    if (level == 0) return std::string(module);
    if (package.empty()) return std::nullopt;
    std::string_view base = package;
    for (std::size_t up = 1; up < level; ++up) {
        const std::size_t dot = base.rfind('.');
        if (dot == std::string_view::npos) return std::nullopt;
        base = base.substr(0, dot);
    }
    std::string out(base);
    if (!module.empty()) {
        out += '.';
        out += module;
    }
    return out;
}
static_assert(relative_module("pkg.sub", 1, "m") == "pkg.sub.m" && relative_module("pkg.sub", 2, "") == "pkg" &&
              !relative_module("pkg", 3, "m"));   // from .m, from .., and beyond the top

/// A qualified name with `builtins.` removed: `builtins.len` and `len` are
/// one function.
[[nodiscard]] constexpr std::string_view without_builtins(std::string_view q) noexcept {
    constexpr std::string_view prefix = "builtins.";
    return q.starts_with(prefix) ? q.substr(prefix.size()) : q;
}
static_assert(without_builtins("builtins.len") == "len" && without_builtins("os.sep") == "os.sep");

/// `name` split at `sep` ("os.path.join" at "." -> os, path, join); an empty `sep` splits nothing
/// (it would find itself everywhere: the loop below never ended on one).
[[nodiscard]] constexpr std::vector<std::string_view> split_name(std::string_view name, std::string_view sep) {
    if (sep.empty()) return {name};
    std::vector<std::string_view> out;
    std::size_t from = 0;
    for (;;) {
        const std::size_t at = name.find(sep, from);
        out.push_back(name.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from));
        if (at == std::string_view::npos) return out;
        from = at + sep.size();
    }
}
static_assert(split_name("os.path.join", ".").size() == 3 && split_name("std::move", "::")[1] == "move" &&   // a Ref's chain; a qname
              split_name("a.b", "").size() == 1);

}  // namespace pygim::similarity
