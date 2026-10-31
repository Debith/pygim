#pragma once
// similarity/adapter/adapter.h — the Python face: the language registry and
// `Index`, one language's units and their all-pairs comparison.
//
// The registry is OPEN: every header in languages/ is one language — a
// class with `static constexpr language_info info` and a static
// `parse(std::string_view text, const source_context& where) -> parsed_source<L>`
// (the Language concept below; a `parse(arena_string&&, ...)` beside it takes a
// read file's bytes over) — and the build discovers the directory into a
// generated `using Languages = language_list<...>` (ext.similarity.toml).
// Adding a language is adding a header.
//
// An Index holds ONE language, chosen by argument when it is made:
//
//     ix = similarity.Index("python")
//     ix.add_file("a.py"); ix.add("def f(x): return x", origin="<snippet>")
//     ix.pairs("function", threshold=0.9)    # a pyarrow.Table: every function against every other
//
// Under the hood it is a std::variant of corpus<L> over the pack: each
// operation visits the one alternative it holds, and a corpus<L> only accepts
// what L's frontend parsed (core.h), so a C++ file can never land in a Python
// index, nor a C++ unit be compared with a Python one. add_file() refuses a
// file of another language with ValueError naming it.
//
// Results are Arrow: units() and pairs() return pyarrow Tables built in C++
// (tables.h) and handed over through the Arrow PyCapsule protocol without a
// copy (utils/arrow_stream.h); no row is ever a Python object.
//
// Two ways out, for two sizes of result. pairs() RANKS: every pair is
// collected, sorted on every core and cut into batches — or, with a limit,
// only the best N are kept and the sweep prunes against them. stream_pairs()
// does not rank: a RecordBatchReader whose batches are swept on every core
// while the reader is consumed (stream.h), in bounded memory — the path for
// 100M+ rows.
//
// Threading: the corpus lives in a shared index_state (index_state.h), reached
// only through its commands: write() under the exclusive lock, which moves the
// generation on; read() and read_at() under the shared one. Every method lets
// go of the GIL first. add() and add_file() parse and fingerprint outside any
// lock and take the exclusive one only to append; pairs(), units(), len(),
// stats() and stream_pairs()'s opening read under the shared one, and a
// stream's workers read each block or tile through read_at. So other Python
// threads run meanwhile, an add() waits instead of racing, and a stream that
// outlives an add() fails rather than mixing two corpora.

#include <array>
#include <cerrno>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <pybind11/pybind11.h>

#include "../../pathlike/adapter/path_text.h"
#include "../../pathlike/batch_read.h"
#include "../../utils/arrow_stream.h"
#include "../../utils/chars.h"
#include "../../utils/interrupted.h"
#include "../../utils/pieces.h"
#include "../../utils/string_list.h"
#include "../core.h"
#include "../sweeps.h"
#include "index_state.h"
#include "stream.h"
#include "tables.h"

