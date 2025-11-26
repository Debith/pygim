#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace py = pybind11;

namespace {
constexpr const char *GENERIC_KIND = "generic";
constexpr const char *ODBC_KIND = "odbc";
constexpr const char *MSSQL_KIND = "mssql";

std::string ltrim(std::string value) {
    auto it = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch); });
    value.erase(value.begin(), it);
    return value;
}

std::string rtrim(std::string value) {
    auto it = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch); });
    value.erase(it.base(), value.end());
    return value;
}

std::string trim(std::string value) {
    return rtrim(ltrim(std::move(value)));
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string strip_braces(const std::string &value) {
    if (value.size() >= 2 && value.front() == '{' && value.back() == '}') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

const std::unordered_set<std::string> &sensitive_keys() {
    static const std::unordered_set<std::string> keys = {"pwd", "password", "uid", "user id", "user"};
    return keys;
}

py::object make_mapping_proxy(const py::dict &mapping) {
    static py::object mapping_proxy_factory = py::module::import("types").attr("MappingProxyType");
    return mapping_proxy_factory(mapping);
}

bool looks_like_url(const std::string &raw) {
    std::istringstream iss(raw);
    std::string first_token;
    iss >> first_token;
    return first_token.find("://") != std::string::npos;
}

class ConnectionStringError : public std::runtime_error {
public:
    explicit ConnectionStringError(const std::string &message) : std::runtime_error(message) {}
};

class ConnectionString {
public:
    explicit ConnectionString(std::string raw) : m_raw(std::move(raw)) {}
    virtual ~ConnectionString() = default;

    virtual std::string masked() const { return m_raw; }
    virtual std::string kind() const { return GENERIC_KIND; }
    virtual bool is_mssql() const { return false; }
    virtual std::optional<std::string> driver() const { return std::nullopt; }

    const std::string &raw() const { return m_raw; }
    std::string repr() const {
        std::ostringstream oss;
        oss << "ConnectionString(" << masked() << ")";
        return oss.str();
    }

private:
    std::string m_raw;
};

using ConnectionStringPtr = std::shared_ptr<ConnectionString>;

class KeyValueConnectionString : public ConnectionString {
public:
    using Entry = std::pair<std::string, std::string>;

    KeyValueConnectionString(std::string raw,
                             std::vector<Entry> entries,
                             std::unordered_map<std::string, std::string> lookup)
        : ConnectionString(std::move(raw)), m_entries(std::move(entries)), m_lookup(std::move(lookup)) {}

    std::string masked() const override {
        std::ostringstream oss;
        const auto &sensitive = sensitive_keys();
        bool first = true;
        for (const auto &entry : m_entries) {
            if (!first) {
                oss << ';';
            }
            first = false;
            std::string key_lower = to_lower(entry.first);
            oss << entry.first << '=';
            if (sensitive.count(key_lower)) {
                oss << "***";
            } else {
                oss << entry.second;
            }
        }
        return oss.str();
    }

    std::string kind() const override { return ODBC_KIND; }

    const std::vector<Entry> &entries() const { return m_entries; }

    std::optional<std::string> get_value(const std::string &key) const {
        std::string lowered = to_lower(key);
        auto it = m_lookup.find(lowered);
        if (it == m_lookup.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<std::string> driver() const override {
        return get_value("driver");
    }

private:
    std::vector<Entry> m_entries;
    std::unordered_map<std::string, std::string> m_lookup;
};

class MssqlConnectionString : public KeyValueConnectionString {
public:
    MssqlConnectionString(std::string raw,
                          std::vector<Entry> entries,
                          std::unordered_map<std::string, std::string> lookup)
        : KeyValueConnectionString(std::move(raw), std::move(entries), std::move(lookup)) {}

    std::string kind() const override { return MSSQL_KIND; }
    bool is_mssql() const override { return true; }

    std::optional<std::string> server() const { return get_value("server"); }
    std::optional<std::string> database() const { return get_value("database"); }
};

class UrlConnectionString : public ConnectionString {
public:
    UrlConnectionString(std::string raw,
                        std::string scheme,
                        std::optional<std::string> username,
                        std::optional<std::string> password,
                        std::optional<std::string> host,
                        std::optional<int> port,
                        std::optional<std::string> database,
                        std::string query,
                        py::dict params)
        : ConnectionString(std::move(raw)),
          m_scheme(std::move(scheme)),
          m_username(std::move(username)),
          m_password(std::move(password)),
          m_host(std::move(host)),
          m_port(port),
          m_database(std::move(database)),
          m_query(std::move(query)),
          m_params_storage(std::move(params)),
          m_params_proxy(make_mapping_proxy(m_params_storage)) {}

    std::string masked() const override {
        if (!m_password) {
            return raw();
        }
        std::ostringstream oss;
        if (!m_scheme.empty()) {
            oss << m_scheme << "://";
        }
        if (m_username) {
            oss << *m_username;
            if (m_password) {
                oss << ":***";
            }
            oss << '@';
        }
        if (!m_username && m_password) {
            oss << ":***@";
        }
        if (m_host) {
            oss << *m_host;
        }
        if (m_port) {
            oss << ':' << *m_port;
        }
        if (m_database) {
            oss << '/' << *m_database;
        }
        if (!m_query.empty()) {
            oss << '?' << m_query;
        }
        return oss.str();
    }

    std::string kind() const override { return m_scheme.empty() ? "url" : m_scheme; }

    bool is_mssql() const override {
        std::string lowered = to_lower(m_scheme);
        return lowered.rfind("mssql", 0) == 0;
    }

    std::optional<std::string> username() const { return m_username; }
    std::optional<std::string> password() const { return m_password; }
    std::optional<std::string> host() const { return m_host; }
    std::optional<int> port() const { return m_port; }
    std::optional<std::string> database() const { return m_database; }
    py::object params() const { return m_params_proxy; }
    std::string scheme() const { return m_scheme; }

private:
    std::string m_scheme;
    std::optional<std::string> m_username;
    std::optional<std::string> m_password;
    std::optional<std::string> m_host;
    std::optional<int> m_port;
    std::optional<std::string> m_database;
    std::string m_query;
    py::dict m_params_storage;
    py::object m_params_proxy;
};

class ConnectionStringFactory {
public:
    static ConnectionStringPtr from_string(const std::string &raw);
    static py::object coerce(const py::object &value);
};

ConnectionStringPtr parse_key_value(const std::string &raw) {
    using Entry = KeyValueConnectionString::Entry;
    std::vector<Entry> entries;
    std::unordered_map<std::string, std::string> lookup;
    std::string buffer;
    std::istringstream ss(raw);
    while (std::getline(ss, buffer, ';')) {
        std::string chunk = trim(buffer);
        if (chunk.empty()) {
            continue;
        }
        auto pos = chunk.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = trim(chunk.substr(0, pos));
        std::string value = trim(chunk.substr(pos + 1));
        std::string clean_value = strip_braces(value);
        entries.emplace_back(key, clean_value);
        lookup[to_lower(key)] = clean_value;
    }
    if (entries.empty()) {
        throw ConnectionStringError("No key=value entries found");
    }
    auto is_mssql_driver = [](const std::string &driver_lower) {
        return driver_lower.find("sql server") != std::string::npos ||
               driver_lower.find("msodbc") != std::string::npos ||
               driver_lower.find("sqlncli") != std::string::npos;
    };

    auto driver_it = lookup.find("driver");
    if (driver_it != lookup.end() && is_mssql_driver(to_lower(driver_it->second))) {
        return std::make_shared<MssqlConnectionString>(raw, entries, lookup);
    }
    return std::make_shared<KeyValueConnectionString>(raw, entries, lookup);
}

ConnectionStringPtr parse_url(const std::string &raw) {
    py::module urllib = py::module::import("urllib.parse");
    py::object urlparse = urllib.attr("urlparse");
    py::object parsed = urlparse(raw);

    std::string scheme = parsed.attr("scheme").cast<std::string>();

    auto to_optional_str = [](const py::object &obj) -> std::optional<std::string> {
        if (obj.is_none()) {
            return std::nullopt;
        }
        return obj.cast<std::string>();
    };

    std::optional<std::string> username = to_optional_str(parsed.attr("username"));
    std::optional<std::string> password = to_optional_str(parsed.attr("password"));
    std::optional<std::string> host = to_optional_str(parsed.attr("hostname"));

    std::optional<int> port;
    py::object port_obj = parsed.attr("port");
    if (!port_obj.is_none()) {
        port = port_obj.cast<int>();
    }

    std::optional<std::string> database;
    std::string path = parsed.attr("path").cast<std::string>();
    if (!path.empty()) {
        if (path.front() == '/') {
            path.erase(path.begin());
        }
        if (!path.empty()) {
            database = path;
        }
    }

    std::string query = parsed.attr("query").cast<std::string>();
    py::object parse_qs = urllib.attr("parse_qs");
    py::dict params = parse_qs(query);

    return std::make_shared<UrlConnectionString>(raw,
                                                 scheme,
                                                 username,
                                                 password,
                                                 host,
                                                 port,
                                                 database,
                                                 query,
                                                 std::move(params));
}

ConnectionStringPtr ConnectionStringFactory::from_string(const std::string &raw_input) {
    std::string trimmed = trim(raw_input);
    if (trimmed.empty()) {
        throw ConnectionStringError("Connection string cannot be empty");
    }
    if (looks_like_url(trimmed)) {
        return parse_url(trimmed);
    }
    return parse_key_value(trimmed);
}

py::object ConnectionStringFactory::coerce(const py::object &value) {
    if (!value || value.is_none()) {
        return py::none();
    }
    if (py::isinstance<ConnectionString>(value)) {
        return value;
    }
    std::string raw = value.cast<std::string>();
    return py::cast(from_string(raw));
}

} // namespace

PYBIND11_MODULE(connection, m) {
    py::register_exception<ConnectionStringError>(m, "ConnectionStringError", PyExc_ValueError);

    py::class_<ConnectionString, ConnectionStringPtr>(m, "ConnectionString")
        .def("masked", &ConnectionString::masked)
        .def("kind", &ConnectionString::kind)
        .def("is_mssql", &ConnectionString::is_mssql)
        .def_property_readonly("driver", &ConnectionString::driver)
        .def_property_readonly("raw", &ConnectionString::raw)
        .def("__str__", &ConnectionString::masked)
        .def("__repr__", &ConnectionString::repr);

    py::class_<KeyValueConnectionString, ConnectionString, std::shared_ptr<KeyValueConnectionString>>(m, "KeyValueConnectionString")
        .def_property_readonly("entries", [](const KeyValueConnectionString &self) {
            const auto &entries = self.entries();
            py::tuple result(entries.size());
            for (size_t i = 0; i < entries.size(); ++i) {
                result[i] = py::make_tuple(entries[i].first, entries[i].second);
            }
            return result;
        })
        .def("get",
             [](const KeyValueConnectionString &self, const std::string &key, py::object default_value) -> py::object {
                 auto value = self.get_value(key);
                 if (value) {
                     return py::cast(*value);
                 }
                 if (default_value.is_none()) {
                     return py::object(py::none());
                 }
                 return default_value;
             },
             py::arg("key"),
             py::arg("default") = py::none())
        .def("kind", &KeyValueConnectionString::kind);

    py::class_<MssqlConnectionString, KeyValueConnectionString, std::shared_ptr<MssqlConnectionString>>(m, "MssqlConnectionString")
        .def_property_readonly("server", &MssqlConnectionString::server)
        .def_property_readonly("database", &MssqlConnectionString::database)
        .def("is_mssql", &MssqlConnectionString::is_mssql)
        .def("kind", &MssqlConnectionString::kind);

    py::class_<UrlConnectionString, ConnectionString, std::shared_ptr<UrlConnectionString>>(m, "UrlConnectionString")
        .def_property_readonly("scheme", &UrlConnectionString::scheme)
        .def_property_readonly("username", &UrlConnectionString::username)
        .def_property_readonly("password", &UrlConnectionString::password)
        .def_property_readonly("host", &UrlConnectionString::host)
        .def_property_readonly("port", &UrlConnectionString::port)
        .def_property_readonly("database", &UrlConnectionString::database)
        .def_property_readonly("params", &UrlConnectionString::params)
        .def("is_mssql", &UrlConnectionString::is_mssql)
        .def("kind", &UrlConnectionString::kind);

    py::class_<ConnectionStringFactory>(m, "ConnectionStringFactory")
        .def_static("coerce", &ConnectionStringFactory::coerce, py::arg("value"))
        .def_static("from_string", &ConnectionStringFactory::from_string, py::arg("raw"));

    m.attr("__all__") = py::make_tuple("ConnectionString",
                                        "ConnectionStringError",
                                        "ConnectionStringFactory",
                                        "KeyValueConnectionString",
                                        "MssqlConnectionString",
                                        "UrlConnectionString");
}
