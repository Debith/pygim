#pragma once
// code/corpus.h — every Python file under a directory, parsed: the code component's one operation.
//
//     corpus_run run(fs::path("Lib"), pathlike::glob_pattern("**/*.py"), width);
//     run.run_here();                                                // walk, read, parse: C++ only
//     corpus c = run.collect();
//     c.size(), c[i].path(), c[i].parsed() ? c[i].tree() : c[i].error()
//     c.names()                                                      // one name table for all files
//
// One pool of tasks runs the whole of it (#270, D27): a directory listed is a task that adds the
// directories under it and a task per file it matches; a file task reads the file and parses it
// on the same worker (read and parse fused, #276). Nothing waits for the walk to end before the
// first parse starts. Each worker reads and parses with a workspace of its own — its scratch rows,
// its name table, the arena its files are read into and their trees' nodes packed into (D61, D69)
// — taken from a stack for the length of a file; at the end the workspaces' names are merged into
// one table and every tree renumbered to it (#276: per-worker tables merged at the end scaled 7x
// where one shared table got slower with threads). The files come out sorted by path, as glob
// sorts them.
//
// A file that is not Python is a result too: its error, line and column; a path that is no
// regular file (a directory `**` matched, a broken link) is left out, as read_all_files leaves it.
// Its bytes stay in its worker's arena until the corpus goes, as do a single-byte codec's before
// they were decoded: an arena gives nothing back.
//
// The run is the caller's to stop: run_all() works on the pool's threads and says when it is done,
// so a caller under an interrupt_scope can stop at Ctrl-C (code/adapter/corpus_bindings.h).
//
// CORE layer: pybind-free.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "../mapping/intern.h"
#include "../pathlike/file_reads.h"
#include "../pathlike/glob_walk.h"
#include "../utils/chunk_arena.h"
#include "../utils/parallel.h"
#include "../utils/timestamp.h"
#include "python/tree.h"

namespace pygim::code {

namespace fs = std::filesystem;
using names_table = mapping::hashed_interner;   // a corpus's names by default

using python::parse_failure;

/// One file of a corpus: its path, its size, and its tree or why it has none.
class corpus_file {
public:
    using outcome = python::parse_outcome;   // the file's tree, or why it has none

    corpus_file(std::string path, std::size_t bytes, outcome result) : m_path(std::move(path)), m_bytes(bytes), m_result(std::move(result)) {}

    [[nodiscard]] const std::string& path() const noexcept { return m_path; }
    [[nodiscard]] std::size_t bytes() const noexcept { return m_bytes; }
    [[nodiscard]] bool parsed() const noexcept { return std::holds_alternative<python::syntax_tree>(m_result); }
    [[nodiscard]] const python::syntax_tree& tree() const { return std::get<python::syntax_tree>(m_result); }
    [[nodiscard]] python::syntax_tree& tree() { return std::get<python::syntax_tree>(m_result); }
    [[nodiscard]] const parse_failure& error() const { return std::get<parse_failure>(m_result); }

private:
    std::string m_path;
    std::size_t m_bytes;
    outcome m_result;
};

/// One file read and parsed, with `names` as its name table: its tree, or its error and where.
/// `decoded`: the bytes are a str's UTF-8, not a file's (no BOM, no coding cookie). The tree is
/// where the bytes are: an arena's chunks, or the heap.
template <mapping::interner Names>
[[nodiscard]] corpus_file parse_one(std::string path, arena_string bytes, Names& names, python::parse_output& scratch, bool decoded = false) {
    const std::size_t size = bytes.size();
    return corpus_file(std::move(path), size, python::parse_python(std::move(bytes), names, scratch, decoded));
}

class corpus_stats {
public:
    corpus_stats() = default;
    /// The counts of `files`, with the size of their name table, the workers and the time taken.
    corpus_stats(const std::vector<corpus_file>& files, std::size_t names, std::size_t workers, duration took) noexcept
        : m_files(files.size()), m_names(names), m_workers(workers), m_took(took) {
        for (const corpus_file& f : files) {
            m_bytes += f.bytes();
            if (f.parsed()) m_nodes += f.tree().size();
            else ++m_failed;
        }
    }