namespace pygim::similarity {

namespace py = pybind11;

/// A language with a frontend: the core's `language` plus `parse`.
template <class L>
concept Language = language<L> && requires(std::string_view s, const source_context& ctx) {
    { L::parse(s, ctx) } -> std::same_as<parsed_source<L>>;
};

/// The dotted module a source file is, and the package its relative
/// imports start from — the way the import system would name it: the file's
/// stem (nothing for `__init__`), under every parent directory that holds an
/// `__init__.py` / `__init__.pyi`, outermost first.
///
///     src/pkg/sub/m.py          (pkg and sub are packages)  -> pkg.sub.m, package pkg.sub
///     src/pkg/sub/__init__.py                                 -> pkg.sub,   package pkg.sub
///     scripts/tool.py           (no __init__.py)              -> tool,      package ""
///
/// `module` given explicitly wins; its package is then its parent (or
/// itself for an `__init__` file).
class module_path {
public:
    module_path(std::string module, std::string package) : m_module(std::move(module)), m_package(std::move(package)) {}
    [[nodiscard]] const std::string& module() const noexcept { return m_module; }
    [[nodiscard]] const std::string& package() const noexcept { return m_package; }

private:
    std::string m_module;
    std::string m_package;
};

[[nodiscard]] inline std::string parent_module(std::string_view m) {
    const std::size_t dot = m.rfind('.');
    return dot == std::string_view::npos ? std::string() : std::string(m.substr(0, dot));
}

[[nodiscard]] inline module_path module_of(const std::filesystem::path& file, const std::optional<std::string>& given) {
    const bool is_init = file.stem() == "__init__";
    if (given) return {*given, is_init ? *given : parent_module(*given)};
    std::vector<std::string> parts;
    if (!is_init) parts.push_back(file.stem().string());
    std::error_code ec;
    for (std::filesystem::path dir = std::filesystem::absolute(file, ec).parent_path(); !dir.empty();) {
        if (!std::filesystem::exists(dir / "__init__.py", ec) && !std::filesystem::exists(dir / "__init__.pyi", ec)) break;
        parts.push_back(dir.filename().string());
        const auto up = dir.parent_path();
        if (up == dir) break;
        dir = up;
    }
    std::string module;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        module += module.empty() ? "" : ".";
        module += *it;
    }
    return {module, is_init ? module : parent_module(module)};
}

/// The registry, generic over the pack the build discovered. No two
/// languages share a name or an extension: checked when it is instantiated.
template <Language... Ls>
struct language_list {
    static_assert(sizeof...(Ls) > 0, "similarity: the language registry is empty (no header in adapter/languages/)");
    static_assert(languages_disjoint<Ls...>(), "similarity: two languages share a name or a file extension");

    /// "cpp, python": the inventory error messages quote.
    [[nodiscard]] static std::string names() { return joined(std::array<std::string_view, sizeof...(Ls)>{Ls::info.name()...}, ", "); }

    /// The language owning extension `ext` (lower-case, leading dot), or none.
    [[nodiscard]] static std::optional<std::string_view> owner_of(std::string_view ext) {
        std::optional<std::string_view> out;
        ((Ls::info.owns(ext) ? (out = Ls::info.name(), true) : false) || ...);
        return out;
    }
};

/// The lower-cased suffix of a file name: "A.CPP" -> ".cpp", "Makefile" -> "".
[[nodiscard]] inline std::string suffix_of(std::string_view name) {
    const std::size_t slash = name.find_last_of("/\\");
    const std::string_view leaf = slash == std::string_view::npos ? name : name.substr(slash + 1);
    const std::size_t dot = leaf.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return {};
    return chars::lower(leaf.substr(dot));
}

// The kind named `k`; ValueError naming the kinds otherwise.
[[nodiscard]] inline unit_kind parse_kind(std::string_view k) {
    if (const std::optional<unit_kind> kind = unit_kind_named(k)) return *kind;
    throw std::invalid_argument("similarity: kind must be 'function' or 'class', got '" + std::string(k) + "'");
}

template <class List>
class basic_index;

/// What add_files() left out: each file's path, and why.
using left_out_files = std::vector<std::pair<std::string, std::string>>;

template <Language... Ls>
class basic_index<language_list<Ls...>> {
    using registry = language_list<Ls...>;
    using variant = std::variant<corpus<Ls>...>;
    using state = index_state<variant>;

public:
    /// An empty index of the language named `name`; ValueError naming the
    /// known languages otherwise.
    explicit basic_index(std::string_view name) : m_state(std::make_shared<state>(make(name))) {}

    /// The language's name: "python".
    [[nodiscard]] std::string_view language() const {
        return m_state->of_type([]<class C>(std::type_identity<C>) { return C::language_type::info.name(); });
    }

