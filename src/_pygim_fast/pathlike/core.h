#pragma once
// pathlike/core.h — the pybind-free heart of `pygim.path`.
//
// A `file` is a std::filesystem::path that knows how to read and decode itself.
// WHICH decoder handles a path is still decided at compile time from the
// extension — but the SET of decoders is open: every engine is one header in
// adapter/engines/, discovered by the build into a type list (engine_list.h
// explains the mechanism and the proofs). This header only defines the
// pybind-free vocabulary that the registry, the proofs and `file` share:
// `engine_info` (the face of one engine) and `file` itself.
//
// This header carries no pybind11 and no parser library, so it stays fast to
// compile and trivially testable; the engine glue that must speak Python lives
// in adapter/.

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

// A borrowed view over an engine's constexpr string list — a deliberately
// minimal stand-in for std::span that is usable in constant expressions on
// every compiler in the build matrix.
struct sv_list {
    const std::string_view* first = nullptr;
    std::size_t count = 0;

    constexpr sv_list() = default;
    template <std::size_t N>
    constexpr sv_list(const std::array<std::string_view, N>& items) noexcept  // implicit by design
        : first(items.data()), count(N) {}

    [[nodiscard]] constexpr const std::string_view* begin() const noexcept { return first; }
    [[nodiscard]] constexpr const std::string_view* end() const noexcept { return first + count; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count; }
    [[nodiscard]] constexpr bool empty() const noexcept { return count == 0; }
    [[nodiscard]] constexpr bool contains(std::string_view s) const noexcept {
        for (std::string_view x : *this) {
            if (x == s) return true;
        }
        return false;
    }
};

// The pybind-free face of one engine: everything the registry, the proofs and
// `file` need to know. Every engine header declares one as `static constexpr
// engine_info info`; its address is the engine's identity at run time (static
// constexpr members are inline variables, so there is exactly one per program).
struct engine_info {
    std::string_view name;     // format name: engine="json"; the Python class is "<name>file"
    std::string_view label;    // LIBRARY label reported by .engine: "simdjson"
    std::string_view doc;      // one sentence, used by every derived docstring
    sv_list exts;              // auto-dispatch extensions: lower-case, leading dot
    sv_list aliases;           // extra engine= spellings: "yml", "ndjson", "tomlplusplus"

    // engine= selectors: the format name, the library label and the aliases.
    [[nodiscard]] constexpr bool selects(std::string_view n) const noexcept {
        return n == name || n == label || aliases.contains(n);
    }
    [[nodiscard]] constexpr bool owns(std::string_view ext) const noexcept { return exts.contains(ext); }
};

namespace detail {
// ASCII case folding of a short extension, so ".YAML" resolves like ".yaml".
// constexpr so the fold-then-lookup chain is provable at compile time.
[[nodiscard]] constexpr std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

[[nodiscard]] constexpr std::string ascii_upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
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
// An engine may be pinned at construction (nullptr = auto by extension); derived
// paths (parent(), with_suffix(), ...) inherit the pin. Resolution itself lives
// in the registry (engine_list.h: engine_list::resolve), which knows every engine.
class file {
public:
    explicit file(fs::path p, const engine_info* pin = nullptr)
        : m_path(std::move(p)), m_pin(pin) {}

    [[nodiscard]] const fs::path& path() const noexcept { return m_path; }

    // The engine pinned at construction; nullptr = auto by extension.
    [[nodiscard]] const engine_info* pinned() const noexcept { return m_pin; }

    // os.PathLike: the plain filesystem string.
    [[nodiscard]] std::string fspath() const { return m_path.string(); }

    // The lower-cased extension the registry dispatches on (".yaml").
    [[nodiscard]] std::string ext_key() const { return detail::ascii_lower(m_path.extension().string()); }

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
    // like pathlib — engine resolution lower-cases separately (ext_key()).
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
        if (!m_pin) return std::format("file(\"{}\")", uri());
        return std::format("file(\"{}\", engine={})", uri(), m_pin->label);
    }

    // -- path composition (pathlib-style) ---------------------------------
    // `self / other` — append a component (an absolute `other` replaces, as in
    // std::filesystem and pathlib). `other / self` is the reflected form.
    [[nodiscard]] file joined(const fs::path& other) const { return file(m_path / other, m_pin); }
    [[nodiscard]] file rjoined(const fs::path& other) const { return file(other / m_path, m_pin); }

    [[nodiscard]] file parent() const { return file(m_path.parent_path(), m_pin); }

    // Ancestors, closest first: "a/b/c" -> ["a/b", "a"].
    [[nodiscard]] std::vector<file> parents() const {
        std::vector<file> out;
        fs::path cur = m_path.parent_path();
        fs::path prev;
        while (!cur.empty() && cur != prev) {
            out.emplace_back(cur, m_pin);
            prev = cur;
            cur = cur.parent_path();
        }
        return out;
    }

    // Derived paths (return new file()s; never mutate).
    [[nodiscard]] file with_suffix(const std::string& s) const {
        fs::path p = m_path;
        p.replace_extension(s);
        return file(p, m_pin);
    }
    [[nodiscard]] file with_name(const std::string& n) const {
        fs::path p = m_path;
        p.replace_filename(n);
        return file(p, m_pin);
    }
    [[nodiscard]] file with_stem(const std::string& s) const { return with_name(s + suffix()); }
    [[nodiscard]] file absolute() const { return file(fs::absolute(m_path), m_pin); }
    [[nodiscard]] file resolve() const { return file(fs::weakly_canonical(m_path), m_pin); }

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
        for (const auto& e : fs::directory_iterator(m_path)) out.emplace_back(e.path(), m_pin);
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

    // The raw bytes of the file, undecoded (binary, no newline translation).
    [[nodiscard]] std::string read_bytes() const {
        std::ifstream ifs(m_path, std::ios::binary);
        if (!ifs) throw std::runtime_error("cannot open file: " + m_path.string());
        std::ostringstream buffer;
        buffer << ifs.rdbuf();
        return buffer.str();
    }

    // Replace the file's contents with `data`, byte for byte (pathlib parity).
    void write_bytes(std::string_view data) const {
        std::ofstream ofs(m_path, std::ios::binary | std::ios::trunc);
        if (!ofs) throw std::runtime_error("cannot open file for writing: " + m_path.string());
        ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!ofs) throw std::runtime_error("write failed: " + m_path.string());
    }

    // Create this directory (pathlib parity): `parents` creates missing
    // ancestors, `exist_ok` tolerates an existing directory. A non-directory
    // in the way always fails.
    void mkdir(bool parents = false, bool exist_ok = false) const {
        std::error_code ec;
        if (fs::is_directory(m_path, ec)) {
            if (exist_ok) return;
            throw std::runtime_error("directory exists: " + m_path.string());
        }
        if (fs::exists(m_path, ec)) {
            throw std::runtime_error("not a directory: " + m_path.string());
        }
        const bool made = parents ? fs::create_directories(m_path, ec) : fs::create_directory(m_path, ec);
        if (ec || !made) {
            throw std::runtime_error("cannot create directory " + m_path.string() +
                                     (ec ? ": " + ec.message() : std::string{}));
        }
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
                    out.emplace_back(it->path(), m_pin);
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
                out.emplace_back(it->path(), m_pin);
            } else {
                std::error_code dec;
                if (it->is_directory(dec) && !dec) glob_walk(it->path(), segs, idx + 1, out);
            }
        }
    }

    fs::path m_path;
    const engine_info* m_pin{nullptr};   // pinned at construction; nullptr = auto by extension
};

}  // namespace pygim::pathlike
