// WinHttpTransport.cpp
// Le transport réel, sur WinHTTP. Voir WinHttpTransport.h pour les trois
// décisions de sécurité qui gouvernent ce fichier.

#include "WinHttpTransport.h"

#ifdef _WIN32

// Aucune macro _WIN32_WINNT ici, contrairement à Bridge/NativeDialog : ce
// fichier compile avec la cible par défaut. Ce qui empêchait <windows.h>
// de compiler dans ce projet n'était pas un bug du SDK mais une COLLISION
// DE NOMS — Bridge/FileApi.h masquait le fileapi.h du SDK, les chemins
// /I étant fouillés avant les chemins système. Le module s'appelle
// désormais ManuscriptApi (voir scripts/README.md).
#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

#include <algorithm>
#include <vector>

namespace net {

namespace {

// UTF-8 -> UTF-16. WinHTTP ne parle que l'encodage natif de Windows ; la
// conversion est enfermée ici, comme celle des chemins l'est dans
// utils::PathUtf8.
std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        &wide[0], needed);
    return wide;
}

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                           static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string utf8(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        &utf8[0], needed, nullptr, nullptr);
    return utf8;
}

std::string systemError(const std::string& what) {
    const DWORD code = GetLastError();
    return what + " (code système " + std::to_string(code) + ")";
}

// Les en-têtes bruts arrivent en un seul bloc, une ligne par en-tête.
// On les redécoupe ; une ligne sans « : » est ignorée plutôt que
// d'interrompre la lecture — c'est une entrée non fiable comme une autre.
std::vector<Header> parseRawHeaders(const std::wstring& raw) {
    std::vector<Header> headers;
    size_t start = 0;
    while (start < raw.size()) {
        size_t end = raw.find(L'\n', start);
        if (end == std::wstring::npos) end = raw.size();
        std::wstring line = raw.substr(start, end - start);
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();

        const size_t colon = line.find(L':');
        if (colon != std::wstring::npos && colon > 0) {
            std::wstring value = line.substr(colon + 1);
            size_t first = value.find_first_not_of(L' ');
            if (first == std::wstring::npos) first = value.size();
            headers.push_back({toUtf8(line.substr(0, colon)), toUtf8(value.substr(first))});
        }
        start = end + 1;
    }
    return headers;
}

} // namespace

struct WinHttpTransport::Impl {
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    Endpoint endpoint;
    std::string error;

    ~Impl() {
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
    }
};

WinHttpTransport::Endpoint WinHttpTransport::parseBaseUrl(const std::string& baseUrl) {
    Endpoint endpoint;

    std::string rest;
    if (baseUrl.rfind("https://", 0) == 0) {
        endpoint.secure = true;
        endpoint.port = 443;
        rest = baseUrl.substr(8);
    } else if (baseUrl.rfind("http://", 0) == 0) {
        endpoint.secure = false;
        endpoint.port = 80;
        rest = baseUrl.substr(7);
    } else {
        return endpoint; // sans schéma explicite, on ne devine pas
    }

    // Un chemin éventuel est ignoré : il appartient aux requêtes.
    const size_t slash = rest.find('/');
    if (slash != std::string::npos) rest.erase(slash);

    const size_t colon = rest.find(':');
    if (colon != std::string::npos) {
        const std::string portText = rest.substr(colon + 1);
        rest.erase(colon);
        if (portText.empty() || portText.size() > 5) return endpoint;
        unsigned value = 0;
        for (const char c : portText) {
            if (c < '0' || c > '9') return endpoint;
            value = value * 10 + static_cast<unsigned>(c - '0');
        }
        if (value == 0 || value > 65535) return endpoint;
        endpoint.port = static_cast<uint16_t>(value);
    }

    if (rest.empty() || rest.size() > 255) return endpoint;
    for (const unsigned char c : rest) {
        // Un hôte ne contient ni espace, ni caractère de contrôle, ni
        // barre oblique : refuser ici évite de composer une requête vers
        // un endroit inattendu.
        if (c <= 0x20 || c == 0x7F || c == '/' || c == '\\' || c == '@') return endpoint;
    }

    endpoint.host = rest;
    endpoint.valid = true;
    return endpoint;
}

WinHttpTransport::WinHttpTransport(const std::string& baseUrl)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->endpoint = parseBaseUrl(baseUrl);
    if (!m_impl->endpoint.valid) {
        m_impl->error = "Adresse de serveur inutilisable : « " + baseUrl +
                        " » (attendu « https://hôte » ou « http://hôte:port »).";
        return;
    }

    m_impl->session = WinHttpOpen(L"editeur-roman/1.0",
                                  WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!m_impl->session) {
        m_impl->error = systemError("Impossible d'initialiser la couche réseau");
        return;
    }

    // DÉCISION 1 : les redirections ne sont pas suivies. Voir l'en-tête.
    DWORD disable = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(m_impl->session, WINHTTP_OPTION_DISABLE_FEATURE, &disable,
                     sizeof(disable));

    m_impl->connection = WinHttpConnect(m_impl->session, toWide(m_impl->endpoint.host).c_str(),
                                        m_impl->endpoint.port, 0);
    if (!m_impl->connection) {
        m_impl->error = systemError("Impossible de joindre « " + m_impl->endpoint.host + " »");
    }
}