    /// Parses `text` and adds its units, recorded as coming from `origin`.
    /// `module` (dotted) qualifies its module-level names and resolves its
    /// relative imports; empty when unknown. Its package is the module's
    /// parent, or the module itself when `origin` names an `__init__` file, as
    /// add_file() takes it. A source the frontend cannot read raises
    /// ValueError ("<origin>:<line>: ..."). Fails every stream opened before it
    /// (stream.h).
    void add(std::string_view text, std::string_view origin, std::string_view module = {}) {
        const std::string package = module_of(std::filesystem::path(origin), std::string(module)).package();
        const source_context ctx{origin, module, package};
        m_state->of_type([&]<class C>(std::type_identity<C>) {
            using L = typename C::language_type;
            py::gil_scoped_release nogil;   // the parse touches only `text` (the str's UTF-8, alive for the call) and locals
            prepared_source<L> units(L::parse(text, ctx));
            m_state->template write<C>([&](C& c) { c.add(std::move(units), origin); });
        });
    }

    /// Reads the file at `path` (str or os.PathLike) and adds its units, its module named by
    /// `module` or else by module_of() from the package directories around it. The read, the
    /// parse and the fingerprints are C++ with the GIL released (pathlike's file_reads.h: one
    /// open, non-blocking, and an fstat of the descriptor — a FIFO is refused, never waited on).
    /// A file whose extension the index's language does not own is a ValueError naming the file,
    /// its language if any, and this index's; one the OS will not read is the OSError open()
    /// would raise (FileNotFoundError, IsADirectoryError, PermissionError); one its frontend cannot
    /// read is a ValueError "<path>:<line>: ...".
    void add_file(const py::object& path, const std::optional<std::string>& module) {
        const std::string p = pathlike::utf8_fspath(path);
        check_language(p);
        int refused = 0;          // the read's errno, raised as OSError once the GIL is back
        std::string unreadable;   // the frontend's message, raised as ValueError
        m_state->of_type([&]<class C>(std::type_identity<C>) {
            using L = typename C::language_type;
            py::gil_scoped_release nogil;   // the read and the parse touch only locals; the append is write()'s
            pathlike::read_result got = pathlike::native_reads{}.read(p);
            if (const auto* none = std::get_if<pathlike::not_a_file>(&got)) {
                refused = none->open_error();
                return;
            }
            if (const auto* failed = std::get_if<pathlike::read_failure>(&got)) {
                refused = failed->error();
                return;
            }
            const module_path mp = module_of(pathlike::file(p).to_fs(), module);
            const source_context ctx{p, mp.module(), mp.package()};
            std::optional<prepared_source<L>> units;
            try {
                units.emplace(L::parse(std::move(std::get<arena_string>(got)), ctx));   // a language that takes the bytes keeps them
            } catch (const parse_error& e) {
                unreadable = e.what();
                return;
            }
            m_state->template write<C>([&](C& c) { c.add(std::move(*units), p); });
        });
        if (refused != 0) {
            errno = refused;
            PyErr_SetFromErrnoWithFilename(PyExc_OSError, p.c_str());   // the OSError subclass open() would raise
            throw py::error_already_set();
        }
        if (!unreadable.empty()) throw std::invalid_argument(unreadable);
    }

