#include "url.h"

// Default constructor
Url::Url() {}

// Constructor using Url
Url::Url(const Url& other) : _url(other._url), _params(other._params) {}

// Constructor using string
Url::Url(const std::string& value) {
    if (value.substr(0, 4) != "http") {
        throw std::invalid_argument("Not an URL: " + value);
    }
    _url = split(value, '/');
}

// Constructor using list of strings
Url::Url(const std::vector<std::string>& valueList) {
    _url = valueList;
    if (_url[0].find("http") == std::string::npos) {
        _url.insert(_url.begin(), "http:/");
    }
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
    std::string result;
    for (const auto& part : _url) {
        result += part + "/";
    }
    return result.substr(0, result.size() - 1);
}

// Overloaded division operator for string
Url Url::operator/(const std::string& other) {
    Url new_url(*this);
    auto other_parts = split(other, '/');
    new_url._url.insert(new_url._url.end(), other_parts.begin(), other_parts.end());
    return new_url;
}

// Overloaded division operator for Url object
Url Url::operator/(const Url& other) {
    Url new_url(*this);
    new_url._url.insert(new_url._url.end(), other._url.begin(), other._url.end());
    return new_url;
}


// withParams implementation
Url Url::withParams(const std::map<std::string, std::string>& mapping) const {
    Url new_url(*this);
    new_url._params.insert(mapping.begin(), mapping.end());
    return new_url;
}

// Overloaded or-operator implementation
Url Url::operator|(const std::map<std::string, std::string>& other) {
    return this->withParams(other);
}