#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <fstream>
#include <sstream>
#include <mutex>
#include <execution>

namespace fs = std::filesystem;

[[nodiscard]] inline bool match_pattern(std::string_view pattern, std::string_view str) {
    // Fast-path checks (no allocation needed)
    if (pattern.empty()) return false;
    if (pattern == "*") return true;
    if (pattern == "*.*") return str.find('.') != std::string_view::npos;

    // Pointer-chase algorithm requires null-terminated strings
    std::string p_str(pattern), s_str(str);
    const char *p = p_str.c_str();
    const char *s = s_str.c_str();
    const char *star = nullptr;
    const char *ss = nullptr;

    while (*s) {
        if (*p == '?' || *p == *s) {
            // Characters match or pattern has '?', move to the next character
            ++p;
            ++s;
        } else if (*p == '*') {
            // '*' found in pattern, remember this position
            star = p++;
            ss = s;
        } else if (star) {
            // Last pattern pointer was '*', backtrack
            p = star + 1;
            s = ++ss;
        } else {
            // Current characters didn't match, and no '*' to backtrack to
            return false;
        }
    }

    // Consume any remaining '*' in the pattern
    while (*p == '*') {
        ++p;
    }

    // If we've reached the end of the pattern, it's a match
    return *p == '\0';
}


using entry   = fs::directory_entry;

/*-------------  A "callable" filter  ----------------*/
struct Filter
{
    std::function<bool(const entry&)> pred;

    bool operator()(const entry& e) const { return pred(e); }

    /* Boolean algebra */
    friend Filter operator&(Filter a, Filter b)
    { return { [=](auto& e){ return a(e) && b(e); } }; }

    friend Filter operator|(Filter a, Filter b)
    { return { [=](auto& e){ return a(e) || b(e); } }; }

    friend Filter operator!(Filter a)
    { return { [=](auto& e){ return !a(e); } }; }
};

/* small helpers ("ext", "size_gt", …) */
inline Filter ext(std::string_view x)
{
    return { [x](const entry& e){ return e.path().extension() == x; } };
}



class PathSet {
public:
    auto begin() const { return m_paths.begin(); }
    auto end()   const { return m_paths.end();   }

    friend std::ostream& operator<<(std::ostream& os, PathSet const& s)
    {
        for (auto& p : s.m_paths) os << p << '\n';
        return os;
    }

    // Constructors
    PathSet() = default;

    PathSet(const fs::path& initial_path) {
        m_paths.insert(initial_path);
    }

    PathSet(const std::string& initial_path) {
        m_paths.insert(fs::path(initial_path));
    }

    PathSet(const std::vector<fs::path>& initial_paths) {
        m_paths.insert(initial_paths.begin(), initial_paths.end());
    }

    PathSet(const std::initializer_list<fs::path>& initial_paths) {
        m_paths.insert(initial_paths.begin(), initial_paths.end());
    }

    PathSet(const PathSet& other) = default;

    // Static factory method for current working directory
    static PathSet cwd() {
        return PathSet(fs::current_path());
    }

    PathSet(PathSet&& other) noexcept = default;
    PathSet& operator=(PathSet&& other) noexcept = default;

    // Methods
    [[nodiscard]] std::size_t size() const {
        return m_paths.size();
    }

    [[nodiscard]] bool empty() const {
        return m_paths.empty();
    }

    [[nodiscard]] std::string repr() const {
        return "PathSet(" + std::to_string(m_paths.size()) + " entries)";
    }

    [[nodiscard]] std::string str() const {
        std::ostringstream oss;
        for (const auto& p : m_paths) {
            oss << p.string() << "\n";
        }
        return oss.str();
    }

    [[nodiscard]] bool contains(const fs::path& path) const {
        return m_paths.find(path) != m_paths.end();
    }

    [[nodiscard]] PathSet clone() const {
        return PathSet(*this);
    }

    bool operator==(const PathSet& other) const = default;

    PathSet operator+(const PathSet& other) const {
        PathSet result;
        result.m_paths.insert(other.m_paths.begin(), other.m_paths.end());
        return result;
    }

    PathSet& operator-=(const PathSet& other) {
        for (const auto& p : other.m_paths) {
            m_paths.erase(p);
        }
        return *this;
    }

    PathSet& operator-=(const std::string& path) {
        m_paths.erase(fs::path(path));
        return *this;
    }

    // Read all files
    [[nodiscard]] std::vector<std::string> read_all_files() const {
        std::vector<std::string> contents;
        contents.reserve(m_paths.size());

        for (const auto& file : m_paths) {
            if (fs::is_regular_file(file)) {
                std::ifstream ifs(file);
                std::stringstream buffer;
                buffer << ifs.rdbuf();
                contents.push_back(buffer.str());
            }
        }
        return contents;
    }

    // Other methods as needed

private:
    std::set<fs::path> m_paths;
};

/*------------  Lazy query = source + predicate  -------------*/
template<class Source>
class Query
{

    Source const* src_;   // we do NOT copy the path list
    Filter        f_;     // combined predicate

public:
    Query(Source const* s, Filter f) : src_(s), f_(std::move(f)) {}

    /* keep piling filters with & and | */
    friend Query operator&(Query q, Filter g) { return { q.src_, q.f_ & g }; }
    friend Query operator|(Query q, Filter g) { return { q.src_, q.f_ | g }; }

    /* evaluate on demand */
    [[nodiscard]] PathSet eval() const
    {
        std::vector<fs::path> out;
        for (auto& p : *src_) {
            entry e(p);
            if (f_(e))
                out.push_back(p);
        }
        return PathSet{std::move(out)};
    }

    /* implicit conversion lets you write: PathSet s = paths & … | … ; */
    operator PathSet() const { return eval(); }
};// end of template<class Source> class Query

using QueryPS = Query<PathSet>;  // alias for binding

/* first kick-off operators: PathSet &/| Filter → Query */
inline Query<PathSet> operator&(PathSet const& s, Filter f) {
    return { &s, std::move(f) };
}
inline Query<PathSet> operator|(PathSet const& s, Filter f) {
    return { &s, std::move(f) };
}
