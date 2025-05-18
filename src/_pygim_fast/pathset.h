#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
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
    std::set<fs::path> paths;

    // Constructors
    PathSet() = default;

    PathSet(const fs::path& initial_path) {
        paths.insert(initial_path);
    }

    PathSet(const std::string& initial_path) {
        paths.insert(fs::path(initial_path));
    }

    PathSet(const std::vector<fs::path>& initial_paths) {
        paths.insert(initial_paths.begin(), initial_paths.end());
    }

    PathSet(const std::initializer_list<fs::path>& initial_paths) {
        paths.insert(initial_paths.begin(), initial_paths.end());
    }

    // Static factory method for current working directory
    static PathSet cwd() {
        return PathSet(fs::current_path());
    }

    PathSet(PathSet&& other) noexcept = default;
    PathSet& operator=(PathSet&& other) noexcept = default;

    // Methods
    std::size_t size() const {
        return paths.size();
    }

    bool empty() const {
        return paths.empty();
    }

    std::string repr() const {
        return "PathSet(" + std::to_string(paths.size()) + " entries)";
    }

    std::string str() const {
        std::ostringstream oss;
        for (const auto& p : paths) {
            oss << p.string() << "\n";
        }
        return oss.str();
    }

    bool contains(const fs::path& path) const {
        return paths.find(path) != paths.end();
    }

    PathSet clone() const {
        return PathSet();
    }

    PathSet operator+(const PathSet& other) const {
        PathSet result;
        result.paths.insert(other.paths.begin(), other.paths.end());
        return result;
    }

    // Filter by extension
    PathSet filter_by_extension(const std::string& ext) const {
        PathSet result;
        for (const auto& p : paths) {
            if (p.extension() == ext) {
                result.paths.insert(p);
            }
        }
        return result;
    }

    // Filter by multiple extensions
    PathSet filter_by_extensions(const std::vector<std::string>& exts) const {
        PathSet result;
        for (const auto& p : paths) {
            if (std::find(exts.begin(), exts.end(), p.extension()) != exts.end()) {
                result.paths.insert(p);
            }
        }
        return result;
    }

    // Filter existing paths
    PathSet filter_existing() const {
        PathSet result;
        for (const auto& p : paths) {
            if (fs::exists(p)) {
                result.paths.insert(p);
            }
        }
        return result;
    }

    // Read all files
    std::vector<std::string> read_all_files() const {
        std::vector<std::string> contents;
        contents.reserve(paths.size());

        for (const auto& file : paths) {
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
