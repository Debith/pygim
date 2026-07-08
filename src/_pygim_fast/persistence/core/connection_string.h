// persistence/core/connection_string.h
// Connection-string value object + builder + source factories.
//
// Consolidates connection-string handling that used to be scattered across
// Python (_translate_conn_str/_dsn_quote), the mssql layer (ensure_packet_size)
// and the adapter (token-conflict scan). Pybind-free and ODBC-free: pure std.
//
// - ConnectionString        immutable value; render(Masked) by default so a
//                           credential never leaks into a log or an error.
// - ConnectionStringBuilder fluent assembly → ConnectionString.
// - ConnectionStringFactory abstract factory; OdbcDsnFactory (raw key=value;)
//                           and SqlAlchemyUrlFactory (mssql+pyodbc:// URLs).
//
// The pure keyword/predicate helpers in `detail` are constexpr and covered by
// static_assert tests, mirroring core::plan_autowiring.

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pygim::core {

/// Whether render() reveals secrets. Masked is the default everywhere except
/// the SQLDriverConnect boundary.
enum class Reveal { Masked, WithSecrets };

/// Canonical identity of a well-known ODBC keyword (case-insensitive).
enum class ConnKey {
    Driver, Server, Database, Uid, Pwd, TrustedConnection,
    Authentication, PacketSize, Dsn, Encrypt, TrustServerCertificate,
    Other  //!< passthrough / unknown key (spelling preserved verbatim)
};

namespace detail {

/// ASCII case-insensitive equality — constexpr, no allocation.
[[nodiscard]] constexpr bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + 32);
        if (ca != cb) return false;
    }
    return true;
}

/// Map an ODBC keyword spelling to its canonical ConnKey (constexpr).
[[nodiscard]] constexpr ConnKey canonical_key(std::string_view k) {
    if (iequals(k, "driver")) return ConnKey::Driver;
    if (iequals(k, "server") || iequals(k, "address") || iequals(k, "addr"))
        return ConnKey::Server;
    if (iequals(k, "database") || iequals(k, "initial catalog"))
        return ConnKey::Database;
    if (iequals(k, "uid") || iequals(k, "user id") || iequals(k, "user"))
        return ConnKey::Uid;
    if (iequals(k, "pwd") || iequals(k, "password")) return ConnKey::Pwd;
    if (iequals(k, "trusted_connection")) return ConnKey::TrustedConnection;
    if (iequals(k, "authentication")) return ConnKey::Authentication;
    if (iequals(k, "packetsize")) return ConnKey::PacketSize;
    if (iequals(k, "dsn")) return ConnKey::Dsn;
    if (iequals(k, "encrypt")) return ConnKey::Encrypt;
    if (iequals(k, "trustservercertificate")) return ConnKey::TrustServerCertificate;
    return ConnKey::Other;
}

/// A value is secret if its key is a password (masked in Masked render).
[[nodiscard]] constexpr bool is_secret_key(ConnKey k) {
    return k == ConnKey::Pwd;
}

/// Canonical spelling for a well-known key (used in diagnostics).
[[nodiscard]] constexpr std::string_view key_label(ConnKey k) {
    switch (k) {
        case ConnKey::Driver:                 return "Driver";
        case ConnKey::Server:                 return "Server";
        case ConnKey::Database:               return "Database";
        case ConnKey::Uid:                    return "UID";
        case ConnKey::Pwd:                    return "PWD";
        case ConnKey::TrustedConnection:      return "Trusted_Connection";
        case ConnKey::Authentication:         return "Authentication";
        case ConnKey::PacketSize:             return "PacketSize";
        case ConnKey::Dsn:                    return "DSN";
        case ConnKey::Encrypt:                return "Encrypt";
        case ConnKey::TrustServerCertificate: return "TrustServerCertificate";
        case ConnKey::Other:                  return "";
    }
    return "";
}

/// A URL carries a scheme://; a raw ODBC DSN does not.
[[nodiscard]] constexpr bool looks_like_url(std::string_view source) {
    return source.find("://") != std::string_view::npos;
}

[[nodiscard]] constexpr std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

/// Brace-quote an ODBC value when it needs it: values containing ';', '{', '}'
/// or surrounding whitespace are wrapped in braces (embedded '}' doubled),
/// mirroring the retired Python _dsn_quote.
[[nodiscard]] inline std::string dsn_quote(std::string_view value) {
    const bool needs = value != trim(value) ||
        value.find_first_of(";{}") != std::string_view::npos;
    if (!needs) return std::string(value);
    std::string out = "{";
    for (char c : value) {
        if (c == '}') out += "}}";
        else out += c;
    }
    out += "}";
    return out;
}

