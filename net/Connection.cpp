// Connection.cpp
// La connexion, et surtout son absence. Voir Connection.h.

#include "Connection.h"

#ifdef _WIN32
#include "WinHttpTransport.h"
#endif

namespace net {

namespace {

const char* const kDigits = "0123456789ABCDEF";

} // namespace

std::string formEncode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const unsigned char c : value) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                                c == '_' || c == '~';
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

void wipe(std::string& secret) {
    // volatile pour que l'écriture ne soit pas supprimée par le
    // compilateur : il verrait sinon un tampon écrit puis jamais relu.
    volatile char* data = const_cast<volatile char*>(secret.data());
    for (size_t i = 0; i < secret.size(); ++i) data[i] = '\0';
    secret.clear();
}

struct Connection::Impl {
    // Nul tant que personne ne s'est connecté. C'est TOUT le sujet de ce
    // module : ce pointeur reste nul pendant la vie entière d'une
    // application utilisée hors ligne.
    std::unique_ptr<Transport> transport;
    std::unique_ptr<SyncClient> client;

    TransportFactory factory;
    std::string serverUrl;
    std::string userName;
    std::string token;

    ~Impl() { if (!token.empty()) wipe(token); }
};

Connection::Connection() : m_impl(std::make_unique<Impl>()) {
    // La fabrique par défaut n'est PAS appelée ici : on se contente de la
    // ranger. Aucune session réseau n'existe à ce stade.
    m_impl->factory = [](const std::string& baseUrl) -> std::unique_ptr<Transport> {
#ifdef _WIN32
        return std::make_unique<WinHttpTransport>(baseUrl);
#else
        (void)baseUrl;
        return nullptr;
#endif
    };
}

Connection::~Connection() = default;

void Connection::setTransportFactory(TransportFactory factory) {
    m_impl->factory = std::move(factory);
}

ConnectionState Connection::state() const {
    return m_impl->client ? ConnectionState::Connected : ConnectionState::Disconnected;
}

bool Connection::isConnected() const { return state() == ConnectionState::Connected; }
bool Connection::hasTransport() const { return m_impl->transport != nullptr; }
const std::string& Connection::serverUrl() const { return m_impl->serverUrl; }
const std::string& Connection::userName() const { return m_impl->userName; }

SyncClient* Connection::client() { return m_impl->client.get(); }

Result Connection::signIn(const std::string& baseUrl, const std::string& user,
                          std::string& secret) {
    // On repart toujours d'un état propre : une connexion ratée ne doit
    // pas laisser derrière elle un transport à demi ouvert.
    signOut();

    if (baseUrl.empty()) {
        wipe(secret);
        return {Outcome::Refused, "Aucune adresse de serveur."};
    }
    if (user.empty()) {
        wipe(secret);
        return {Outcome::Refused, "Aucun identifiant."};
    }
    if (!m_impl->factory) {
        wipe(secret);
        return {Outcome::Unreachable, "Aucun transport réseau disponible."};
    }

    // LE MOMENT où le réseau entre dans l'application. Pas avant.
    std::unique_ptr<Transport> transport = m_impl->factory(baseUrl);
    if (!transport) {
        wipe(secret);
        return {Outcome::Unreachable, "Le transport réseau n'a pas pu être créé."};
    }

    // 1. La poignée de main d'abord : inutile d'envoyer des identifiants
    //    à un serveur dont on ne sait pas s'il nous comprendra.
    SyncClient probe(*transport);
    ServerInfo info;
    Result result = probe.version(info);
    if (!result.ok()) {
        wipe(secret);
        return result;
    }

    // 2. Puis la demande de session. Le secret est dans le CORPS, jamais
    //    dans l'URL (voir PROTOCOLE.md).
    Request request;
    request.method = Method::Post;
    request.path = "/api/session";
    request.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
    request.body = "user=" + formEncode(user) + "&secret=" + formEncode(secret);
    request.maxResponseBytes = kMaxVersionBytes;
    // Le corps de la requête contient le secret : on l'efface dès qu'il a
    // été transmis, et l'exemplaire de l'appelant tout de suite.
    wipe(secret);

    Response response;
    std::string error;
    const bool reached = transport->send(request, response, &error);
    wipe(request.body);

    if (!reached) {
        return {Outcome::Unreachable,
                error.empty() ? std::string("Serveur injoignable.") : error};
    }
    if (response.status == 401 || response.status == 403) {
        return {Outcome::Unauthorized, "Identifiant ou mot de passe refusé."};
    }
    if (!response.ok()) {
        return {Outcome::Refused,
                "Le serveur a refusé la connexion (code " +
                    std::to_string(response.status) + ")."};
    }

    std::string token = SyncClient::readKey(response.body, "token");
    if (token.empty() || token.size() > 512) {
        return {Outcome::BadResponse, "Le serveur n'a pas délivré de session utilisable."};
    }

    m_impl->serverUrl = baseUrl;
    m_impl->userName = SyncClient::readKey(response.body, "user");
    if (m_impl->userName.empty()) m_impl->userName = user;
    m_impl->token = token;
    wipe(token);

    m_impl->transport = std::move(transport);
    m_impl->client = std::make_unique<SyncClient>(*m_impl->transport, m_impl->token);
    return {Outcome::Ok, {}};
}

void Connection::signOut() {
    if (m_impl->transport && !m_impl->token.empty()) {
        // Prévenir le serveur est une politesse, pas une condition : on
        // se déconnecte de toute façon. Un serveur injoignable ne doit
        // pas pouvoir retenir l'auteur connecté.
        Request request;
        request.method = Method::Delete;
        request.path = "/api/session";
        request.headers.push_back({"Authorization", "Bearer " + m_impl->token});
        request.maxResponseBytes = kMaxVersionBytes;
        request.timeoutMs = 5000; // on n'attend pas longtemps pour partir
        Response response;
        std::string error;
        (void)m_impl->transport->send(request, response, &error);
    }

    if (!m_impl->token.empty()) wipe(m_impl->token);
    m_impl->userName.clear();
    m_impl->serverUrl.clear();
    // L'ordre compte : le client tient une référence sur le transport.
    m_impl->client.reset();
    m_impl->transport.reset(); // ferme les poignées système
}

} // namespace net
