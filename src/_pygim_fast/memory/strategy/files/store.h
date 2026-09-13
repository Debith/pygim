// memory/strategy/files/store.h — the canonical store: plain files, shared through git (03 §3).
//
//   taxonomy/        base.yaml, pack-*.yaml                     section 02
//   objects/7d/3a…   memory text, and frozen vocabularies, by digest
//   audit/<clone>.jsonl        this copy's rows, append-only, hash-chained
//   usage/<clone>/<day>.jsonl  observations
//   receipts/<clone>/<day>.jsonl
//   memories/<slug>.md         one head view per chain, regenerated
//   local/                     never committed: clone id, commit lock, sessions
//
// Satisfies memory_store (core/service.h).
#pragma once

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../../core/service.h"
#include "base_vocabulary.h"
#include "json_lines.h"
#include "lock.h"
#include "yaml_taxonomy.h"

namespace pygim::memory::strategy::files {

namespace fs = std::filesystem;

class store {
public:
    /// Creates a repository at `root` with the base vocabulary. Refuses a
    /// directory that already holds one.
    static void init(const fs::path& root) {
        if (fs::exists(root / "taxonomy" / "base.yaml"))
            throw std::runtime_error(root.string() + ": already a memory repository");
        for (const char* d : {"taxonomy", "objects", "audit", "usage", "receipts", "memories", "reviews", "corpus", "local"})
            fs::create_directories(root / d);
        write_atomically(root / "taxonomy" / "base.yaml", base_vocabulary);
        write_atomically(root / ".gitignore", "local/\n");
        write_atomically(root / ".gitattributes", "*.jsonl merge=union\n");
    }

    explicit store(const fs::path& root) : m_root(fs::absolute(root)), m_lock(m_root / "local" / "commit.lock") {
        if (!fs::exists(m_root / "taxonomy" / "base.yaml"))
            throw std::runtime_error(m_root.string() + ": not a memory repository (no taxonomy/base.yaml) — run `oo memory init`");
        const auto id = m_root / "local" / "clone";
        if (fs::exists(id)) {
            m_clone = read_file(id);
            while (!m_clone.empty() && (m_clone.back() == '\n' || m_clone.back() == '\r')) m_clone.pop_back();
        }
        if (m_clone.empty()) {
            std::random_device rd;
            m_clone = "c-" + digest::of(decimal(rd()) + decimal(rd()) + m_root.string()).hex().substr(0, 8);
            write_atomically(id, m_clone + "\n");
        }
    }

    [[nodiscard]] const fs::path& root() const noexcept { return m_root; }
    [[nodiscard]] std::string clone() const { return m_clone; }

    /// Things noticed while reading the files that need a human's eye.
    [[nodiscard]] const std::vector<std::string>& problems() const noexcept { return m_problems; }

    // ── vocabulary ────────────────────────────────────────────────────────

    [[nodiscard]] std::vector<taxonomy_file> taxonomy_files() const {
        std::vector<taxonomy_file> out;
        for (const auto& e : fs::directory_iterator(m_root / "taxonomy")) {
            if (!e.is_regular_file() || e.path().extension() != ".yaml") continue;
            out.push_back({"taxonomy/" + e.path().filename().string(), read_file(e.path())});
        }
        return out;
    }

    [[nodiscard]] taxonomy_load load_taxonomy() { return files::load_taxonomy(taxonomy_files()); }

    /// Every vocabulary version is frozen when first seen, so a receipt that
    /// pinned it can still be rerun after the files move on (03 §3.1).
    void freeze_taxonomy(const digest& version) {
        std::string blob = "pygim-taxonomy-files-1";
        for (const auto& f : taxonomy_files()) {
            encode_field(blob, f.name);
            encode_field(blob, f.text);
        }
        put_object(version, blob);
    }