    /// Reads, parses and fingerprints `files` on worker threads with the GIL released — Ctrl-C
    /// and SIGTERM owned for the run (#272) — then adds their units in the order given, so their
    /// ids are what add_file() in a loop would give them. A path of another language is a
    /// ValueError before anything is read. A file the frontend cannot read, that the OS refuses or
    /// that is no file is left out, and returned as (path, message). Modules as add_file() derives
    /// them. `threads`: how many work at once; None grows them while more are worth it.
    [[nodiscard]] left_out_files add_files(const py::iterable& files, std::optional<std::size_t> threads) {
        if (py::isinstance<py::str>(files) || py::isinstance<py::bytes>(files))   // iterated, one path is its characters
            throw py::type_error("similarity: add_files() takes an iterable of paths, not one path: add_files([path]), or add_file(path)");
        parallel::chosen_width width = parallel::width_for(threads, "add_files");   // 0 refused before a read, even of nothing
        std::vector<std::string> names;
        for (py::handle f : files) {
            names.push_back(pathlike::utf8_fspath(f));
            check_language(names.back());
        }
        left_out_files rejected;
        if (names.empty()) return rejected;
        m_state->of_type([&]<class C>(std::type_identity<C>) {
            using L = typename C::language_type;
            using outcome = typename file_stage<L>::outcome;
            pathlike::path_list paths;
            paths.reserve(names.size());
            for (const std::string& n : names) paths.push_back(n);
            const pathlike::window_limits every = pathlike::window_limits::whole(paths.size());
            using job_type = pathlike::basic_batch_read<pathlike::native_reads, file_stage<L>>;
            std::vector<outcome> results = run_interruptible(   // the run touches only its own job
                "add_files", [&] { return job_type(std::move(paths), std::move(width), every); }, [](job_type& job) { return job.collect(); });
            std::size_t units = 0, sources = 0;   // reserved at once: each unit moves once (LLVM's 156,089: 17-25 ms -> 5 ms)
            for (std::size_t i = 0; i < results.size(); ++i) {
                if (const auto* prepared = std::get_if<prepared_source<L>>(&results[i])) {
                    units += prepared->units().size();
                    ++sources;
                } else {
                    rejected.emplace_back(names[i], std::move(std::get<std::string>(results[i])));
                }
            }
            if (sources == 0) return;   // nothing read: the corpus, its streams and its caches stay as they are
            py::gil_scoped_release nogil;   // the corpus, under write()'s exclusive lock; `results` and `names` are this call's
            m_state->template write<C>([&](C& c) {
                c.reserve(units, sources);
                for (std::size_t i = 0; i < results.size(); ++i) {
                    if (auto* prepared = std::get_if<prepared_source<L>>(&results[i])) c.add(std::move(*prepared), names[i]);
                }
            });
        });
        return rejected;
    }

    /// Number of units (functions and classes).
    [[nodiscard]] std::size_t size() const {
        return read([](const auto& r) { return r.corpus().units().size(); });
    }

    /// The units, in insertion order, optionally of one kind, as a
    /// pyarrow.Table (tables.h: id, kind, qualname, origin, line, owner,
    /// nodes, weight). A method's `owner` is its class's id.
    [[nodiscard]] py::object units(const std::optional<std::string>& kind) const {
        const std::optional<unit_kind> want = kind ? std::optional{parse_kind(*kind)} : std::nullopt;
        std::shared_ptr<arrow::RecordBatch> batch = read([&](const auto& r) { return tables::units(r.corpus(), want); });
        return arrow_stream::to_table(arrow_stream::exporter::of(std::move(batch)));
    }

    /// Every pair of units of `kind`, each of at least `min_size` nodes,
    /// scoring at least `threshold`, RANKED (best first, ties by unit ids),
    /// as a pyarrow.Table of batches of `batch_size` rows (tables.h: a, b,
    /// a_qualname, a_origin, a_line, b_qualname, b_origin, b_line, score,
    /// shared, total, exact). With a `limit`, only the best `limit` pairs —
    /// and the sweep skips what could not make the cut.
    ///
    /// `columns` selects and orders the columns (default: all); (a, b, score)
    /// is a 16-byte row instead of ~56, the names, files and lines joinable
    /// from units() by id.
    ///
    /// Ranking needs every pair in memory (24 bytes each, plus the table);
    /// for results too large for that, stream_pairs().
    /// Everything — sweep, sort, batch building — runs on every core with the
    /// GIL released. Cost: O(n²) comparisons in the worst case.
    [[nodiscard]] py::object pairs(std::string_view kind, double threshold, std::uint32_t min_size,
                                   std::size_t limit, std::size_t batch_size,
                                   const std::optional<std::vector<std::string>>& columns) const {
        const unit_kind k = parse_kind(kind);
        check_threshold(threshold);
        check_batch_size(batch_size);
        std::vector<tables::pair_col> cols = tables::select_pair_columns(columns);
        std::shared_ptr<arrow::Schema> schema;
        std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
        read([&](const auto& r) {
            const std::vector<std::uint32_t> order = r.sweep_order(k, min_size);
            const sweeps::sorted_parts parts = sweeps::best_pairs(r.corpus(), order, threshold, limit, k);
            std::size_t total = 0;
            for (const auto& p : parts) total += p.size();
            if (limit != 0 && total > limit) total = limit;
            std::shared_ptr<const tables::unit_columns> units = units_for(r, cols);   // before cols moves
            const tables::pair_columns shared(std::move(units), std::move(cols));
            schema = shared.schema();
            batches.resize(pieces(total, batch_size));
            sweeps::ranked(parts, limit, batch_size, [&](std::size_t b, std::size_t n, auto&& chunks) {
                tables::pair_batch out(shared, n);
                chunks([&](std::size_t at, const match* m, std::size_t k) { out.write(at, m, k); });
                batches[b] = std::move(out).finish();
            });
        });
        return arrow_stream::to_table(arrow_stream::exporter::of(std::move(schema), std::move(batches)));
    }

