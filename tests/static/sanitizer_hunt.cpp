// tests/static/sanitizer_hunt.cpp — both source readers under AddressSanitizer and
// UndefinedBehaviorSanitizer, over real files and deterministic mutants of them.
//
// Built and driven by run_sanitizer_hunt.py; one process runs one unit:
//
//     sanitizer_hunt <python|cpp> <seed file list> <unit> <first> <count> <crash dir>
//
// Unit 0..R-1 read the corpus as it is (a slice of files each); later units read mutants: input i
// of unit u is file (mix(u, i) % files) with 1 to 4 mutations drawn from the same seed, so any
// input is made again from (target, unit, i) alone. A source a reader refuses is a result, not a
// finding. A finding is a sanitizer report (the process dies: -fno-sanitize-recover) or an input
// that takes over ten seconds (an alarm): either writes the input to <crash dir> and names it on
// stderr ("died on input <i>") before the process ends, and the driver goes on from the next input.
//
// Each input runs through:
//   python  code::python: source checks, lexer, parser, tree, dump with positions;
//           similarity's Python frontend and every unit's fingerprint
//   cpp     similarity's C++ tokenizer and reader, every unit's fingerprint
//
// The result is one JSON line on stdout: inputs, refused, the slowest input and its time.

#include <sanitizer/common_interface_defs.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "../../src/_pygim_fast/code/python/dump.h"
#include "../../src/_pygim_fast/code/python/parser.h"
#include "../../src/_pygim_fast/code/python/tree.h"
#include "../../src/_pygim_fast/mapping/intern.h"
#include "../../src/_pygim_fast/similarity/cpp_source.h"
#include "../../src/_pygim_fast/similarity/python_source.h"
#include "../../src/_pygim_fast/utils/timestamp.h"

