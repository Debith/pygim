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

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
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
enum class Engine { Unknown, Yaml, Json, Toml };

// Compile-time extension -> optimal engine. The whole format registry is here.
inline constexpr std::array<std::pair<std::string_view, Engine>, 4> kExtEngines{{
    {".yaml", Engine::Yaml},
    {".yml", Engine::Yaml},
    {".json", Engine::Json},
    {".toml", Engine::Toml},
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
// Both spellings select: the FORMAT ("yaml") and the LIBRARY ("rapidyaml") —
// formats are what users think in, libraries are what engine_label() reports.
[[nodiscard]] constexpr Engine engine_from_name(std::string_view name) noexcept {
    if (name == "yaml" || name == "yml" || name == "rapidyaml") return Engine::Yaml;
    if (name == "json" || name == "simdjson") return Engine::Json;
    if (name == "toml" || name == "toml++" || name == "tomlplusplus") return Engine::Toml;
    return Engine::Unknown;
}

// Engines are LIBRARIES, so their labels are library names: what .engine
// reports and repr() shows is the actual decoder implementation.
[[nodiscard]] constexpr std::string_view engine_label(Engine e) noexcept {
    switch (e) {
        case Engine::Yaml: return "rapidyaml";
        case Engine::Json: return "simdjson";
        case Engine::Toml: return "toml++";
        case Engine::Unknown: return "unknown";
    }
    return "unknown";
}

// Spot proof that the dispatch table is what we think it is — the exhaustive
// suites (whole-table sweeps, duplicates, label round-trips, glob matcher)
// live in tests/static/pathlike_core_proofs.cpp, compiled by every build.
static_assert(engine_for_ext(".yaml") == Engine::Yaml);
static_assert(engine_for_ext(".yml") == Engine::Yaml);
static_assert(engine_for_ext(".json") == Engine::Json);
static_assert(engine_for_ext(".toml") == Engine::Toml);
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

// One glob *segment* against one path component: `*` and `?`, never crossing
// a directory separator (the walk in file::glob() handles `/` and `**`).
[[nodiscard]] constexpr bool glob_match(std::string_view pattern, std::string_view name) noexcept {
    std::size_t p = 0, n = 0;
    std::size_t star_p = std::string_view::npos, star_n = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
            ++p; ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star_p = p++;
            star_n = n;
        } else if (star_p != std::string_view::npos) {
            p = star_p + 1;
            n = ++star_n;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

}  // namespace detail

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
    // These follow PATHLIB's rules, not std::filesystem's, wherever the two
    // disagree (proven by the PurePath differential tests): trailing slashes
    // are ignored, "." / ".." have no name, and a trailing dot is NOT a
    // suffix ("a." -> suffix "", where fs::path::extension() says ".").
    [[nodiscard]] std::string name() const {
        fs::path p = m_path;
        if (!p.has_filename() && p.has_parent_path()) p = p.parent_path();  // "a/b/" -> "a/b"
        const std::string n = p.filename().string();
        if (n == ".") return "";   // pathlib: "." has no name (".." keeps its)
        return n;
    }

    [[nodiscard]] std::string stem() const {
        const std::string n = name();
        const std::size_t dot = n.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot == n.size() - 1) return n;
        return n.substr(0, dot);
    }

    // Final extension including the dot (".gz"); "" if none. Case is preserved,
    // like pathlib — engine resolution lower-cases separately.
    [[nodiscard]] std::string suffix() const {
        const std::string n = name();
        const std::size_t dot = n.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot == n.size() - 1) return "";
        return n.substr(dot);
    }

    // Every extension of the final component: "a.tar.gz" -> [".tar", ".gz"].
    // A leading-dot name (".bashrc") has no suffixes, and a trailing dot has
    // none either, as in pathlib.
    [[nodiscard]] std::vector<std::string> suffixes() const {
        const std::string n = name();
        std::vector<std::string> out;
        if (n.empty() || n.back() == '.') return out;
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
    // pathlib normalisation: "." components and empties (trailing slashes)
    // are dropped; the root component ("/") is kept.
    [[nodiscard]] std::vector<std::string> parts() const {
        std::vector<std::string> out;
        for (const fs::path& part : m_path) {
            const std::string s = part.string();
            if (s.empty() || s == ".") continue;
            out.push_back(s);
        }
        return out;
    }

    // "file://<path>" URI (forward slashes on every platform) and its repr.
    [[nodiscard]] std::string uri() const { return "file://" + m_path.generic_string(); }
    [[nodiscard]] std::string repr() const {
        if (m_engine == Engine::Unknown) return std::format("file(\"{}\")", uri());
        return std::format("file(\"{}\", engine={})", uri(), engine_label(m_engine));
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

    // -- directory traversal (pathlib parity; results inherit the engine pin)
    // Children of this directory, sorted for determinism.
    [[nodiscard]] std::vector<file> iterdir() const {
        std::vector<file> out;
        for (const auto& e : fs::directory_iterator(m_path)) out.emplace_back(e.path(), m_engine);
        sort_by_path(out);
        return out;
    }

    // Relative glob: `*` and `?` within a component, `/` separates components,
    // `**` matches any number of directories (and, as the final component,
    // every descendant). Sorted, deduplicated.
    [[nodiscard]] std::vector<file> glob(std::string_view pattern) const {
        if (pattern.empty()) throw std::invalid_argument("glob: empty pattern");
        std::vector<std::string> segs;
        for (std::size_t start = 0; start <= pattern.size();) {
            const std::size_t slash = pattern.find('/', start);
            const std::size_t end = (slash == std::string_view::npos) ? pattern.size() : slash;
            if (end > start) segs.emplace_back(pattern.substr(start, end - start));
            if (slash == std::string_view::npos) break;
            start = slash + 1;
        }
        // A leading '/' is rejected explicitly: on Windows fs::path("/x") is
        // NOT is_absolute() (no drive), yet the pattern is clearly not
        // relative in the caller's intent.
        if (segs.empty() || pattern.front() == '/' || fs::path(pattern).is_absolute()) {
            throw std::invalid_argument("glob: pattern must be relative, got '" +
                                        std::string(pattern) + "'");
        }
        std::vector<file> out;
        glob_walk(m_path, segs, 0, out);
        sort_by_path(out);
        out.erase(std::unique(out.begin(), out.end(),
                              [](const file& a, const file& b) { return a.path() == b.path(); }),
                  out.end());
        return out;
    }

    // glob("**/" + pattern): the pattern anywhere under this directory.
    [[nodiscard]] std::vector<file> rglob(std::string_view pattern) const {
        return glob("**/" + std::string(pattern));
    }

    // Which engine read() will decode with, in precedence order: the caller's
    // override wins, then the engine pinned at construction, then the
    // compile-time choice for this extension. Throws if none resolves — the
    // firewall against silently guessing a format.
    [[nodiscard]] Engine resolve_engine(std::string_view requested) const {
        if (!requested.empty()) {
            const Engine named = engine_from_name(requested);
            if (named == Engine::Unknown) {
                throw std::invalid_argument("unknown engine: '" + std::string(requested) +
                                            "' (known: yaml/rapidyaml, json/simdjson, "
                                            "toml/toml++)");
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
    static void sort_by_path(std::vector<file>& v) {
        std::sort(v.begin(), v.end(),
                  [](const file& a, const file& b) { return a.path() < b.path(); });
    }

    // Walk one pattern segment at `base`. Iteration errors (permissions,
    // races) are skipped rather than thrown — matching pathlib's tolerance.
    void glob_walk(const fs::path& base, const std::vector<std::string>& segs,
                   std::size_t idx, std::vector<file>& out) const {
        const std::string& seg = segs[idx];
        const bool last = idx + 1 == segs.size();
        std::error_code ec;
        if (seg == "**") {
            if (last) {   // final `**`: every descendant, files and directories
                for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end;
                     it.increment(ec)) {
                    out.emplace_back(it->path(), m_engine);
                }
                return;
            }
            glob_walk(base, segs, idx + 1, out);   // `**` matching zero directories
            for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end;
                 it.increment(ec)) {
                std::error_code dec;
                if (it->is_directory(dec) && !dec) glob_walk(it->path(), segs, idx + 1, out);
            }
            return;
        }
        for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
            if (!detail::glob_match(seg, it->path().filename().string())) continue;
            if (last) {
                out.emplace_back(it->path(), m_engine);
            } else {
                std::error_code dec;
                if (it->is_directory(dec) && !dec) glob_walk(it->path(), segs, idx + 1, out);
            }
        }
    }

    fs::path m_path;
    Engine   m_engine{Engine::Unknown};   // pinned at construction; Unknown = auto
};

}  // namespace pygim::pathlike