    [[nodiscard]] std::size_t files() const noexcept { return m_files; }
    [[nodiscard]] std::size_t failed() const noexcept { return m_failed; }
    [[nodiscard]] std::size_t bytes() const noexcept { return m_bytes; }
    [[nodiscard]] std::size_t nodes() const noexcept { return m_nodes; }
    [[nodiscard]] std::size_t names() const noexcept { return m_names; }
    [[nodiscard]] std::size_t workers() const noexcept { return m_workers; }
    [[nodiscard]] duration took() const noexcept { return m_took; }

private:
    std::size_t m_files = 0, m_failed = 0, m_bytes = 0, m_nodes = 0, m_names = 0, m_workers = 0;
    duration m_took{0};
};

/// The parsed files under a root, sorted by path, sharing one name table; and the arenas their
/// trees' texts and nodes are in, which live as long as the corpus.
template <mapping::interner Names = names_table>
class basic_corpus {
public:
    using names_type = Names;
    using arenas = std::vector<std::unique_ptr<chunk_arena>>;
    basic_corpus() = default;
    basic_corpus(std::vector<corpus_file> files, Names names, corpus_stats stats, arenas held = {})
        : m_arenas(std::move(held)), m_files(std::move(files)), m_names(std::move(names)), m_stats(stats) {}

    [[nodiscard]] std::size_t size() const noexcept { return m_files.size(); }
    [[nodiscard]] const corpus_file& operator[](std::size_t i) const noexcept { return m_files[i]; }
    [[nodiscard]] const std::vector<corpus_file>& files() const noexcept { return m_files; }
    [[nodiscard]] const Names& names() const noexcept { return m_names; }
    [[nodiscard]] const corpus_stats& stats() const noexcept { return m_stats; }
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    /// The index of the file at `path`, or npos.
    [[nodiscard]] std::size_t find(std::string_view path) const noexcept {
        const auto it = std::lower_bound(m_files.begin(), m_files.end(), path, [](const corpus_file& f, std::string_view p) { return f.path() < p; });
        return it != m_files.end() && it->path() == path ? static_cast<std::size_t>(it - m_files.begin()) : npos;
    }

private:
    arenas m_arenas;   // first: it outlives the trees whose nodes it holds
    std::vector<corpus_file> m_files;
    Names m_names;
    corpus_stats m_stats;
};
using corpus = basic_corpus<>;

/// A run that builds a corpus: the walk, the reads and the parses on one task pool. Its names
/// table is a hashed one: the merge at the end shards the workers' names by their hashes.
template <parallel::width_policy Width = parallel::chosen_width, clock_source Clock = monotonic_clock, mapping::interner Names = names_table>
    requires(Names::hashed)
class basic_corpus_run {
public:
    basic_corpus_run(fs::path root, pathlike::glob_pattern pattern, Width width, Clock clock = Clock{})
        : m_pattern(std::move(pattern)), m_clock(clock), m_started(m_clock.now_ns()), m_pool(worker(this), std::move(width)) {
        m_pool.submit(task(pathlike::glob_task::first(m_pattern, std::move(root))));
    }

    /// Runs on the pool's threads; `finished()` is called from the one that ends the last task.
    void run_all(std::function<void()> finished) { m_pool.run_all(std::move(finished)); }
    /// Runs with the caller among the workers; returns when every file is parsed.
    void run_here() { m_pool.run_here(); }

