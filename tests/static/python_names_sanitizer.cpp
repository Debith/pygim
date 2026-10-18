// tests/static/python_names_sanitizer.cpp — drive every function of utils/python_names.h over a
// corpus under AddressSanitizer and UndefinedBehaviorSanitizer, so an out-of-bounds read, a bad
// std::string_view span or signed overflow in the naming rules aborts the run.
//
// Built and run only by tests/static/run_python_names_sanitizer.py (g++ -fsanitize=address,undefined).
// Not part of the extension build or the pytest suite. The corpus is read from argv[1], one input
// per line, each line a sequence of \xHH escapes decoded to raw bytes (so NULs and non-UTF-8 bytes
// are exercised); every std::string outlives the std::string_view taken from it.

#include "python_names.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace pn = pygim::python_names;

static std::string decode(const std::string& line) {
    std::string out;
    for (std::size_t i = 0; i < line.size();) {
        if (line[i] == '\\' && i + 3 < line.size() && line[i + 1] == 'x') {
            out.push_back(static_cast<char>(std::stoi(line.substr(i + 2, 2), nullptr, 16)));
            i += 4;
        } else {
            out.push_back(line[i]);
            ++i;
        }
    }
    return out;
}

// Sink the results so the optimiser cannot delete the calls; the sanitizers watch the work itself.
static std::size_t sink = 0;
static void use(bool b) { sink += b ? 1 : 2; }
static void use(std::string_view s) { sink += s.size(); for (char c : s) sink += static_cast<unsigned char>(c); }
static void use(const std::string& s) { use(std::string_view{s}); }

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s corpus\n", argv[0]); return 2; }
    std::vector<std::string> corpus;
    if (FILE* file = std::fopen(argv[1], "rb")) {
        char* line = nullptr;
        std::size_t cap = 0;
        for (ssize_t n; (n = getline(&line, &cap, file)) != -1;) {
            std::string text(line, static_cast<std::size_t>(n));
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            corpus.push_back(decode(text));
        }
        std::free(line);
        std::fclose(file);
    }

    for (const auto& a : corpus) {
        use(pn::is_dunder(a));
        use(pn::is_private_name(a));
        use(pn::owner_of(a));
        for (const auto& b : corpus) {
            use(pn::mangled(a, b));
            use(pn::unmangled(a, b));
            use(pn::requalified(a, b, b));  // b as from and as qualname: exercises the prefix compare
            // Three-argument moves: a->b over the whole corpus of names would be cubic, so the name
            // rotates through the corpus while from/to are this pair.
            const auto& c = corpus[(&b - &corpus[0] + 1) % corpus.size()];
            if (auto moved = pn::respelt(a, b, c)) use(*moved);
            use(pn::reowned(a, b, c));
        }
    }
    std::fprintf(stderr, "sanitizer harness: %zu corpus entries, sink=%zu\n", corpus.size(), sink);
    return 0;
}
