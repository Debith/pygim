#include <sstream>
#include <iostream>
#include "url.h"
#include "id.h"


std::string stripCharacter(const std::string& str, char character) {
    size_t start = str.find_first_not_of(character);
    if (start == std::string::npos) {
        // The string contains only the character to be stripped
        return "";
    }

    size_t end = str.find_last_not_of(character);
    return str.substr(start, (end - start + 1));
}


// Default constructor
Url::Url() :
    mScheme(""),
    mUsername(""),
    mPassword(""),
    mHost(""),
    mPort(0),
    mAuthority(""),
    mQuery(""),
    mFragment("") {
    // Default constructor initializes all string members to empty strings
}


Url::Url(const Url& other) :
    mScheme(other.mScheme),
    mUsername(other.mUsername),
    mPassword(other.mPassword),
    mHost(other.mHost),
    mPort(other.mPort),
    mPath(other.mPath),
    mAuthority(other.mAuthority),
    mQuery(other.mQuery),
    mFragment(other.mFragment),
    _params(other._params) {
}


Url::Url(const std::string& value) :
        mScheme(""),
        mUsername(""),
        mPassword(""),
        mHost(""),
        mPort(0),
        mAuthority(""),
        mQuery(""),
        mFragment("")
    {
    std::istringstream iss(value);
    std::string token;

    // Extract scheme (e.g., "http")
    if (std::getline(iss, token, ':')) {
        mScheme = token;
        iss.ignore(2); // Skip '//' after the scheme
    }

    // Extract host
    if (std::getline(iss, token, '/')) {
        mHost = token;
    }

    // Extract path
    while (std::getline(iss, token, '/')) {
        mPath.push_back(token);
    }
}


Url::Url(const std::map<std::string, std::string>& parts) {
    mScheme = parts.at("scheme");
    mUsername = parts.at("userInfo");
    mPassword = parts.at("userInfo");
    mHost = parts.at("host");
    int port = std::stoi(parts.at("port"));  // Convert string to int

    if (port < 0 || port > 65535) {
        throw std::out_of_range("Value out of range for uint16_t");
    }
    mAuthority = parts.at("authority");
    mQuery = parts.at("query");
    mFragment = parts.at("fragment");
    // Note: This implementation assumes that all the keys are present in the map.
    // You might want to add error checks or defaults.
}


Url::Url(const std::string& scheme,
         const std::string& username,
         const std::string& password,
         const std::string& host,
         const uint16_t port,
         const std::string& path,
         const std::string& query,
         const std::string& fragment
         ) :
    mScheme(scheme),
    mUsername(username),
    mPassword(password),
    mHost(host),
    mPort(port),
    mAuthority(""),
    mQuery(query),
    mFragment(fragment) {
    split(path, '/');
}


void Url::split(const std::string& s, char delimiter) {
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        // push token to vector after removing leading and trailing whitespaces
        // as well as slashes.
        token = stripCharacter(token, '/');
        if (token.empty()) {
            continue;
        }
        mPath.push_back(token);
    }
}


std::vector<std::string> iter() const {
    std::vector<std::string> components;

    components.push_back(mScheme);

    // Combine username and password as a single string
    if (!mUsername.empty()) {
        components.push_back(mUsername + (mPassword.empty() ? "" : ":" + mPassword));
    }

    // Combine host and port as a single string
    components.push_back(mHost + (mPort ? ":" + std::to_string(mPort) : ""));

    // Append all path components
    for (const auto& segment : mPath) {
        components.push_back(segment);
    }

    if (!mQuery.empty()) {
        components.push_back(mQuery);
    }

    // ... include other components as needed ...

    return components;
}


const std::string Url::str() const {
    std::stringstream result;

    // Scheme (e.g., "http")
    if (!mScheme.empty()) {
        result << mScheme << "://";
    }

    // User Info (e.g., "username:password@")
    if (!mUsername.empty()) {
        result << mUsername;
    }

    if (!mPassword.empty()) {
        result << ":" << mPassword;
    }

    if (!mUsername.empty() || !mPassword.empty()) {
        result << "@";
    }

    // Host (e.g., "www.example.com")
    if (!mHost.empty()) {
        result << mHost;
    }

    // Port (e.g., ":8080")
    if (mPort) {
        result << ":" << mPort;
    }

    // Path (e.g., "/path/to/resource")
    for (const auto& part : mPath) {
        result << "/" << part;
    }

    // Query (e.g., "?query=value")
    if (!mQuery.empty()) {
        result << "?" << mQuery;
    }

    // Fragment (e.g., "#section")
    if (!mFragment.empty()) {
        result << "#" << mFragment;
    }

    return result.str();
}


// Overloaded division operator for string
Url Url::operator/(const std::string& other) {
    Url new_url(*this);
    new_url.split(other, '/');
    return new_url;
}

// Overloaded division operator for Url object
Url Url::operator/(const Url& other) {
    Url new_url(*this);

    if (mScheme != other.mScheme) {
        throw std::invalid_argument("Scheme mismatch");
    } else if (mHost != other.mHost) {
        throw std::invalid_argument("Username mismatch");
    } else if (mHost != other.mHost) {
        throw std::invalid_argument("Host mismatch");
    } else if (mPort != other.mPort) {
        throw std::invalid_argument("Port mismatch");
    } else if (mAuthority != other.mAuthority) {
        throw std::invalid_argument("Authority mismatch");
    } else if (mQuery != other.mQuery) {
        throw std::invalid_argument("Query mismatch");
    } else if (mFragment != other.mFragment) {
        throw std::invalid_argument("Fragment mismatch");
    }

    for (const auto& part : other.mPath) {
        new_url.mPath.push_back(part);
    }

    return new_url;
}


// withParams implementation
Url Url::withParams(const std::map<std::string, std::string>& mapping) const {
    Url new_url(*this);

    //new_url._params.insert(mapping.begin(), mapping.end());
    return new_url;
}


// Overloaded or-operator implementation
Url Url::operator|(const std::map<std::string, std::string>& other) {
    return this->withParams(other);
}


bool Url::operator==(const Url& other) const {
    if (this == &other) return true;
    return
        mScheme == other.mScheme &&
        mUsername == other.mUsername &&
        mPassword == other.mPassword &&
        mHost == other.mHost &&
        mPort == other.mPort &&
        mPath == other.mPath &&
        mAuthority == other.mAuthority &&
        mQuery == other.mQuery &&
        mFragment == other.mFragment &&
        _params == other._params;
}


bool Url::operator!=(const Url& other) const {
    return !(*this == other);
}