    /// The same pairs as pairs() without a limit, UNRANKED, as a
    /// pyarrow.RecordBatchReader: batches of exactly `batch_size` rows (the
    /// last shorter) produced on every core while the reader is consumed, in
    /// sweep order — the same batches every time, for any thread count.
    /// Memory stays bounded whatever the result size — by `batch_size` and
    /// the workers: the batches a placed stream's workers are writing (a few
    /// ahead of the one read), or two blocks a worker of a collected one — so
    /// 100M+ pairs can be written to Parquet, aggregated or filtered as they
    /// come. `columns` as for pairs(). An add() while the stream is open fails
    /// the stream.
    [[nodiscard]] py::object stream_pairs(std::string_view kind, double threshold, std::uint32_t min_size,
                                          std::size_t batch_size,
                                          const std::optional<std::vector<std::string>>& columns) const {
        const unit_kind k = parse_kind(kind);
        check_threshold(threshold);
        check_batch_size(batch_size);
        std::vector<tables::pair_col> cols = tables::select_pair_columns(columns);
        std::shared_ptr<arrow::RecordBatchReader> reader = read([&](const auto& r) -> std::shared_ptr<arrow::RecordBatchReader> {
            using C = std::decay_t<decltype(r.corpus())>;
            std::shared_ptr<const tables::unit_columns> units = units_for(r, cols);   // before cols moves
            tables::pair_columns shared(std::move(units), std::move(cols));
            std::vector<std::uint32_t> order = r.sweep_order(k, min_size);
            const std::uint64_t at = r.generation();
            return sweeps::with_stream(k, threshold, [&]<sweeps::stream_plan P, class Counts>(sweeps::plan_tag<P>, std::type_identity<Counts>)
                                                         -> std::shared_ptr<arrow::RecordBatchReader> {
                if constexpr (P == sweeps::stream_plan::placed) {
                    return std::make_shared<placed_pair_stream<C, state, Counts>>(m_state, r.corpus(), at, std::move(order), batch_size, std::move(shared));
                } else {
                    return std::make_shared<pair_stream<C, state, Counts>>(m_state, r.corpus(), at, std::move(order), threshold, batch_size, std::move(shared));
                }
            });
        });
        return arrow_stream::to_reader(arrow_stream::exporter::streaming(std::move(reader)));
    }

    /// Counts and the corpus's exact bytes — its units, their fingerprints and names, not what a
    /// query keeps beside them (index_state's columns and orders): {"units", "functions",
    /// "classes", "subtrees", "bytes"}.
    [[nodiscard]] py::dict stats() const {
        const corpus_counts n = read([](const auto& r) { return corpus_counts(r.corpus()); });
        py::dict d;
        d["units"] = n.units();
        d["functions"] = n.functions();
        d["classes"] = n.classes();
        d["subtrees"] = n.subtrees();
        d["bytes"] = n.bytes();
        return d;
    }

    [[nodiscard]] std::string repr() const {
        return "similarity.Index('" + std::string(language()) + "', units=" + std::to_string(size()) + ")";
    }

private:
    std::shared_ptr<state> m_state;

    // f(corpus_reading) under the shared lock, the GIL let go while it waits and runs: what every
    // query of the corpus goes through.
    template <class F>
    decltype(auto) read(F&& f) const {
        return m_state->of_type([&]<class C>(std::type_identity<C>) -> decltype(auto) {
            py::gil_scoped_release nogil;   // the corpus, under read()'s shared lock; f touches no Python object
            return m_state->template read<C>(f);
        });
    }