namespace {

namespace cp = pygim::code::python;
namespace sim = pygim::similarity;

// ── the input in hand: written out if the process dies or hangs on it ─────────
std::string g_input;
char g_crash_path[4096];
char g_input_line[64];   // "sanitizer_hunt: died on input <i>\n": the driver reads which input from stderr

void write_all(int fd, const char* p, std::size_t left) {
    while (left > 0) {
        const ssize_t n = ::write(fd, p, left);
        if (n <= 0) break;
        p += n;
        left -= static_cast<std::size_t>(n);
    }
}
void write_input() {
    write_all(2, g_input_line, std::strlen(g_input_line));
    const int fd = ::open(g_crash_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    write_all(fd, g_input.data(), g_input.size());
    ::close(fd);
}
void on_death() { write_input(); }
// UBSan ends in abort() (abort_on_error=1) without calling the death callback: the input is
// written here instead, then the abort goes on to its default end.
void on_abort(int sig) {
    write_input();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
void on_alarm(int) {
    write_input();
    constexpr char msg[] = "sanitizer_hunt: input took over 10 s\n";
    [[maybe_unused]] const auto n = ::write(2, msg, sizeof msg - 1);
    ::_exit(124);
}

// ── deterministic mutation ───────────────────────────────────────────────────
std::uint64_t mix(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

class random_stream {
public:
    explicit random_stream(std::uint64_t seed) : m_state(seed) {}
    std::uint64_t next() { return m_state = mix(m_state); }
    std::size_t below(std::size_t n) { return n == 0 ? 0 : static_cast<std::size_t>(next() % n); }

private:
    std::uint64_t m_state;
};

// Tokens as a mutator sees them: runs of word bytes, runs of blanks, single other bytes.
std::vector<std::pair<std::size_t, std::size_t>> spans_of(const std::string& s) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    std::size_t i = 0;
    auto word = [](unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; };
    while (i < s.size()) {
        std::size_t j = i + 1;
        if (word(static_cast<unsigned char>(s[i]))) {
            while (j < s.size() && word(static_cast<unsigned char>(s[j]))) ++j;
        } else if (s[i] == ' ' || s[i] == '\t') {
            while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
        }
        out.emplace_back(i, j - i);
        i = j;
    }
    return out;
}

const std::vector<std::string>& pieces(bool python) {
    static const std::vector<std::string> py{
        "(", ")", "[", "]", "{", "}", ":", ",", "'", "\"", "'''", "\"\"\"", "f\"", "t'", "rb'", "\\\n", "\t", "\r", "\r\n",
        "lambda", "match", "case", "type", "async", "await", "*", "**", "@", ":=", "#", "\\N{", "{x=}", "{x!r:>{w}}",
        "\n    ", "\n\t", "\f", "\x00", "\x80", "\xc3", "\xf0\x9f", "\xef\xbb\xbf", "# -*- coding: latin-1 -*-\n",
        "# coding: cp1252\n", "elif", "else:", "yield", "from", "import", "global", "nonlocal", "1e", "0x", "1_", "1j"};
    static const std::vector<std::string> cpp{
        "(", ")", "[", "]", "{", "}", ";", ",", "'", "\"", "R\"(", ")\"", "R\"x(", "u8\"", "/*", "*/", "//", "\\\n", "#if 0\n",
        "#else\n", "#endif\n", "#if X\n", "#define M(x) \\\n", "::", "->", "<=>", "template<", ">>", "operator", "class ",
        "struct ", "namespace ", "using ", "extern \"C\" {", "\x00", "\x80", "\xc3", "\r", "\t", "1'000", "0x1p3", "'\\''"};
    return python ? py : cpp;
}

std::string mutated(std::string s, random_stream& r, bool python) {
    const std::size_t ops = 1 + r.below(4);
    for (std::size_t k = 0; k < ops; ++k) {
        const auto spans = spans_of(s);
        const std::size_t op = r.below(10);
        if (spans.empty()) {
            s += pieces(python)[r.below(pieces(python).size())];
            continue;
        }
        const auto [at, len] = spans[r.below(spans.size())];
        switch (op) {
            case 0: s.erase(at, len); break;                                                   // a token deleted
            case 1: s.insert(at, s.substr(at, len)); break;                                    // doubled
            case 2: {                                                                          // replaced by another
                const auto [at2, len2] = spans[r.below(spans.size())];
                const std::string other = s.substr(at2, len2);
                s.replace(at, len, other);
                break;
            }
            case 3: {                                                                          // two swapped
                const auto [at2, len2] = spans[r.below(spans.size())];
                if (at2 > at + len) {
                    const std::string a = s.substr(at, len), b = s.substr(at2, len2);
                    s.replace(at2, len2, a);
                    s.replace(at, len, b);
                }
                break;
            }
            case 4:
            case 5: s.insert(at, pieces(python)[r.below(pieces(python).size())]); break;      // a piece of the grammar
            case 6: s[r.below(s.size())] = static_cast<char>(r.next()); break;                // a byte changed
            case 7: s.resize(r.below(s.size() + 1)); break;                                     // cut short
            case 8: s.insert(at, std::string(1 + r.below(1200), "([{"[r.below(3)])); break;  // deep nesting
            default: {                                                                         // a span repeated
                const std::size_t from = r.below(s.size()), n = std::min<std::size_t>(r.below(256), s.size() - from);
                s.insert(r.below(s.size() + 1), s.substr(from, n));
                break;
            }
        }
    }
    return s;
}

// ── the readers ───────────────────────────────────────────────────────────────
// Returns false when the reader refuses the source.
bool read_python(const std::string& bytes, sim::python_source::workspace& work, cp::parse_output& scratch) {
    bool ok = true;
    try {   // the code component: the tree, then its dump with positions (literals decoded)
        cp::source_text src = cp::source_text::from_bytes(pygim::arena_string(bytes));
        pygim::mapping::hashed_interner names;
        scratch.clear();
        cp::basic_parser<cp::stop_at_first, pygim::mapping::hashed_interner>(src.text(), scratch, names).module();
        const cp::syntax_tree tree(std::move(src), std::move(scratch));
        volatile std::size_t n = cp::dump(tree, names, cp::plain_repr{}, 0, true).size();
        (void)n;
    } catch (const cp::syntax_error&) {
        ok = false;
    }
    try {   // similarity's frontend over the same bytes, then every fingerprint
        for (const sim::parsed_unit& u : sim::python_source::parse_python(pygim::arena_string(bytes), sim::source_context("hunt.py", "pkg.hunt", "pkg"), work)) {
            volatile std::uint64_t root = sim::fingerprint_of(u.body()).root();
            (void)root;
        }
    } catch (const sim::parse_error&) {
        ok = false;
    }
    return ok;
}

bool read_cpp(const std::string& bytes) {
    try {
        for (const sim::parsed_unit& u : sim::cpp::parse_cpp(bytes, "hunt.cpp")) {
            volatile std::uint64_t root = sim::fingerprint_of(u.body()).root();
            (void)root;
        }
        return true;
    } catch (const sim::parse_error&) {
        return false;
    }
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 7) {
        std::fprintf(stderr, "usage: sanitizer_hunt <python|cpp> <seed file list> <unit> <first> <count> <crash dir>\n");
        return 2;
    }
    const bool python = std::string_view(argv[1]) == "python";
    const std::uint64_t unit = std::stoull(argv[3]);
    const std::size_t first = std::stoull(argv[4]), count = std::stoull(argv[5]);
    const std::string crash_dir = argv[6];

    std::vector<std::string> paths;
    {
        std::ifstream list(argv[2]);
        for (std::string line; std::getline(list, line);) {
            if (!line.empty()) paths.push_back(line);
        }
    }
    const std::size_t real_units = (paths.size() + 199) / 200;   // the corpus as it is, 200 files a unit
    std::vector<std::string> files(paths.size());
    __sanitizer_set_death_callback(on_death);
    std::signal(SIGALRM, on_alarm);
    std::signal(SIGABRT, on_abort);

    sim::python_source::workspace work;
    cp::parse_output scratch;
    std::size_t refused = 0, inputs = 0, slowest_i = 0;
    double slowest_ms = 0.0;
    for (std::size_t i = first; i < first + count; ++i) {
        std::size_t file = 0;
        std::string described;
        if (unit < real_units) {
            file = unit * 200 + i;
            if (file >= paths.size()) break;
            if (files[file].empty()) files[file] = read_file(paths[file]);
            g_input = files[file];
        } else {
            random_stream r(mix(unit * 0x100000001b3ull + i));
            file = r.below(paths.size());
            if (files[file].empty()) files[file] = read_file(paths[file]);
            g_input = mutated(files[file], r, python);
        }
        std::snprintf(g_crash_path, sizeof g_crash_path, "%s/%s-u%llu-i%zu.bin", crash_dir.c_str(), argv[1],
                      static_cast<unsigned long long>(unit), i);
        std::snprintf(g_input_line, sizeof g_input_line, "sanitizer_hunt: died on input %zu\n", i);
        ::alarm(10);
#ifdef HUNT_PLANTED_DEFECTS   // the negative control: the instrumentation must catch both
        if (g_input.size() > 4 && g_input[0] == '#' && g_input[1] == 'A') {
            volatile char past = g_input.data()[g_input.size() + 64];   // a heap read past the end
            (void)past;
        }
        if (g_input.size() > 4 && g_input[0] == '#' && g_input[1] == 'U') {
            volatile int big = INT_MAX;
            big = big + static_cast<int>(g_input.size());   // signed overflow
        }
#endif
        const pygim::monotonic_timestamp t0 = pygim::monotonic_timestamp::now();
        const bool ok = python ? read_python(g_input, work, scratch) : read_cpp(g_input);
        const double ms = 1e3 * pygim::monotonic_timestamp::now().seconds_since(t0);
        ::alarm(0);
        refused += !ok;
        ++inputs;
        if (ms > slowest_ms) {
            slowest_ms = ms;
            slowest_i = i;
        }
    }
    std::printf("{\"target\": \"%s\", \"unit\": %llu, \"first\": %zu, \"inputs\": %zu, \"refused\": %zu, \"slowest_i\": %zu, "
                "\"slowest_ms\": %.1f, \"real\": %s}\n",
                argv[1], static_cast<unsigned long long>(unit), first, inputs, refused, slowest_i, slowest_ms,
                unit < real_units ? "true" : "false");
    return 0;
}
