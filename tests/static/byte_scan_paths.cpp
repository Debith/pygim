// tests/static/byte_scan_paths.cpp — every path of utils/byte_scan.h against its definition, at run time.
//
// The dispatch runs one path a machine: AVX2 where it is there, so the SSE2 64-byte loop never runs
// on an AVX2 machine, and NEON only on arm64. This driver calls each path this build has directly —
// first_wide and each_wide (SSE2 or NEON), first_avx2 and each_avx2 where the processor runs them,
// and the dispatched first_non_ascii, first_line_end and for_each_line_end — over texts of every
// length 0-200 with a stop (a byte from 0x80, \n, \r, \r\n) at every offset, from every start
// 0-140, and compares each answer with the scalar definition. Built and run by
// run_byte_scan_paths.py, with and without the sanitizers.
//
// Prints one line per path it checked and "every path agrees with the definition"; exits 1 naming
// the first disagreement.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "../../src/_pygim_fast/utils/byte_scan.h"

namespace bs = pygim::byte_scan;
namespace d = pygim::byte_scan::detail;

namespace {

int failures = 0;

void disagree(const char* path, std::string_view text, std::size_t from, std::size_t got, std::size_t want) {
    if (failures++ < 5) std::printf("DISAGREES %s: length %zu, from %zu: %zu, the definition %zu\n", path, text.size(), from, got, want);
}

// The scalar definition's stops of `s`, in order.
template <class S>
std::vector<std::size_t> every_stop(std::string_view s) {
    std::vector<std::size_t> out;
    for (std::size_t i = d::scalar<S>(s, 0); i < s.size(); i = d::scalar<S>(s, i + 1)) out.push_back(i);
    return out;
}

// Texts of `length` bytes of 'a' with `stop` written at `at` (an empty stop: none).
std::string text(std::size_t length, std::size_t at, std::string_view stop) {
    std::string s(length, 'a');
    for (std::size_t k = 0; k < stop.size() && at + k < length; ++k) s[at + k] = stop[k];
    return s;
}

template <class S, class First>
void check_first(const char* path, std::string_view s, First first) {
    for (std::size_t from = 0; from <= 140 && from <= s.size(); ++from) {
        // a path returns the first stop, or where its whole steps end: the definition finishes it
        const std::size_t got = d::scalar<S>(s, first(s, from));
        const std::size_t want = d::scalar<S>(s, from);
        if (got != want) disagree(path, s, from, got, want);
    }
}

template <class S, class Each>
void check_each(const char* path, std::string_view s, Each each) {
    std::vector<std::size_t> got;
    auto put = [&](std::size_t at) { got.push_back(at); };
    for (std::size_t i = d::scalar<S>(s, each(s, put)); i < s.size(); i = d::scalar<S>(s, i + 1)) got.push_back(i);
    if (got != every_stop<S>(s)) disagree(path, s, 0, got.size(), every_stop<S>(s).size());
}

}  // namespace

int main() {
    const std::string_view stops[] = {"", "\xc3\xa9", "\n", "\r", "\r\n"};
    std::vector<std::string> texts;
    for (std::size_t length = 0; length <= 200; ++length) {
        for (std::string_view stop : stops) {
            for (std::size_t at = 0; at < length || at == 0; ++at) texts.push_back(text(length, at, stop));
        }
    }
    texts.push_back(std::string(4096, '\n'));   // a stop in every byte
    texts.push_back(std::string(4096, '\x80'));
    std::printf("%zu texts\n", texts.size());

    for (const std::string& s : texts) {
        check_first<d::not_ascii>("first_non_ascii", s, [](std::string_view t, std::size_t from) { return bs::first_non_ascii(t, from); });
        check_first<d::line_end>("first_line_end", s, [](std::string_view t, std::size_t from) { return bs::first_line_end(t, from); });
        check_each<d::line_end>("for_each_line_end", s, [](std::string_view t, auto& put) {
            bs::for_each_line_end(t, put);
            return t.size();
        });
#if PYGIM_BYTE_SCAN_SSE2 || PYGIM_BYTE_SCAN_NEON
        check_first<d::not_ascii>("first_wide<not_ascii>", s, [](std::string_view t, std::size_t from) { return d::first_wide<d::not_ascii>(t.data(), from, t.size()); });
        check_first<d::line_end>("first_wide<line_end>", s, [](std::string_view t, std::size_t from) { return d::first_wide<d::line_end>(t.data(), from, t.size()); });
        check_each<d::line_end>("each_wide<line_end>", s, [](std::string_view t, auto& put) { return d::each_wide<d::line_end>(t.data(), t.size(), put); });
#endif
#if PYGIM_BYTE_SCAN_AVX2
        if (pygim::has_avx2()) {
            check_first<d::not_ascii>("first_avx2<not_ascii>", s, [](std::string_view t, std::size_t from) { return d::first_avx2<d::not_ascii>(t.data(), from, t.size()); });
            check_first<d::line_end>("first_avx2<line_end>", s, [](std::string_view t, std::size_t from) { return d::first_avx2<d::line_end>(t.data(), from, t.size()); });
            check_each<d::line_end>("each_avx2<line_end>", s, [](std::string_view t, auto& put) { return d::each_avx2<d::line_end>(t.data(), t.size(), put); });
        }
#endif
    }
    // a start past the end is the size
    if (bs::first_line_end("ab", 7) != 2 || bs::first_non_ascii("ab", static_cast<std::size_t>(-1)) != 2) disagree("past the end", "ab", 7, 0, 2);
    std::string paths = "dispatched";
#if PYGIM_BYTE_SCAN_SSE2
    paths += ", sse2";
#endif
#if PYGIM_BYTE_SCAN_NEON
    paths += ", neon";
#endif
#if PYGIM_BYTE_SCAN_AVX2
    if (pygim::has_avx2()) paths += ", avx2";
#endif
    std::printf("paths: %s\n", paths.c_str());
    if (failures) {
        std::printf("%d disagreement(s)\n", failures);
        return 1;
    }
    std::printf("every path agrees with the definition\n");
    return 0;
}