    // What stats() reports of one corpus state.
    class corpus_counts {
    public:
        template <class Corpus>
        explicit corpus_counts(const Corpus& c) : m_units(c.units().size()), m_bytes(c.bytes()) {
            for (const unit& u : c.units()) {
                (u.kind() == unit_kind::function ? m_functions : m_classes) += 1;
                m_subtrees += u.fp().subtrees().size();
            }
        }
        [[nodiscard]] std::size_t units() const noexcept { return m_units; }
        [[nodiscard]] std::size_t functions() const noexcept { return m_functions; }
        [[nodiscard]] std::size_t classes() const noexcept { return m_classes; }
        [[nodiscard]] std::size_t subtrees() const noexcept { return m_subtrees; }
        [[nodiscard]] std::size_t bytes() const noexcept { return m_bytes; }

    private:
        std::size_t m_units, m_functions = 0, m_classes = 0, m_subtrees = 0, m_bytes;
    };

    // The units' columns a result with columns `cols` shows, from the reading's cache; none when it
    // shows no name, file or line.
    template <class Reading>
    [[nodiscard]] static std::shared_ptr<const tables::unit_columns> units_for(const Reading& r, const std::vector<tables::pair_col>& cols) {
        return tables::pair_columns::needs_units(cols) ? r.unit_columns() : nullptr;
    }

    static void check_threshold(double threshold) {
        if (!(threshold >= 0.0 && threshold <= 1.0))
            throw std::invalid_argument("similarity: threshold must be within [0, 1], got " + std::to_string(threshold));
    }
    static void check_batch_size(std::size_t batch_size) {
        if (batch_size == 0) throw std::invalid_argument("similarity: batch_size must be at least 1, got 0");
    }

    // A path whose extension the index's language does not own: ValueError naming both languages.
    void check_language(const std::string& p) const {
        const std::string ext = suffix_of(p);
        m_state->of_type([&]<class C>(std::type_identity<C>) {
            using L = typename C::language_type;
            if (L::info.owns(ext)) return;
            std::string msg = "'" + p + "' is ";
            const auto other = registry::owner_of(ext);
            msg += other ? "a " + std::string(*other) + " file" : "not a file of a known language";
            msg += "; this index holds " + std::string(L::info.name()) + " (" + joined(L::info.exts(), " ") + ") and never compares across languages";
            throw std::invalid_argument(msg);
        });
    }

    // What a worker does with a file it read: its module from the package directories around it,
    // its units parsed and fingerprinted — or why there are none. Throws nothing.
    template <class L>
    class file_stage {
    public:
        using outcome = std::variant<prepared_source<L>, std::string>;
        [[nodiscard]] outcome operator()(pathlike::os_path path, pathlike::read_result&& got) const noexcept {
            try {
                if (auto* bytes = std::get_if<arena_string>(&got)) {
                    const module_path mp = module_of(pathlike::file(std::string(path.text())).to_fs(), std::nullopt);   // as add_file() names it
                    const source_context ctx{path.text(), mp.module(), mp.package()};   // views of the job's path_list and of mp
                    return prepared_source<L>(L::parse(std::move(*bytes), ctx));
                }
                if (const auto* none = std::get_if<pathlike::not_a_file>(&got)) return pathlike::left_out(path, *none);
                return pathlike::left_out(path, std::get<pathlike::read_failure>(got));
            } catch (const std::exception& e) {
                return std::string(e.what());
            } catch (...) {
                return std::string("unreadable");
            }
        }
    };

    static variant make(std::string_view name) {
        std::optional<variant> out;
        ((Ls::info.name() == name ? (out.emplace(std::in_place_type<corpus<Ls>>), true) : false) || ...);
        if (!out)
            throw std::invalid_argument("similarity: unknown language '" + std::string(name) + "' (known: " +
                                        registry::names() + ")");
        return std::move(*out);
    }
};

}  // namespace pygim::similarity