    /// The corpus, once the run is done: files sorted by path, names merged into one table.
    [[nodiscard]] basic_corpus<Names> collect() {
        m_pool.rethrow_if_failed();
        std::vector<corpus_file> files;
        std::vector<std::size_t> owner;   // which workspace each file came from
        for (std::size_t w = 0; w < m_workspaces.size(); ++w) m_workspaces[w]->move_files(files, owner, w);
        Names names = merged_names(files, owner);
        std::sort(files.begin(), files.end(), [](const corpus_file& a, const corpus_file& b) { return a.path() < b.path(); });
        files.erase(std::unique(files.begin(), files.end(), [](const corpus_file& a, const corpus_file& b) { return a.path() == b.path(); }), files.end());
        const corpus_stats stats(files, names.size(), m_workspaces.size(), duration(m_clock.now_ns() - m_started));
        typename basic_corpus<Names>::arenas arenas;
        for (const auto& w : m_workspaces) arenas.push_back(w->take_arena());
        return basic_corpus<Names>(std::move(files), std::move(names), stats, std::move(arenas));
    }

private:
    using task = std::variant<pathlike::glob_task, fs::path>;

    // What one worker reads and parses with: its scratch rows, its name table, the arena its files
    // are read into and its trees' nodes packed into, the files it has done.
    class workspace {
    public:
        // The file at `path` read into the arena and parsed; nothing for a path that is no regular
        // file, or one the OS refused: left out.
        void read_and_parse(std::string path) {
            pathlike::read_result r = pathlike::native_reads{}.read(path, m_arena.get());
            arena_string* bytes = std::get_if<arena_string>(&r);
            if (bytes == nullptr) return;
            m_files.push_back(parse_one(std::move(path), std::move(*bytes), m_names, m_scratch));
        }
        // Its arena, moved out to the corpus with the trees in it.
        [[nodiscard]] std::unique_ptr<chunk_arena> take_arena() noexcept { return std::move(m_arena); }
        [[nodiscard]] const Names& names() const noexcept { return m_names; }
        // Its names' hashes, by id: what a merge interns them by.
        [[nodiscard]] std::vector<std::uint64_t> hashes() const {
            std::vector<std::uint64_t> h(m_names.size());
            for (std::uint32_t id = 0; id < h.size(); ++id) h[id] = Names::hash_of(m_names[id]);
            return h;
        }
        // Its names, moved out: a run of one workspace, whose ids are already the corpus's.
        [[nodiscard]] Names take_names() noexcept { return std::move(m_names); }
        // Its files, appended to `files` with `me` for each in `owner`; their trees still in its ids.
        void move_files(std::vector<corpus_file>& files, std::vector<std::size_t>& owner, std::size_t me) {
            for (corpus_file& f : m_files) {
                files.push_back(std::move(f));
                owner.push_back(me);
            }
            m_files.clear();
        }

    private:
        Names m_names;
        python::parse_output m_scratch;
        std::unique_ptr<chunk_arena> m_arena = std::make_unique<chunk_arena>();
        std::vector<corpus_file> m_files;
    };

    // The workspaces' names as one table, every tree in `files` renumbered to it, on as many cores
    // as there were workspaces. One workspace's table is the corpus's as it is. Of several, each
    // hashes its names on a core of its own; then shard s interns, in workspace order, the names
    // whose hash's top bits say s; the shards, appended one after another, are the table, a name's
    // id its shard's offset plus its id there. One thread interning 16 workspaces' 138,000 names
    // took 9-11 ms of CPython Lib's 59 ms parse.
    using name_hashes = std::vector<std::uint64_t>;   // a table's names' hashes, by id
    using id_map = std::vector<std::uint32_t>;        // a workspace's ids to another table's