WinHttpTransport::~WinHttpTransport() = default;

bool WinHttpTransport::valid() const {
    return m_impl->connection != nullptr && m_impl->error.empty();
}

const std::string& WinHttpTransport::error() const { return m_impl->error; }

bool WinHttpTransport::send(const Request& request, Response& response,
                            std::string* errorMessage) {
    response = Response{};

    auto fail = [&errorMessage](const std::string& message) {
        if (errorMessage) *errorMessage = message;
        return false;
    };

    if (!valid()) {
        return fail(m_impl->error.empty() ? std::string("Transport réseau indisponible.")
                                          : m_impl->error);
    }

    const DWORD flags = m_impl->endpoint.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET handle = WinHttpOpenRequest(m_impl->connection,
                                          toWide(methodName(request.method)).c_str(),
                                          toWide(request.path).c_str(), nullptr,
                                          WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!handle) return fail(systemError("Requête impossible à préparer"));

    // Fermeture garantie quel que soit le chemin de sortie : il y en a
    // beaucoup dans cette fonction, et une poignée oubliée fuit.
    struct Closer {
        HINTERNET handle;
        ~Closer() { if (handle) WinHttpCloseHandle(handle); }
    } closer{handle};

    // DÉCISION 1 (suite) : au niveau de la requête aussi, par sécurité —
    // une option de session peut être remise à zéro par un réglage système.
    DWORD disable = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(handle, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));

    const int timeout = static_cast<int>(request.timeoutMs ? request.timeoutMs : 30000);
    WinHttpSetTimeouts(handle, timeout, timeout, timeout, timeout);

    std::wstring headers;
    for (const Header& header : request.headers) {
        // Un en-tête ne peut pas contenir de fin de ligne : ce serait une
        // injection d'en-tête, c'est-à-dire une requête différente de
        // celle qu'on croit envoyer.
        if (header.name.find_first_of("\r\n") != std::string::npos ||
            header.value.find_first_of("\r\n") != std::string::npos) {
            return fail("En-tête de requête refusé : il contient une fin de ligne.");
        }
        headers += toWide(header.name) + L": " + toWide(header.value) + L"\r\n";
    }

    const BOOL sent = WinHttpSendRequest(
        handle, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        headers.empty() ? 0 : static_cast<DWORD>(headers.size()),
        request.body.empty() ? WINHTTP_NO_REQUEST_DATA
                             : const_cast<char*>(request.body.data()),
        static_cast<DWORD>(request.body.size()),
        static_cast<DWORD>(request.body.size()), 0);
    if (!sent) return fail(systemError("Envoi impossible"));

    if (!WinHttpReceiveResponse(handle, nullptr)) {
        return fail(systemError("Aucune réponse du serveur"));
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        return fail(systemError("Réponse sans code d'état"));
    }
    response.status = static_cast<int>(status);

    // En-têtes bruts : une seule interrogation, puis découpage à la main.
    DWORD headerSize = 0;
    WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                        WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &headerSize,
                        WINHTTP_NO_HEADER_INDEX);
    if (headerSize > 0 && headerSize < 1024u * 1024u) {
        std::wstring raw(headerSize / sizeof(wchar_t) + 1, L'\0');
        if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                WINHTTP_HEADER_NAME_BY_INDEX, &raw[0], &headerSize,
                                WINHTTP_NO_HEADER_INDEX)) {
            raw.resize(headerSize / sizeof(wchar_t));
            response.headers = parseRawHeaders(raw);
        }
    }

    // DÉCISION 3 : lecture bornée. On s'arrête net au-delà de la borne et
    // on le dit, plutôt que de rendre un corps coupé qu'on croirait entier.
    std::string body;
    std::vector<char> buffer(64u * 1024u);
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(handle, &available)) {
            return fail(systemError("Lecture interrompue"));
        }
        if (available == 0) break;

        const size_t room = (body.size() >= request.maxResponseBytes)
                                ? 0
                                : request.maxResponseBytes - body.size();
        if (room == 0) {
            response.truncated = true;
            break;
        }

        const DWORD toRead = static_cast<DWORD>(
            std::min<size_t>({static_cast<size_t>(available), buffer.size(), room}));
        DWORD read = 0;
        if (!WinHttpReadData(handle, buffer.data(), toRead, &read)) {
            return fail(systemError("Lecture interrompue"));
        }
        if (read == 0) break;
        body.append(buffer.data(), read);
    }

    response.body = std::move(body);
    return true;
}

} // namespace net

#endif // _WIN32
