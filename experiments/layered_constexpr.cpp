// Compile-time tests for the layered merge map.
//
//   g++ -std=c++26 -Wall -Wextra experiments/layered_constexpr.cpp -o /tmp/lc && /tmp/lc
//
// Every scenario below is a `static_assert`, so it is checked DURING COMPILATION.
// If any were false, this file would fail to compile — the tests ARE the build.
// The map is backed by std::vector + std::string_view (both constexpr in C++20+);
// a hash-map backing (std::unordered_map) could NOT be evaluated at compile time.

#include <string_view>
#include <vector>

using std::string_view;

enum class Strategy { Replace, Sum, Multiply, Max, Min };

constexpr int combine(Strategy s, int below, int value, bool first) {
    if (first) return value;  // the first contributor establishes the value
    switch (s) {
        case Strategy::Replace:  return value;
        case Strategy::Sum:      return below + value;
        case Strategy::Multiply: return below * value;
        case Strategy::Max:      return below > value ? below : value;
        case Strategy::Min:      return below < value ? below : value;
    }
    return value;
}

// A flat, constexpr-friendly layered merge map: keeps every contribution, folds on
// observe, removes by source. (Linear scan — fine for small sheets, and the price
// of being usable in a constant expression.)
class LayeredMergeMap {
    struct Entry { string_view key; string_view source; Strategy strategy; int value; };
    std::vector<Entry> m_entries;

public:
    constexpr void apply(string_view source, string_view key, Strategy s, int value) {
        m_entries.push_back({key, source, s, value});
    }
    constexpr void remove(string_view source) {
        std::vector<Entry> kept;
        for (const Entry& e : m_entries)
            if (e.source != source) kept.push_back(e);
        m_entries = kept;
    }
    constexpr int observe(string_view key) const {
        int acc = 0;
        bool first = true;
        for (const Entry& e : m_entries) {
            if (e.key == key) {
                acc = combine(e.strategy, acc, e.value, first);
                first = false;
            }
        }
        return acc;
    }
    constexpr int footprint(string_view source) const {
        int n = 0;
        for (const Entry& e : m_entries)
            if (e.source == source) ++n;
        return n;
    }
};

// -- whole scenarios, evaluated at compile time --------------------------------

constexpr int speed_after(int steps) {
    LayeredMergeMap m;
    m.apply("elf", "speed", Strategy::Replace, 30);
    if (steps >= 1) m.apply("paralyzed", "speed", Strategy::Multiply, 0);
    if (steps >= 2) m.remove("paralyzed");
    return m.observe("speed");
}

constexpr int speed_strongest(bool haste) {
    LayeredMergeMap m;
    m.apply("base", "speed", Strategy::Replace, 30);
    m.apply("boots", "speed", Strategy::Max, 40);
    if (haste) m.apply("haste", "speed", Strategy::Max, 60);
    return m.observe("speed");
}

// Reversibility: the map recovers 30 after the *0 modifier is removed.
static_assert(speed_after(0) == 30, "base");
static_assert(speed_after(1) == 0, "paralyzed multiplies to 0");
static_assert(speed_after(2) == 30, "removing paralyzed restores 30 (no inverse needed)");

// Strongest-takes-precedence, and removal recomputes the new strongest.
static_assert(speed_strongest(true) == 60, "max(30,40,60)");
static_assert(speed_strongest(false) == 40, "max(30,40) after haste is gone");

// Source grouping: one cause fans out over several keys.
static_assert([] {
    LayeredMergeMap m;
    m.apply("bless#1", "attack", Strategy::Sum, 4);
    m.apply("bless#1", "save", Strategy::Sum, 4);
    m.apply("bless#1", "morale", Strategy::Max, 1);
    return m.footprint("bless#1");
}() == 3, "bless touches exactly three keys");

#include <cstdio>
int main() {
    std::puts("compile-time layered-map tests passed (this ran only because they compiled)");
}