    [[nodiscard]] Names merged_names(std::vector<corpus_file>& files, const std::vector<std::size_t>& owner) {
        const std::size_t width = m_workspaces.size();
        if (width <= 1) return width ? m_workspaces.front()->take_names() : Names();
        std::vector<name_hashes> hashes(width);
        std::vector<id_map> maps(width);   // each workspace's ids to its shard's, then to the corpus's
        parallel::for_each(width, width, [&](std::size_t w, std::size_t) {
            hashes[w] = m_workspaces[w]->hashes();
            maps[w].resize(hashes[w].size());
        });
        const auto shard_of = [width](std::uint64_t h) { return static_cast<std::size_t>(((h >> 32) * width) >> 32); };
        std::vector<Names> shards(width);
        std::vector<name_hashes> shard_hashes(width);
        std::vector<std::size_t> shard_bytes(width, 0);
        parallel::for_each(width, width, [&](std::size_t s, std::size_t) {
            for (std::size_t w = 0; w < width; ++w) {
                const Names& from = m_workspaces[w]->names();
                for (std::uint32_t id = 0; id < from.size(); ++id) {
                    const std::uint64_t h = hashes[w][id];
                    if (shard_of(h) != s) continue;
                    maps[w][id] = shards[s].intern(from[id], h);
                    if (maps[w][id] == shard_hashes[s].size()) {   // new in the shard
                        shard_hashes[s].push_back(h);
                        shard_bytes[s] += from[id].size();
                    }
                }
            }
        });
        Names names;
        std::size_t ids = 0, bytes = 0;
        for (std::size_t s = 0; s < width; ++s) ids += shards[s].size(), bytes += shard_bytes[s];
        names.reserve(ids, bytes);
        std::vector<std::uint32_t> offset(width);
        for (std::size_t s = 0; s < width; ++s) {
            offset[s] = static_cast<std::uint32_t>(names.size());
            for (std::uint32_t id = 0; id < shards[s].size(); ++id) (void)names.intern(shards[s][id], shard_hashes[s][id]);   // each new: shards are disjoint
        }
        parallel::for_each(width, width, [&](std::size_t w, std::size_t) {
            for (std::uint32_t id = 0; id < maps[w].size(); ++id) maps[w][id] += offset[shard_of(hashes[w][id])];
        });
        parallel::for_each(files.size(), width, [&](std::size_t i, std::size_t) {
            if (files[i].parsed()) files[i].tree().remap_names(maps[owner[i]]);
        });
        return names;
    }

    // A free workspace, lent for one file and back on the free list however the parse ends (a
    // bad_alloc too); one is made when every one is lent. The free list has room for every
    // workspace, so giving one back never allocates.
    class lease {
    public:
        explicit lease(basic_corpus_run& run) : m_run(run), m_workspace(run.take()) {}
        ~lease() { m_run.give(m_workspace); }
        lease(const lease&) = delete;
        lease& operator=(const lease&) = delete;
        [[nodiscard]] workspace* operator->() const noexcept { return m_workspace; }

    private:
        basic_corpus_run& m_run;
        workspace* m_workspace;
    };
    [[nodiscard]] workspace* take() {
        std::lock_guard guard(m_lock);
        if (m_free.empty()) {
            m_free.reserve(m_workspaces.size() + 1);
            m_workspaces.push_back(std::make_unique<workspace>());
            return m_workspaces.back().get();
        }
        workspace* w = m_free.back();
        m_free.pop_back();
        return w;
    }
    void give(workspace* w) noexcept {
        std::lock_guard guard(m_lock);
        m_free.push_back(w);
    }

    // A directory: its matches become file tasks, its subdirectories directory tasks.
    // A file: read and parsed, here.
    class worker {
    public:
        explicit worker(basic_corpus_run* run) noexcept : m_run(run) {}
        template <class Add>
        void operator()(task&& t, const Add& add) const {
            if (auto* dir = std::get_if<pathlike::glob_task>(&t)) {
                pathlike::glob_step(m_run->m_pattern, *dir, [&](fs::path p) { add(task(std::move(p))); }, [&](pathlike::glob_task g) { add(task(std::move(g))); });
                return;
            }
            const fs::path& p = std::get<fs::path>(t);
            lease(*m_run)->read_and_parse(pathlike::text_from_fs(p));   // UTF-8 on Windows, as the rest of pathlike
        }

    private:
        basic_corpus_run* m_run;
    };

    pathlike::glob_pattern m_pattern;
    [[no_unique_address]] Clock m_clock;
    std::int64_t m_started;
    std::mutex m_lock;
    std::vector<std::unique_ptr<workspace>> m_workspaces;
    std::vector<workspace*> m_free;
    parallel::task_pool<task, worker, Width> m_pool;   // last: its workers stop before what they use goes
};
using corpus_run = basic_corpus_run<>;

}  // namespace pygim::code
