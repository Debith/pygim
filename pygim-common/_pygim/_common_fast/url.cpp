#include <sstream>
#include "url.h"

// Default constructor
Url::Url() :
    mScheme(""),
    mUserInfo(""),
    mHost(""),
    mPort(0),
    mAuthority(""),
    mQuery(""),
    mFragment("") {
    // Default constructor initializes all string members to empty strings
}

Url::Url(const Url& other) :
    mScheme(other.mScheme),
    mUserInfo(other.mUserInfo),
    mHost(other.mHost),
    mPort(other.mPort),
    mAuthority(other.mAuthority),
    mQuery(other.mQuery),
    mFragment(other.mFragment),
    _params(other._params) {
    // Copy constructor
}

Url::Url(const std::string& value) :
        mScheme(""),
        mUserInfo(""),
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

Url::Url(const std::vector<std::string>& valueList) {
    // Assuming valueList contains ordered URL parts:
    if(valueList.size() > 0) mScheme = valueList[0];
    if(valueList.size() > 1) mUserInfo = valueList[1];
    // ... and so on, adjust as per the actual structure of valueList
}

Url::Url(const std::map<std::string, std::string>& parts) {
    mScheme = parts.at("scheme");
    mUserInfo = parts.at("userInfo");
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
         const std::string& host,
         const uint16_t port,
         const std::string& path,
         const std::string& query,
         const std::string& fragment
         ) :
    mScheme(scheme),
    mUserInfo(""),
    mHost(host),
    mPort(port),
    mAuthority(""),
    mQuery(query),
    mFragment(fragment) {
    mPath = split(path, '/');
}

std::vector<std::string> Url::split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}


std::string Url::str() {
    std::stringstream result;

    // Scheme (e.g., "http")
    if (!mScheme.empty()) {
        result << mScheme << "://";
    }

    // User Info (e.g., "username:password@")
    if (!mUserInfo.empty()) {
        result << mUserInfo << "@";
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
    auto other_parts = split(other, '/');
    //new_url._url.insert(new_url._url.end(), other_parts.begin(), other_parts.end());
    return new_url;
}

// Overloaded division operator for Url object
Url Url::operator/(const Url& other) {
    Url new_url(*this);
    //new_url._url.insert(new_url._url.end(), other._url.begin(), other._url.end());
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
        mUserInfo == other.mUserInfo &&
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
