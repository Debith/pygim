#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#define PYBIND11_HAS_FILESYSTEM_IS_OPTIONAL
#include <pybind11/stl/filesystem.h>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <mutex>
#include <execution>

namespace py = pybind11;
namespace fs = std::filesystem;

bool match_pattern(const std::string& pattern, const std::string& str) {
    const char *p = pattern.c_str();
    const char *s = str.c_str();
    const char *star = nullptr;
    const char *ss = nullptr;

    // If the pattern is empty, it matches nothing
    if (*p == '\0') {
        return false;
    }

    // Optimization for "*" pattern
    if (pattern == "*") {
        return true;
    }

    // Optimization for "*.*" pattern
    if (pattern == "*.*") {
        // Check if 'str' contains a dot
        return str.find('.') != std::string::npos;
    }

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


class PathSet {
public:
    // TODO: m_paths is being accessed by pybind11 bindings. Ideally,
    // this should be private, but then need to find a way to expose iterators.
    std::set<fs::path> m_paths;

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

    PathSet(const PathSet& other) {
        m_paths = other.m_paths;
    }

    // Static factory method for current working directory
    static PathSet cwd() {
        return PathSet(fs::current_path());
    }

    PathSet(PathSet&& other) noexcept = default;
    PathSet& operator=(PathSet&& other) noexcept = default;

    // Methods
    std::size_t size() const {
        return m_paths.size();
    }

    bool empty() const {
        return m_paths.empty();
    }

    std::string repr() const {
        return "PathSet(" + std::to_string(m_paths.size()) + " entries)";
    }

    std::string str() const {
        std::ostringstream oss;
        for (const auto& p : m_paths) {
            oss << p.string() << "\n";
        }
        return oss.str();
    }

    bool contains(const fs::path& path) const {
        return m_paths.find(path) != m_paths.end();
    }

    PathSet clone() const {
        return PathSet(*this);
    }

    bool operator==(const PathSet& other) const {
        if (this == &other) {
            return true;
        }

        if (m_paths.size() != other.m_paths.size()) {
            return false;
        }

        // Check if all elements in this set are in the other set
        for (const auto& p : m_paths) {
            if (other.m_paths.find(p) == other.m_paths.end()) {
                return false;
            }
        }
        return true;
    }

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

    // Filter by extension
    PathSet filter_by_extension(const std::string& ext) const {
        PathSet result;
        for (const auto& p : m_paths) {
            if (p.extension() == ext) {
                result.m_paths.insert(p);
            }
        }
        return result;
    }

    // Filter by multiple extensions
    PathSet filter_by_extensions(const std::vector<std::string>& exts) const {
        PathSet result;
        for (const auto& p : m_paths) {
            if (std::find(exts.begin(), exts.end(), p.extension()) != exts.end()) {
                result.m_paths.insert(p);
            }
        }
        return result;
    }

    // Filter existing m_paths
    PathSet filter_existing() const {
        PathSet result;
        for (const auto& p : m_paths) {
            if (fs::exists(p)) {
                result.m_paths.insert(p);
            }
        }
        return result;
    }

    // Read all files
    std::vector<std::string> read_all_files() const {
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
};
