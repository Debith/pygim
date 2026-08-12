#pragma once
// pathlike/core.h — the pybind-free heart of `pygim.path`.
//
// A `file` is a std::filesystem::path that knows how to read and decode itself.
// The *decoder* is chosen at COMPILE TIME from the file extension: the extension
// -> engine table is constexpr, the lookup is constexpr, and the mapping is proven
// with static_assert below (see bindings.cpp for the runtime proof too). Adding a
// new format is a single-line edit to `kExtEngines`.
//
// This header carries no pybind11 and no YAML library, so it stays fast to compile
// and trivially testable; the engine glue that must speak Python lives in adapter.h.

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pygim::pathlike {

namespace fs = std::filesystem;

// The decoders `read()` can dispatch to. `Unknown` means "no engine resolved".
enum class Engine { Unknown, Yaml, Json };

// Compile-time extension -> optimal engine. The whole format registry is here.
inline constexpr std::array<std::pair<std::string_view, Engine>, 3> kExtEngines{{
    {".yaml", Engine::Yaml},
    {".yml", Engine::Yaml},
    {".json", Engine::Json},
}};

// The optimal engine for a file extension (leading dot, lower-case), decided at
// compile time whenever the extension is a constant. Unknown -> Engine::Unknown.
[[nodiscard]] constexpr Engine engine_for_ext(std::string_view ext) noexcept {
    for (const auto& [name, engine] : kExtEngines) {
        if (name == ext) return engine;
    }
    return Engine::Unknown;
}

// The engine a caller named explicitly (read(engine="yaml")); "" means "auto".
[[nodiscard]] constexpr Engine engine_from_name(std::string_view name) noexcept {
    if (name == "yaml" || name == "yml") return Engine::Yaml;
    if (name == "json") return Engine::Json;
    return Engine::Unknown;
}

[[nodiscard]] constexpr std::string_view engine_label(Engine e) noexcept {
    switch (e) {
        case Engine::Yaml: return "yaml";
        case Engine::Json: return "json";
        case Engine::Unknown: return "unknown";
    }
    return "unknown";
}

// Compile-time proof that the dispatch table is what we think it is.
static_assert(engine_for_ext(".yaml") == Engine::Yaml);
static_assert(engine_for_ext(".yml") == Engine::Yaml);
static_assert(engine_for_ext(".json") == Engine::Json);
static_assert(engine_for_ext(".txt") == Engine::Unknown);
static_assert(engine_from_name("yaml") == Engine::Yaml);
static_assert(engine_from_name("json") == Engine::Json);
static_assert(engine_from_name("") == Engine::Unknown);

namespace detail {
// ASCII lower-case of a short extension, so ".YAML" resolves like ".yaml".
// constexpr so the lower-case-then-lookup chain is provable at compile time.
[[nodiscard]] constexpr std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// ── Exhaustive compile-time proof of the format registry ──────────────────
// The spot checks above pin known entries; these validators sweep the WHOLE
// table, so a format added to kExtEngines later is proven automatically.
// consteval (not constexpr): they can never be compiled into runtime code.

// Every entry resolves to its declared engine, never Unknown, and keeps the
// key contract: leading dot, lower-case (ascii_lower() is a no-op on keys).
consteval bool table_entries_resolve() {
    for (const auto& [ext, eng] : kExtEngines) {
        if (eng == Engine::Unknown) return false;
        if (engine_for_ext(ext) != eng) return false;
        if (ext.size() < 2 || ext.front() != '.') return false;
        if (ascii_lower(ext) != ext) return false;
    }
    return true;
}

// Duplicate keys would make later entries silently unreachable (first match wins).
consteval bool table_has_no_duplicates() {
    for (std::size_t i = 0; i < kExtEngines.size(); ++i) {
        for (std::size_t j = i + 1; j < kExtEngines.size(); ++j) {
            if (kExtEngines[i].first == kExtEngines[j].first) return false;
        }
    }
    return true;
}

// engine_label() -> engine_from_name() round-trips for every reachable engine.
consteval bool labels_roundtrip() {
    for (const auto& [ext, eng] : kExtEngines) {
        if (engine_from_name(engine_label(eng)) != eng) return false;
    }
    return engine_label(Engine::Unknown) == std::string_view{"unknown"};
}

// The lower-case-then-lookup chain used by resolve_engine(), proven end to end.
consteval bool case_folds_before_lookup() {
    return engine_for_ext(ascii_lower(".YAML")) == Engine::Yaml &&
           engine_for_ext(ascii_lower(".Yml")) == Engine::Yaml &&
           engine_for_ext(ascii_lower(".JSON")) == Engine::Json;
}

// Near-misses stay Unknown: raw lookups are exact (case, dot, whole string).
consteval bool misses_stay_unknown() {
    return engine_for_ext("") == Engine::Unknown &&
           engine_for_ext(".") == Engine::Unknown &&
           engine_for_ext("yaml") == Engine::Unknown &&
           engine_for_ext(".yaml ") == Engine::Unknown &&
           engine_for_ext(".YAML") == Engine::Unknown &&
           engine_from_name("YAML") == Engine::Unknown &&
           engine_from_name(".yaml") == Engine::Unknown;
}
}  // namespace detail

static_assert(detail::table_entries_resolve());
static_assert(detail::table_has_no_duplicates());
static_assert(detail::labels_roundtrip());
static_assert(detail::case_folds_before_lookup());
static_assert(detail::misses_stay_unknown());
// Out-of-range enum values (reachable via static_cast) still label as "unknown".
static_assert(engine_label(static_cast<Engine>(42)) == std::string_view{"unknown"});

// A filesystem path that reads and decodes itself. Models os.PathLike (it exposes
// __fspath__ through the binding), so it drops straight into open(), Path(), etc.
// An engine may be pinned at construction; Engine::Unknown means "auto by
// extension". Derived paths (parent(), with_suffix(), ...) inherit the pin.
class file {
public:
    explicit file(fs::path p, Engine engine = Engine::Unknown)
        : m_path(std::move(p)), m_engine(engine) {}

    [[nodiscard]] const fs::path& path() const noexcept { return m_path; }

    // The engine pinned at construction; Engine::Unknown = auto by extension.
    [[nodiscard]] Engine pinned_engine() const noexcept { return m_engine; }

    // os.PathLike: the plain filesystem string.
    [[nodiscard]] std::string fspath() const { return m_path.string(); }

    // -- name components (pathlib parity: case preserved) -----------------
    [[nodiscard]] std::string name() const { return m_path.filename().string(); }
    [[nodiscard]] std::string stem() const { return m_path.stem().string(); }

    // Final extension including the dot (".gz"); "" if none. Case is preserved,
    // like pathlib — engine resolution lower-cases separately.
    [[nodiscard]] std::string suffix() const { return m_path.extension().string(); }

    // Every extension of the final component: "a.tar.gz" -> [".tar", ".gz"].
    // A leading-dot name (".bashrc") has no suffixes, as in pathlib.
    [[nodiscard]] std::vector<std::string> suffixes() const {
        const std::string n = m_path.filename().string();
        std::vector<std::string> out;
        const std::size_t start = n.find_first_not_of('.');
        if (start == std::string::npos) return out;
        std::size_t dot = n.find('.', start);
        while (dot != std::string::npos) {
            const std::size_t next = n.find('.', dot + 1);
            out.push_back(n.substr(dot, next == std::string::npos ? next : next - dot));
            dot = next;
        }
        return out;
    }

    // Path components in order: "a/b/c.yaml" -> ["a", "b", "c.yaml"].
    [[nodiscard]] std::vector<std::string> parts() const {
        std::vector<std::string> out;
        for (const fs::path& part : m_path) out.push_back(part.string());
        return out;
    }

    // "file://<path>" URI (forward slashes on every platform) and its repr.
    [[nodiscard]] std::string uri() const { return "file://" + m_path.generic_string(); }
    [[nodiscard]] std::string repr() const {
        std::string out = "file(\"" + uri() + "\"";
        if (m_engine != Engine::Unknown) {
            out += ", engine=";
            out += engine_label(m_engine);
        }
        return out + ")";
    }

    // -- path composition (pathlib-style) ---------------------------------
    // `self / other` — append a component (an absolute `other` replaces, as in
    // std::filesystem and pathlib). `other / self` is the reflected form.
    [[nodiscard]] file joined(const fs::path& other) const { return file(m_path / other, m_engine); }
    [[nodiscard]] file rjoined(const fs::path& other) const { return file(other / m_path, m_engine); }

    [[nodiscard]] file parent() const { return file(m_path.parent_path(), m_engine); }

    // Ancestors, closest first: "a/b/c" -> ["a/b", "a"].
    [[nodiscard]] std::vector<file> parents() const {
        std::vector<file> out;
        fs::path cur = m_path.parent_path();
        fs::path prev;
        while (!cur.empty() && cur != prev) {
            out.emplace_back(cur, m_engine);
            prev = cur;
            cur = cur.parent_path();
        }
        return out;
    }

    // Derived paths (return new file()s; never mutate).
    [[nodiscard]] file with_suffix(const std::string& s) const {
        fs::path p = m_path;
        p.replace_extension(s);
        return file(p, m_engine);
    }
    [[nodiscard]] file with_name(const std::string& n) const {
        fs::path p = m_path;
        p.replace_filename(n);
        return file(p, m_engine);
    }
    [[nodiscard]] file with_stem(const std::string& s) const { return with_name(s + suffix()); }
    [[nodiscard]] file absolute() const { return file(fs::absolute(m_path), m_engine); }
    [[nodiscard]] file resolve() const { return file(fs::weakly_canonical(m_path), m_engine); }

    // -- filesystem status ------------------------------------------------
    [[nodiscard]] bool is_absolute() const { return m_path.is_absolute(); }
    [[nodiscard]] bool exists() const { return fs::exists(m_path); }
    [[nodiscard]] bool is_file() const { return fs::is_regular_file(m_path); }
    [[nodiscard]] bool is_dir() const { return fs::is_directory(m_path); }
    [[nodiscard]] bool is_symlink() const { return fs::is_symlink(m_path); }
    [[nodiscard]] std::uintmax_t size() const { return fs::file_size(m_path); }

    // Which engine read() will decode with, in precedence order: the caller's
    // override wins, then the engine pinned at construction, then the
    // compile-time choice for this extension. Throws if none resolves — the
    // firewall against silently guessing a format.
    [[nodiscard]] Engine resolve_engine(std::string_view requested) const {
        if (!requested.empty()) {
            const Engine named = engine_from_name(requested);
            if (named == Engine::Unknown) {
                throw std::invalid_argument("unknown engine: '" + std::string(requested) +
                                            "' (known: yaml, json)");
            }
            return named;
        }
        if (m_engine != Engine::Unknown) return m_engine;
        const std::string ext = detail::ascii_lower(m_path.extension().string());
        const Engine byext = engine_for_ext(ext);
        if (byext == Engine::Unknown) {
            throw std::invalid_argument("no engine for extension '" + ext +
                                        "' — pass engine= at construction or read(engine=...)");
        }
        return byext;
    }

    // The raw bytes of the file, undecoded (binary, no newline translation).
    [[nodiscard]] std::string read_bytes() const {
        std::ifstream ifs(m_path, std::ios::binary);
        if (!ifs) throw std::runtime_error("cannot open file: " + m_path.string());
        std::ostringstream buffer;
        buffer << ifs.rdbuf();
        return buffer.str();
    }

private:
    fs::path m_path;
    Engine   m_engine{Engine::Unknown};   // pinned at construction; Unknown = auto
};

}  // namespace pygim::pathlike