/// Percent-decode. When `plus_is_space`, also decode '+' → ' '
/// (application/x-www-form-urlencoded, as used in URL query strings).
[[nodiscard]] inline std::string pct_decode(std::string_view s, bool plus_is_space) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        if (plus_is_space && s[i] == '+') out += ' ';
        else out += s[i];
    }
    return out;
}

}  // namespace detail

/// One connection-string attribute: canonical key, verbatim spelling, value.
struct ConnAttribute {
    ConnKey     key;
    std::string name;   //!< original spelling (used for Other passthrough keys)
    std::string value;  //!< unbraced, decoded value
};

class ConnectionStringBuilder;  // fwd

/// Immutable connection-string value object. Construct via ConnectionString::parse
/// or a ConnectionStringBuilder — there is no public constructor.
class ConnectionString {
public:
    /// Parse either an ODBC DSN (raw key=value;) or a SQLAlchemy-style URL.
    [[nodiscard]] static ConnectionString parse(std::string_view source);

    /// Render back to a DSN string. Masked (default) redacts passwords for
    /// logs / error messages / repr; WithSecrets is for SQLDriverConnect only.
    [[nodiscard]] std::string render(Reveal reveal = Reveal::Masked) const {
        if (reveal == Reveal::WithSecrets && !m_connect.empty()) {
            return m_connect;  // faithful reconnect string (raw DSN kept verbatim)
        }
        return render_attrs(reveal);
    }

    [[nodiscard]] bool has(ConnKey k) const {
        return std::ranges::any_of(m_attrs, [k](const auto& a) { return a.key == k; });
    }
    [[nodiscard]] std::optional<std::string_view> get(ConnKey k) const {
        for (const auto& a : m_attrs)
            if (a.key == k) return std::string_view(a.value);
        return std::nullopt;
    }
    [[nodiscard]] std::optional<std::string_view> driver()   const { return get(ConnKey::Driver); }
    [[nodiscard]] std::optional<std::string_view> server()   const { return get(ConnKey::Server); }
    [[nodiscard]] std::optional<std::string_view> database() const { return get(ConnKey::Database); }
    [[nodiscard]] bool is_named_dsn() const { return has(ConnKey::Dsn); }

    /// Keys that conflict with access-token auth (design doc 2.1). Empty = OK.
    [[nodiscard]] std::vector<ConnKey> token_conflicts() const {
        std::vector<ConnKey> out;
        for (ConnKey k : {ConnKey::Uid, ConnKey::Pwd,
                          ConnKey::TrustedConnection, ConnKey::Authentication}) {
            if (has(k)) out.push_back(k);
        }
        return out;
    }

    /// Functional update: a copy with PacketSize set/replaced.
    [[nodiscard]] ConnectionString with_packet_size(int packet_size) const {
        return with(ConnKey::PacketSize, "PacketSize", std::to_string(packet_size));
    }
    [[nodiscard]] ConnectionString with(ConnKey key, std::string_view name,
                                        std::string_view value) const {
        std::vector<ConnAttribute> attrs = m_attrs;
        auto it = std::ranges::find_if(attrs, [key](const auto& a) { return a.key == key; });
        if (it != attrs.end()) it->value = std::string(value);
        else attrs.push_back({key, std::string(name), std::string(value)});
        ConnectionString cs(std::move(attrs), {});
        cs.m_connect = cs.render_attrs(Reveal::WithSecrets);
        return cs;
    }

    /// Value equality over normalized attributes (order-independent); used as
    /// the parallel-load cache key.
    [[nodiscard]] friend bool operator==(const ConnectionString& a,
                                         const ConnectionString& b) {
        if (a.m_attrs.size() != b.m_attrs.size()) return false;
        for (const auto& x : a.m_attrs) {
            const bool matched = std::ranges::any_of(b.m_attrs, [&](const auto& y) {
                return x.key == y.key &&
                       (x.key != ConnKey::Other || detail::iequals(x.name, y.name)) &&
                       x.value == y.value;
            });
            if (!matched) return false;
        }
        return true;
    }

    [[nodiscard]] const std::vector<ConnAttribute>& attributes() const { return m_attrs; }

private:
    friend class ConnectionStringBuilder;

    ConnectionString(std::vector<ConnAttribute> attrs, std::string connect)
        : m_attrs(std::move(attrs)), m_connect(std::move(connect)) {}

    [[nodiscard]] std::string render_attrs(Reveal reveal) const {
        std::string out;
        for (const auto& a : m_attrs) {
            out += a.name;
            out += '=';
            if (reveal == Reveal::Masked && detail::is_secret_key(a.key)) {
                out += "***";
            } else if (a.key == ConnKey::Driver) {
                // Driver names conventionally braced.
                out += a.value.starts_with('{') ? a.value : ("{" + a.value + "}");
            } else {
                out += detail::dsn_quote(a.value);
            }
            out += ';';
        }
        return out;
    }

