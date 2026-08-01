// SyncClient.cpp
// Le contrat de PROTOCOLE.md traduit en appels. Voir SyncClient.h.

#include "SyncClient.h"

#include "Sha256.h"

#include <algorithm>

namespace net {

namespace {

// L'identifiant de projet est un UUID : on n'accepte que ce qui peut en
// être un. Cela évite qu'un identifiant bricolé se retrouve dans une URL.
bool isSafeProjectId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (const char c : id) {
        const bool acceptable = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                (c >= '0' && c <= '9') || c == '-';
        if (!acceptable) return false;
    }
    return true;
}

// L'ETag arrive entouré de guillemets, parfois précédé de « W/ » pour une
// validation faible. On ne garde que l'empreinte.
std::string cleanEtag(std::string etag) {
    if (etag.rfind("W/", 0) == 0) etag.erase(0, 2);
    if (!etag.empty() && etag.front() == '"') etag.erase(0, 1);
    if (!etag.empty() && etag.back() == '"') etag.pop_back();
    return etag;
}

bool isHexSha(const std::string& value) {
    if (value.size() != 64) return false;
    for (const char c : value) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                         (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

std::string lowered(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

std::string sha256Of(const std::string& content) {
    utils::Sha256 hash;
    hash.update(content);
    return hash.finalHex();
}

// Le corps d'une réponse d'erreur est du texte destiné à l'auteur. On le
// reprend s'il est court et sans caractère de contrôle ; sinon on s'en
// tient à notre propre phrase — un serveur fautif ne dictera pas ce qui
// s'affiche à l'écran.
std::string usableMessage(const std::string& body) {
    if (body.empty() || body.size() > 300) return {};
    for (const unsigned char c : body) {
        if (c < 0x20 && c != '\n' && c != '\t') return {};
    }
    return body;
}

} // namespace

SyncClient::SyncClient(Transport& transport, std::string token)
    : m_transport(transport), m_token(std::move(token)) {}

bool SyncClient::isSafePath(const std::string& path) {
    if (path.empty() || path.size() > 1024) return false;
    if (path.front() == '/') return false;      // toujours relatif au projet
    if (path.find('\\') != std::string::npos) return false;
    if (path.find(':') != std::string::npos) return false; // « C: », flux NTFS

    for (const unsigned char c : path) {
        if (c < 0x20 || c == 0x7F) return false;
    }

    // Aucun segment « .. », et aucun segment vide (« a//b ») : les deux
    // mènent hors du dossier de projet ou à une URL ambiguë.
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = std::min(path.find('/', start), path.size());
        const std::string segment = path.substr(start, end - start);
        if (segment.empty() || segment == "..") return false;
        if (end == path.size()) break;
        start = end + 1;
    }
    return true;
}

std::string SyncClient::percentEncode(const std::string& path) {
    static const char* const kDigits = "0123456789ABCDEF";
    std::string out;
    out.reserve(path.size());
    for (const unsigned char c : path) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                                c == '_' || c == '~' || c == '/';
        if (unreserved) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kDigits[c >> 4];
            out += kDigits[c & 0x0F];
        }
    }
    return out;
}

std::string SyncClient::readKey(const std::string& body, const std::string& key) {
    size_t start = 0;
    while (start < body.size()) {
        size_t end = body.find('\n', start);
        if (end == std::string::npos) end = body.size();
        std::string line = body.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const size_t space = line.find(' ');
        if (space != std::string::npos && line.compare(0, space, key) == 0 &&
            space == key.size()) {
            return line.substr(space + 1);
        }
        start = end + 1;
    }
    return {};
}

std::string SyncClient::projectPath(const std::string& projectId,
                                    const std::string& suffix) const {
    return "/api/projects/" + projectId + suffix;
}

Result SyncClient::exchange(Request& request, Response& response, bool authenticated) {
    if (authenticated && !m_token.empty()) {
        request.headers.push_back({"Authorization", "Bearer " + m_token});
    }

    std::string error;
    if (!m_transport.send(request, response, &error)) {
        return {Outcome::Unreachable,
                error.empty() ? std::string("Serveur injoignable.") : error};
    }

    if (response.truncated) {
        return {Outcome::BadResponse,
                "Réponse du serveur trop volumineuse : elle a été interrompue."};
    }
    if (response.ok()) return {Outcome::Ok, {}};

    const std::string detail = usableMessage(response.body);
    auto message = [&detail](const char* fallback) {
        return detail.empty() ? std::string(fallback) : detail;
    };

    switch (response.status) {
        case 401: return {Outcome::Unauthorized, message("Connexion expirée ou refusée.")};
        case 403: return {Outcome::Forbidden,    message("Ce projet est partagé en lecture seule.")};
        case 404: return {Outcome::NotFound,     message("Introuvable sur le serveur.")};
        case 409: return {Outcome::Conflict,     message("Le fichier a changé sur le serveur.")};
        case 413: return {Outcome::TooLarge,     message("Fichier trop volumineux pour le serveur.")};
        case 429: return {Outcome::Busy,         message("Serveur occupé, à réessayer.")};
        default: break;
    }
    if (response.status >= 500) {
        return {Outcome::ServerError, message("Le serveur a rencontré une panne.")};
    }
    return {Outcome::Refused,
            message(("Requête refusée par le serveur (code " +
                     std::to_string(response.status) + ").").c_str())};
}