    [[nodiscard]] std::optional<taxonomy_load> load_frozen_taxonomy(const digest& version) {
        const auto blob = object(version);
        if (!blob || !blob->starts_with("pygim-taxonomy-files-1")) return std::nullopt;
        std::vector<taxonomy_file> files;
        std::string_view rest = std::string_view(*blob).substr(std::string_view("pygim-taxonomy-files-1").size());
        auto next = [&](std::string& out) {
            const auto colon = rest.find(':');
            if (colon == std::string_view::npos) return false;
            std::size_t n = 0;
            for (const char c : rest.substr(0, colon)) n = n * 10 + static_cast<std::size_t>(c - '0');
            out = std::string(rest.substr(colon + 1, n));
            rest.remove_prefix(colon + 1 + n);
            return true;
        };
        while (!rest.empty()) {
            taxonomy_file f;
            if (!next(f.name) || !next(f.text)) break;
            files.push_back(std::move(f));
        }
        return files::load_taxonomy(std::move(files));
    }

    // ── rows ──────────────────────────────────────────────────────────────

    /// Every row in every clone's audit file.
    [[nodiscard]] std::vector<row> rows() {
        m_offsets.clear();
        m_problems.clear();
        return new_rows();
    }

    /// Rows appended to any audit file since this store last read it —
    /// complete lines only, so a row half-written by a dying process is never
    /// read as a row.
    [[nodiscard]] std::vector<row> new_rows() {
        std::vector<row> out;
        const auto dir = m_root / "audit";
        if (!fs::exists(dir)) return out;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir))
            if (e.is_regular_file() && e.path().extension() == ".jsonl") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const auto& p : files) {
            const std::string name = p.filename().string();
            std::size_t& offset = m_offsets[name];
            const auto size = static_cast<std::size_t>(fs::file_size(p));
            if (size <= offset) continue;
            const std::string text = read_file(p);
            const auto end = text.rfind('\n');
            if (end == std::string::npos || end + 1 <= offset) continue;
            std::size_t line_no = static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
            std::size_t pos = offset;
            while (pos <= end) {
                const auto nl = text.find('\n', pos);
                ++line_no;
                const std::string_view line(text.data() + pos, nl - pos);
                pos = nl + 1;
                if (line.empty()) continue;
                std::string problem;
                if (auto r = parse_row(line, &problem)) {
                    if (r->clone == m_clone) m_seq = std::max(m_seq, r->seq);
                    out.push_back(std::move(*r));
                } else {
                    m_problems.push_back("audit/" + name + ":" + decimal(line_no) + ": " + problem);
                }
            }
            offset = end + 1;
        }
        return out;
    }

    /// The commit point (03 §4.1): the row reaches the disk before this returns.
    void append(const row& r) {
        const std::string name = m_clone + ".jsonl";
        const std::string line = row_line(r) + "\n";
        append_durably(m_root / "audit" / name, line);
        m_offsets[name] = static_cast<std::size_t>(fs::file_size(m_root / "audit" / name));
        m_seq = std::max(m_seq, r.seq);
    }

    [[nodiscard]] std::uint64_t next_seq() { return m_seq + 1; }

    [[nodiscard]] file_lock::guard lock() { return m_lock.acquire(); }

    // ── objects ───────────────────────────────────────────────────────────

    void put_object(const digest& d, std::string_view bytes) {
        const auto p = object_path(d);
        if (fs::exists(p)) return;  // content-addressed: writing twice is writing once
        write_atomically(p, bytes);
    }

    [[nodiscard]] std::optional<std::string> object(const digest& d) const {
        const auto p = object_path(d);
        if (!fs::exists(p)) return std::nullopt;
        return read_file(p);
    }

    // ── observations ──────────────────────────────────────────────────────

    void append_usage(const usage_record& u) { append_durably(day_file("usage"), usage_line(u) + "\n"); }

    [[nodiscard]] std::vector<usage_record> usage() const {
        std::vector<usage_record> out;
        for (const auto& line : lines_under(m_root / "usage"))
            if (auto u = parse_usage(line)) out.push_back(std::move(*u));
        return out;
    }

    void append_receipt(const receipt& r) { append_durably(day_file("receipts"), receipt_line(r) + "\n"); }

    [[nodiscard]] std::vector<receipt> receipts() const {
        std::vector<receipt> out;
        for (const auto& line : lines_under(m_root / "receipts"))
            if (auto r = parse_receipt(line)) out.push_back(std::move(*r));
        return out;
    }

    // ── head views (03 §3.3) ──────────────────────────────────────────────

    void write_view(const memory_record& m, std::string_view text, const std::vector<std::string>& tags) {
        std::string out = "---\nmemory: ";
        out += m.key.hex();
        out += "\ntitle: ";
        json_string(out, m.title);
        out += "\norigin: " + m.origin + "\ntags: ";
        json_strings(out, tags);
        if (!m.supersedes.empty()) {
            std::vector<std::string> keys;
            for (const auto& k : m.supersedes) keys.push_back(k.hex());
            out += "\nsupersedes: ";
            json_strings(out, keys);
        }
        if (!m.generalises.empty()) {
            std::vector<std::string> keys;
            for (const auto& k : m.generalises) keys.push_back(k.hex());
            out += "\ngeneralises: ";
            json_strings(out, keys);
        }
        if (!m.cites.empty()) {
            out += "\ncites: ";
            json_strings(out, m.cites);
        }
        out += "\n---\n";
        out.append(text);
        if (out.back() != '\n') out.push_back('\n');
        write_atomically(m_root / "memories" / (m.slug + ".md"), out);
    }

    /// A session's lessons-learnt report (overview §4.11): a view regenerated from the audit log,
    /// committed with the repository so the human reviews it in the diff. Returns its path.
    std::string write_report(std::uint64_t session, std::string_view text) {
        const fs::path p = m_root / "reviews" / ("session-" + std::to_string(session) + ".md");
        fs::create_directories(p.parent_path());
        write_atomically(p, text);
        return p.string();
    }

    void remove_view(std::string_view slug) {
        std::error_code ec;
        fs::remove(m_root / "memories" / (std::string(slug) + ".md"), ec);
    }

    // ── sessions and time ─────────────────────────────────────────────────

    [[nodiscard]] std::uint64_t next_session() {
        auto guard = lock();
        const auto p = m_root / "local" / "session";
        std::uint64_t n = 0;
        if (fs::exists(p))
            for (const char c : read_file(p))
                if (c >= '0' && c <= '9') n = n * 10 + static_cast<std::uint64_t>(c - '0');
        ++n;
        write_atomically(p, decimal(n) + "\n");
        return n;
    }

    [[nodiscard]] std::string now() const {
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
        return buf;
    }