    std::vector<ConnAttribute> m_attrs;
    std::string                m_connect;  //!< verbatim reconnect DSN ("" → derive from attrs)
};

/// Fluent builder → immutable ConnectionString.
class ConnectionStringBuilder {
public:
    ConnectionStringBuilder& driver(std::string_view v)   { return set(ConnKey::Driver, "Driver", v); }
    ConnectionStringBuilder& database(std::string_view v) { return set(ConnKey::Database, "Database", v); }
    ConnectionStringBuilder& uid(std::string_view v)      { return set(ConnKey::Uid, "UID", v); }
    ConnectionStringBuilder& pwd(std::string_view v)      { return set(ConnKey::Pwd, "PWD", v); }
    ConnectionStringBuilder& named_dsn(std::string_view v){ return set(ConnKey::Dsn, "DSN", v); }

    ConnectionStringBuilder& server(std::string_view host, std::optional<int> port = std::nullopt) {
        std::string v(host);
        if (port) { v += ','; v += std::to_string(*port); }
        return set(ConnKey::Server, "Server", v);
    }

    ConnectionStringBuilder& set(ConnKey key, std::string_view name, std::string_view value) {
        m_attrs.push_back({key, std::string(name), std::string(value)});
        return *this;
    }
    ConnectionStringBuilder& set_raw(std::string_view name, std::string_view value) {
        return set(detail::canonical_key(name), name, value);
    }

    /// Preserve a verbatim reconnect string (raw DSNs round-trip unchanged).
    ConnectionStringBuilder& verbatim(std::string connect) {
        m_verbatim = std::move(connect);
        return *this;
    }

    [[nodiscard]] ConnectionString build() {
        ConnectionString cs(std::move(m_attrs),
                            m_verbatim ? std::move(*m_verbatim) : std::string{});
        if (cs.m_connect.empty()) cs.m_connect = cs.render_attrs(Reveal::WithSecrets);
        return cs;
    }

    /// Delegate a whole source string to the factories (the "builder uses
    /// factory" path). Equivalent to ConnectionString::parse.
    [[nodiscard]] static ConnectionString from_source(std::string_view source) {
        return ConnectionString::parse(source);
    }

private:
    std::vector<ConnAttribute>  m_attrs;
    std::optional<std::string>  m_verbatim;
};

// ── Abstract factory + concrete source parsers ──────────────────────────────

/// One product per source dialect.
struct ConnectionStringFactory {
    virtual ~ConnectionStringFactory() = default;
    [[nodiscard]] virtual bool accepts(std::string_view source) const = 0;
    [[nodiscard]] virtual ConnectionString create(std::string_view source) const = 0;
};

/// Raw ODBC DSN: tokenize `key=value;` (brace-aware), keep the original verbatim.
struct OdbcDsnFactory final : ConnectionStringFactory {
    [[nodiscard]] bool accepts(std::string_view source) const override {
        return !detail::looks_like_url(source);
    }
    [[nodiscard]] ConnectionString create(std::string_view dsn) const override {
        ConnectionStringBuilder b;
        std::size_t i = 0, n = dsn.size();
        while (i < n) {
            while (i < n && dsn[i] == ';') ++i;
            if (i >= n) break;
            const std::size_t eq = dsn.find('=', i);
            if (eq == std::string_view::npos) break;
            const std::string_view name = detail::trim(dsn.substr(i, eq - i));
            std::size_t v = eq + 1;
            std::string value;
            if (v < n && dsn[v] == '{') {           // braced value
                ++v;
                while (v < n) {
                    if (dsn[v] == '}') {
                        if (v + 1 < n && dsn[v + 1] == '}') { value += '}'; v += 2; continue; }
                        ++v;
                        break;
                    }
                    value += dsn[v++];
                }
                while (v < n && dsn[v] != ';') ++v;
            } else {
                std::size_t semi = dsn.find(';', v);
                if (semi == std::string_view::npos) semi = n;
                value = std::string(detail::trim(dsn.substr(v, semi - v)));
                v = semi;
            }
            if (!name.empty())
                b.set(detail::canonical_key(name), name, value);
            i = v;
        }
        b.verbatim(std::string(dsn));  // raw DSN reconnects byte-for-byte
        return b.build();
    }
};

