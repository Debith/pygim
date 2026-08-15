// The layered merge map on std::flat_map — a constexpr, standard associative
// container (sorted-vector backed). Requires a libstdc++ with constexpr flat_map:
//
//   g++-16 -std=c++26 -Wall -Wextra experiments/layered_flatmap.cpp -o /tmp/lf && /tmp/lf
//
// Everything below is static_assert'd, so the whole engine — forward fold, the
// source reverse-index, and source-grouped reversible removal — is verified DURING
// COMPILATION, using real maps (not hand-rolled vectors).

#include <flat_map>
#include <string_view>
#include <vector>
#include <algorithm>

using std::string_view;

enum class Strategy { Replace, Sum, Multiply, Max, Min };

constexpr int combine(Strategy s, int below, int value, bool first) {
    if (first) return value;
    switch (s) {
        case Strategy::Replace:  return value;
        case Strategy::Sum:      return below + value;
        case Strategy::Multiply: return below * value;
        case Strategy::Max:      return below > value ? below : value;
        case Strategy::Min:      return below < value ? below : value;
    }
    return value;
}

class LayeredMergeMap {
    struct Contribution { string_view source; Strategy strategy; int value; };
    std::flat_map<string_view, std::vector<Contribution>> m_by_key;    // forward: fold
    std::flat_map<string_view, std::vector<string_view>> m_footprint;  // reverse: locate/remove

public:
    constexpr void apply(string_view source, string_view key, Strategy s, int value) {
        m_by_key[key].push_back({source, s, value});
        m_footprint[source].push_back(key);
    }

    constexpr void remove(string_view source) {           // O(footprint) — no full scan
        auto it = m_footprint.find(source);
        if (it == m_footprint.end()) return;
        for (string_view key : it->second) {
            auto& vec = m_by_key[key];
            std::erase_if(vec, [&](const Contribution& c) { return c.source == source; });
        }
        m_footprint.erase(it);
    }

    constexpr int observe(string_view key) const {
        auto it = m_by_key.find(key);
        if (it == m_by_key.end()) return 0;
        int acc = 0;
        bool first = true;
        for (const Contribution& c : it->second) {
            acc = combine(c.strategy, acc, c.value, first);
            first = false;
        }
        return acc;
    }

    constexpr int footprint_size(string_view source) const {
        auto it = m_footprint.find(source);
        return it == m_footprint.end() ? 0 : static_cast<int>(it->second.size());
    }
};

// -- whole scenarios, checked at compile time ---------------------------------

constexpr int speed_after(int steps) {
    LayeredMergeMap m;
    m.apply("elf", "speed", Strategy::Replace, 30);
    if (steps >= 1) m.apply("paralyzed", "speed", Strategy::Multiply, 0);
    if (steps >= 2) m.remove("paralyzed");
    return m.observe("speed");
}

constexpr int footprint_then_remove() {
    LayeredMergeMap m;
    m.apply("elf", "speed", Strategy::Replace, 30);
    m.apply("bless#1", "attack", Strategy::Sum, 4);
    m.apply("bless#1", "save", Strategy::Sum, 4);
    m.apply("bless#1", "morale", Strategy::Max, 1);
    const int fp = m.footprint_size("bless#1");   // 3, in O(footprint)
    m.remove("bless#1");                           // drops the whole fan-out
    return fp * 100 + m.observe("attack");         // 3*100 + 0 -> 300
}

static_assert(speed_after(0) == 30, "base");
static_assert(speed_after(1) == 0, "paralyzed *0");
static_assert(speed_after(2) == 30, "reversible: removing paralyzed restores 30");
static_assert(footprint_then_remove() == 300, "source index: footprint 3, then removed");

#include <cstdio>
int main() {
    std::puts("constexpr std::flat_map layered map: compile-time tests passed");
}