private:
    [[nodiscard]] fs::path object_path(const digest& d) const {
        const std::string h = d.hex();
        return m_root / "objects" / h.substr(0, 2) / h.substr(2);
    }

    [[nodiscard]] fs::path day_file(std::string_view kind) const { return m_root / kind / m_clone / (now().substr(0, 10) + ".jsonl"); }

    [[nodiscard]] static std::vector<std::string> lines_under(const fs::path& dir) {
        std::vector<std::string> out;
        if (!fs::exists(dir)) return out;
        std::vector<fs::path> files;
        for (const auto& e : fs::recursive_directory_iterator(dir))
            if (e.is_regular_file() && e.path().extension() == ".jsonl") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const auto& p : files) {
            const std::string text = read_file(p);
            std::size_t pos = 0;
            while (pos < text.size()) {
                const auto nl = text.find('\n', pos);
                if (nl == std::string::npos) break;  // an unfinished line is not a record
                if (nl > pos) out.emplace_back(text.substr(pos, nl - pos));
                pos = nl + 1;
            }
        }
        return out;
    }

    fs::path m_root;
    file_lock m_lock;
    std::string m_clone;
    std::map<std::string, std::size_t> m_offsets;
    std::uint64_t m_seq = 0;
    std::vector<std::string> m_problems;
};

static_assert(memory_store<store>);

}  // namespace pygim::memory::strategy::files