/// SQLAlchemy-style URL: mssql+pyodbc://user:pass@host:port/db?driver=…&k=v
/// Absorbs the retired Python _translate_conn_str, including the odbc_connect
/// passthrough and the named-DSN (hostless) form.
struct SqlAlchemyUrlFactory final : ConnectionStringFactory {
    [[nodiscard]] bool accepts(std::string_view source) const override {
        return detail::looks_like_url(source) &&
               detail::iequals(scheme(source).substr(0, 5), "mssql");
    }
    [[nodiscard]] ConnectionString create(std::string_view url) const override {
        const std::size_t sep = url.find("://");
        std::string_view rest = url.substr(sep + 3);

        std::string_view query;
        if (const std::size_t q = rest.find('?'); q != std::string_view::npos) {
            query = rest.substr(q + 1);
            rest = rest.substr(0, q);
        }
        std::string_view path;
        std::string_view authority = rest;
        if (const std::size_t p = rest.find('/'); p != std::string_view::npos) {
            path = rest.substr(p + 1);
            authority = rest.substr(0, p);
        }

        // authority = [user[:pass]@]host[:port]
        std::string user, pass;
        bool has_pass = false;
        std::string_view hostport = authority;
        if (const std::size_t at = authority.rfind('@'); at != std::string_view::npos) {
            std::string_view userinfo = authority.substr(0, at);
            hostport = authority.substr(at + 1);
            if (const std::size_t c = userinfo.find(':'); c != std::string_view::npos) {
                user = detail::pct_decode(userinfo.substr(0, c), false);
                pass = detail::pct_decode(userinfo.substr(c + 1), false);
                has_pass = true;
            } else {
                user = detail::pct_decode(userinfo, false);
            }
        }
        std::string host;
        std::optional<int> port;
        if (const std::size_t c = hostport.rfind(':'); c != std::string_view::npos) {
            host = detail::pct_decode(hostport.substr(0, c), false);
            const std::string_view p = hostport.substr(c + 1);
            if (!p.empty()) port = std::stoi(std::string(p));
        } else {
            host = detail::pct_decode(hostport, false);
        }
        const std::string database = detail::pct_decode(path, false);

        // Query: form-decoded key=value pairs; odbc_connect wins verbatim.
        std::string driver;
        std::vector<std::pair<std::string, std::string>> passthrough;
        for (std::size_t i = 0; i < query.size();) {
            std::size_t amp = query.find('&', i);
            if (amp == std::string_view::npos) amp = query.size();
            std::string_view pair = query.substr(i, amp - i);
            i = amp + 1;
            if (pair.empty()) continue;
            const std::size_t eq = pair.find('=');
            const std::string k = detail::pct_decode(
                eq == std::string_view::npos ? pair : pair.substr(0, eq), true);
            const std::string v = eq == std::string_view::npos
                ? std::string{} : detail::pct_decode(pair.substr(eq + 1), true);
            if (detail::iequals(k, "odbc_connect")) {
                return OdbcDsnFactory{}.create(v);  // full DSN, verbatim
            }
            if (detail::iequals(k, "driver")) driver = v;
            else passthrough.emplace_back(k, v);
        }

        ConnectionStringBuilder b;
        // Named-DSN form: a bare host with no port/database/driver is a DSN name.
        if (!host.empty() && driver.empty() && !port && database.empty()) {
            b.named_dsn(host);
        } else {
            if (!driver.empty()) b.driver(driver);
            b.server(host.empty() ? "localhost" : host, port);
            if (!database.empty()) b.database(database);
        }
        if (!user.empty()) b.uid(user);
        if (has_pass) b.pwd(pass);
        for (const auto& [k, v] : passthrough) b.set_raw(k, v);
        return b.build();
    }

private:
    [[nodiscard]] static std::string_view scheme(std::string_view url) {
        const std::size_t sep = url.find("://");
        return sep == std::string_view::npos ? std::string_view{} : url.substr(0, sep);
    }
};

// ── parse() facade: abstract-factory dispatch ───────────────────────────────

inline ConnectionString ConnectionString::parse(std::string_view source) {
    static const SqlAlchemyUrlFactory url_factory;
    static const OdbcDsnFactory dsn_factory;
    if (detail::looks_like_url(source)) {
        if (!url_factory.accepts(source)) {
            throw std::runtime_error(
                "Unsupported URL scheme: expected an mssql+pyodbc:// URL or a "
                "raw ODBC connection string");
        }
        return url_factory.create(source);
    }
    return dsn_factory.create(source);
}

// ── constexpr contracts (compile-time verified, like plan_autowiring) ────────

static_assert(detail::iequals("PWD", "pwd"));
static_assert(detail::canonical_key("Password") == ConnKey::Pwd);
static_assert(detail::canonical_key("Server") == ConnKey::Server);
static_assert(detail::canonical_key("weird_key") == ConnKey::Other);
static_assert(detail::is_secret_key(ConnKey::Pwd));
static_assert(!detail::is_secret_key(ConnKey::Uid));
static_assert(detail::looks_like_url("mssql+pyodbc://h/db"));
static_assert(!detail::looks_like_url("Driver={X};Server=h;"));

}  // namespace pygim::core
