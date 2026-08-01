// Transport.cpp
// Les quelques fonctions communes à tous les transports. Voir Transport.h.

#include "Transport.h"

namespace net {

namespace {

char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalIgnoringCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

} // namespace

const char* methodName(Method method) {
    switch (method) {
        case Method::Post:   return "POST";
        case Method::Put:    return "PUT";
        case Method::Delete: return "DELETE";
        case Method::Get:    break;
    }
    return "GET";
}

std::string findHeader(const std::vector<Header>& headers, const std::string& name) {
    for (const Header& header : headers) {
        if (equalIgnoringCase(header.name, name)) return header.value;
    }
    return {};
}

} // namespace net