Result SyncClient::version(ServerInfo& info) {
    info = ServerInfo{};

    Request request;
    request.method = Method::Get;
    request.path = "/api/version";
    request.maxResponseBytes = kMaxVersionBytes;

    Response response;
    const Result result = exchange(request, response, /*authenticated=*/false);
    if (!result.ok()) return result;

    const std::string api = readKey(response.body, "api");
    if (api.empty()) {
        return {Outcome::BadResponse,
                "Ce serveur ne répond pas comme un serveur de synchronisation."};
    }
    // Pas de stoi : une chaîne quelconque lèverait une exception, et un
    // nombre démesuré déborderait. On lit à la main, borné.
    int parsed = 0;
    for (const char c : api) {
        if (c < '0' || c > '9' || parsed > 10000) { parsed = -1; break; }
        parsed = parsed * 10 + (c - '0');
    }
    if (parsed < 0) {
        return {Outcome::BadResponse, "Version de protocole illisible."};
    }

    info.api = parsed;
    info.server = readKey(response.body, "server");
    if (info.api != kApiVersion) {
        return {Outcome::Refused,
                "Ce serveur parle la version " + std::to_string(info.api) +
                    " du protocole, l'application la version " +
                    std::to_string(kApiVersion) + "."};
    }
    return {Outcome::Ok, {}};
}

Result SyncClient::remoteIndex(const std::string& projectId, io::ProjectIndex& index) {
    index = io::ProjectIndex{};
    if (!isSafeProjectId(projectId)) {
        return {Outcome::Refused, "Identifiant de projet invalide."};
    }

    Request request;
    request.method = Method::Get;
    request.path = projectPath(projectId, "/index");
    request.maxResponseBytes = kMaxIndexBytes;

    Response response;
    const Result result = exchange(request, response);
    if (!result.ok()) return result;

    // parseIndex est tolérant par construction (il ignore les lignes
    // abîmées) : c'est exactement ce qu'on veut d'une entrée non fiable.
    index = io::parseIndex(response.body);
    return {Outcome::Ok, {}};
}

Result SyncClient::readFile(const std::string& projectId, const std::string& path,
                            std::string& content, std::string& sha256) {
    content.clear();
    sha256.clear();
    if (!isSafeProjectId(projectId)) return {Outcome::Refused, "Identifiant de projet invalide."};
    if (!isSafePath(path)) return {Outcome::Refused, "Chemin de fichier refusé : " + path};

    Request request;
    request.method = Method::Get;
    request.path = projectPath(projectId, "/files/" + percentEncode(path));
    request.maxResponseBytes = kMaxFileBytes;

    Response response;
    const Result result = exchange(request, response);
    if (!result.ok()) return result;

    // LA vérification qui compte : le contenu correspond-il à l'empreinte
    // annoncée ? Sans elle, un transfert coupé donnerait un chapitre
    // amputé qui écraserait le bon.
    const std::string announced = lowered(cleanEtag(findHeader(response.headers, "ETag")));
    const std::string computed = sha256Of(response.body);
    if (announced.empty() || !isHexSha(announced)) {
        return {Outcome::BadResponse,
                "Le serveur n'a pas fourni d'empreinte pour « " + path + " »."};
    }
    if (announced != computed) {
        return {Outcome::BadResponse,
                "Le contenu reçu pour « " + path + " » ne correspond pas à son empreinte."};
    }

    content = response.body;
    sha256 = computed;
    return {Outcome::Ok, {}};
}

Result SyncClient::writeFile(const std::string& projectId, const std::string& path,
                             const std::string& content, const std::string& expectedSha,
                             std::string& newSha) {
    newSha.clear();
    if (!isSafeProjectId(projectId)) return {Outcome::Refused, "Identifiant de projet invalide."};
    if (!isSafePath(path)) return {Outcome::Refused, "Chemin de fichier refusé : " + path};
    if (!expectedSha.empty() && !isHexSha(expectedSha)) {
        return {Outcome::Refused, "Empreinte attendue invalide."};
    }

    Request request;
    request.method = Method::Put;
    request.path = projectPath(projectId, "/files/" + percentEncode(path));
    request.body = content;
    request.headers.push_back({"Content-Type", "application/octet-stream"});
    // Jamais d'écriture sans dire ce qu'on croit écraser : c'est ce qui
    // transforme un écrasement silencieux en conflit visible.
    request.headers.push_back(
        {"If-Match", expectedSha.empty() ? std::string("*") : "\"" + expectedSha + "\""});

    Response response;
    const Result result = exchange(request, response);
    if (!result.ok()) return result;

    const std::string announced = lowered(cleanEtag(findHeader(response.headers, "ETag")));
    const std::string computed = sha256Of(content);
    if (!announced.empty() && announced != computed) {
        return {Outcome::BadResponse,
                "Le serveur a enregistré « " + path + " » sous une empreinte différente."};
    }
    newSha = computed;
    return {Outcome::Ok, {}};
}

Result SyncClient::removeFile(const std::string& projectId, const std::string& path,
                              const std::string& expectedSha) {
    if (!isSafeProjectId(projectId)) return {Outcome::Refused, "Identifiant de projet invalide."};
    if (!isSafePath(path)) return {Outcome::Refused, "Chemin de fichier refusé : " + path};
    if (!expectedSha.empty() && !isHexSha(expectedSha)) {
        return {Outcome::Refused, "Empreinte attendue invalide."};
    }

    Request request;
    request.method = Method::Delete;
    request.path = projectPath(projectId, "/files/" + percentEncode(path));
    request.headers.push_back(
        {"If-Match", expectedSha.empty() ? std::string("*") : "\"" + expectedSha + "\""});

    Response response;
    return exchange(request, response);
}

} // namespace net
