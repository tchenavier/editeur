// test_http.cpp
// Tests du transport RÉEL (net/WinHttpTransport) contre un petit serveur
// HTTP lancé par le test lui-même, sur la machine locale.
//
// POURQUOI CE SERVEUR JOUET. Le reste de la couche réseau se teste avec
// net/FakeTransport, qui ne branche rien. Mais le transport réel, lui, ne
// prouve rien tant qu'aucun octet n'a circulé : la composition de la
// requête, la lecture des en-têtes, la boucle de lecture bornée et le
// refus des redirections ne s'exercent qu'en parlant vraiment à quelqu'un.
// Livrer du code réseau jamais exécuté serait le pire des deux mondes.
//
// Le serveur écoute sur 127.0.0.1, sur un port que le système attribue :
// aucun port fixe à réserver, rien qui sorte de la machine, et deux
// exécutions simultanées ne se gênent pas.
//
// EN CLAIR, PAS EN TLS, et c'est délibéré : un serveur de test en TLS
// supposerait un certificat, donc la tentation d'assouplir la vérification
// dans le client. C'est exactement le réglage qui survit jusqu'en
// production. La vérification du certificat n'est donc jamais touchée ;
// c'est WinHTTP qui la porte, et elle sera éprouvée contre le vrai serveur
// au jalon D.

#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "SyncClient.h"
#include "WinHttpTransport.h"

#pragma comment(lib, "ws2_32.lib")

#include <cassert>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

// --- Serveur jouet -----------------------------------------------------
// Il sert une réponse préparée par connexion, note la requête reçue, et
// s'arrête quand il les a toutes servies. Aucune ambition au-delà : ce
// n'est pas le serveur de la Phase 5, c'est un partenaire de test.
class ToyServer {
public:
    explicit ToyServer(std::vector<std::string> responses)
        : m_responses(std::move(responses)) {}

    ~ToyServer() { stop(); }

    bool start() {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        m_started = true;

        m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listener == INVALID_SOCKET) return false;

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = 0; // port choisi par le système
        InetPtonA(AF_INET, "127.0.0.1", &address.sin_addr);
        if (bind(m_listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            return false;
        }

        int length = sizeof(address);
        if (getsockname(m_listener, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            return false;
        }
        m_port = ntohs(address.sin_port);

        if (listen(m_listener, 4) != 0) return false;

        m_thread = std::thread([this] { serve(); });
        return true;
    }

    void stop() {
        if (m_listener != INVALID_SOCKET) {
            closesocket(m_listener);
            m_listener = INVALID_SOCKET;
        }
        if (m_thread.joinable()) m_thread.join();
        if (m_started) { WSACleanup(); m_started = false; }
    }

    void wait() { if (m_thread.joinable()) m_thread.join(); }

    uint16_t port() const { return m_port; }
    const std::vector<std::string>& received() const { return m_received; }

private:
    void serve() {
        for (size_t i = 0; i < m_responses.size(); ++i) {
            const SOCKET client = accept(m_listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) return;

            // Lire jusqu'à la fin des en-têtes, puis le corps annoncé.
            std::string request;
            char buffer[4096];
            size_t headerEnd = std::string::npos;
            while (headerEnd == std::string::npos) {
                const int read = recv(client, buffer, sizeof(buffer), 0);
                if (read <= 0) break;
                request.append(buffer, static_cast<size_t>(read));
                headerEnd = request.find("\r\n\r\n");
                if (request.size() > 1024u * 1024u) break;
            }
            if (headerEnd != std::string::npos) {
                size_t expected = 0;
                const size_t marker = request.find("Content-Length:");
                if (marker != std::string::npos && marker < headerEnd) {
                    expected = static_cast<size_t>(std::stoul(request.substr(marker + 15)));
                }
                while (request.size() < headerEnd + 4 + expected) {
                    const int read = recv(client, buffer, sizeof(buffer), 0);
                    if (read <= 0) break;
                    request.append(buffer, static_cast<size_t>(read));
                }
            }
            m_received.push_back(request);

            const std::string& reply = m_responses[i];
            size_t offset = 0;
            while (offset < reply.size()) {
                const int written = send(client, reply.data() + offset,
                                         static_cast<int>(reply.size() - offset), 0);
                if (written <= 0) break;
                offset += static_cast<size_t>(written);
            }
            shutdown(client, SD_BOTH);
            closesocket(client);
        }
    }

    std::vector<std::string> m_responses;
    std::vector<std::string> m_received;
    SOCKET m_listener = INVALID_SOCKET;
    uint16_t m_port = 0;
    std::thread m_thread;
    bool m_started = false;
};

std::string httpReply(int status, const std::string& reason, const std::string& body,
                      const std::string& extraHeaders = {}) {
    return "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n" +
           "Content-Length: " + std::to_string(body.size()) + "\r\n" +
           "Content-Type: text/plain; charset=utf-8\r\n" + extraHeaders + "\r\n" + body;
}

std::string baseUrlFor(uint16_t port) {
    return "http://127.0.0.1:" + std::to_string(port);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main() {
    std::cout << std::unitbuf;

    // --- Test 1 : lecture d'une adresse de serveur ---
    {
        auto endpoint = net::WinHttpTransport::parseBaseUrl("https://exemple.fr");
        assert(endpoint.valid && endpoint.secure && endpoint.port == 443);
        assert(endpoint.host == "exemple.fr");

        endpoint = net::WinHttpTransport::parseBaseUrl("http://127.0.0.1:8080/ignore");
        assert(endpoint.valid && !endpoint.secure && endpoint.port == 8080);
        assert(endpoint.host == "127.0.0.1");

        // Ce qu'on refuse plutôt que de deviner.
        assert(!net::WinHttpTransport::parseBaseUrl("exemple.fr").valid);      // sans schéma
        assert(!net::WinHttpTransport::parseBaseUrl("ftp://exemple.fr").valid);
        assert(!net::WinHttpTransport::parseBaseUrl("https://").valid);
        assert(!net::WinHttpTransport::parseBaseUrl("https://exemple.fr:0").valid);
        assert(!net::WinHttpTransport::parseBaseUrl("https://exemple.fr:99999").valid);
        assert(!net::WinHttpTransport::parseBaseUrl("https://a b.fr").valid);
        assert(!net::WinHttpTransport::parseBaseUrl("https://user@ailleurs.fr").valid);
        std::cout << "Test 1 OK (lecture de l'adresse du serveur)\n";
    }

    // --- Test 2 : un aller-retour réel ---
    {
        const std::string corps = "# Chapitre premier\n\nIl faisait nuit.\n";
        ToyServer server({httpReply(200, "OK", corps, "ETag: \"abc123\"\r\n")});
        assert(server.start());

        net::WinHttpTransport transport(baseUrlFor(server.port()));
        assert(transport.valid());

        net::Request request;
        request.path = "/api/projects/x/files/roman.md";
        request.headers.push_back({"X-Essai", "valeur"});

        net::Response response;
        std::string error;
        assert(transport.send(request, response, &error));
        assert(response.status == 200);
        assert(response.body == corps);
        assert(!response.truncated);
        // Les en-têtes ont bien été relus et redécoupés.
        assert(net::findHeader(response.headers, "etag") == "\"abc123\"");

        server.wait();
        assert(server.received().size() == 1);
        assert(contains(server.received()[0], "GET /api/projects/x/files/roman.md"));
        assert(contains(server.received()[0], "X-Essai: valeur"));
        std::cout << "Test 2 OK (aller-retour reel, en-tetes relus)\n";
    }

    // --- Test 3 : envoi d'un corps ---
    {
        const std::string corps = "texte envoyé";
        ToyServer server({httpReply(200, "OK", "", "ETag: \"def\"\r\n")});
        assert(server.start());

        net::WinHttpTransport transport(baseUrlFor(server.port()));
        net::Request request;
        request.method = net::Method::Put;
        request.path = "/api/projects/x/files/roman.md";
        request.body = corps;
        request.headers.push_back({"If-Match", "\"ancienne\""});

        net::Response response;
        std::string error;
        assert(transport.send(request, response, &error));
        assert(response.status == 200);

        server.wait();
        assert(contains(server.received()[0], "PUT /api/projects/x/files/roman.md"));
        assert(contains(server.received()[0], "If-Match: \"ancienne\""));
        assert(contains(server.received()[0], corps)); // le corps est bien arrivé
        std::cout << "Test 3 OK (envoi d'un corps et des en-tetes)\n";
    }

    // --- Test 4 : la borne de lecture tient face au vrai réseau ---
    {
        const std::string enorme(200000, 'x');
        ToyServer server({httpReply(200, "OK", enorme)});
        assert(server.start());

        net::WinHttpTransport transport(baseUrlFor(server.port()));
        net::Request request;
        request.path = "/gros";
        request.maxResponseBytes = 5000;

        net::Response response;
        std::string error;
        assert(transport.send(request, response, &error));
        // Coupé ET signalé : jamais rendu comme s'il était complet.
        assert(response.truncated);
        assert(response.body.size() <= 5000);
        server.wait();
        std::cout << "Test 4 OK (lecture bornee, coupure signalee)\n";
    }

    // --- Test 5 : LES REDIRECTIONS NE SONT PAS SUIVIES ---
    {
        // Le point de sécurité de ce fichier : suivre une redirection
        // emmènerait l'en-tête « Authorization » vers un hôte que personne
        // n'a choisi. On doit donc voir le 302 lui-même, et une seule
        // requête doit avoir été émise.
        ToyServer server({httpReply(302, "Found", "",
                                    "Location: http://exemple-malveillant.fr/api\r\n")});
        assert(server.start());

        net::WinHttpTransport transport(baseUrlFor(server.port()));
        net::Request request;
        request.path = "/api/version";
        request.headers.push_back({"Authorization", "Bearer secret"});

        net::Response response;
        std::string error;
        assert(transport.send(request, response, &error));
        assert(response.status == 302);
        server.wait();
        assert(server.received().size() == 1);

        // Et vu du client de protocole, une redirection est un refus.
        assert(!response.ok());
        std::cout << "Test 5 OK (redirection NON suivie, jeton non emporte)\n";
    }

    // --- Test 6 : serveur injoignable ---
    {
        // Rien n'écoute sur le port 1 : la panne doit être franche et
        // distinguée d'une réponse d'erreur.
        net::WinHttpTransport transport("http://127.0.0.1:1");
        net::Request request;
        request.path = "/api/version";
        request.timeoutMs = 3000;

        net::Response response;
        std::string error;
        assert(!transport.send(request, response, &error));
        assert(!error.empty());
        assert(response.status == 0);

        // Adresse inutilisable : refusée dès la construction, pas au
        // milieu d'une synchronisation.
        net::WinHttpTransport bancal("pas une adresse");
        assert(!bancal.valid());
        assert(!bancal.error().empty());
        net::Response ignore;
        assert(!bancal.send(request, ignore, &error));
        std::cout << "Test 6 OK (injoignable et adresse invalide)\n";
    }

    // --- Test 7 : injection d'en-tête refusée ---
    {
        ToyServer server({httpReply(200, "OK", "api 1\n")});
        assert(server.start());

        net::WinHttpTransport transport(baseUrlFor(server.port()));
        net::Request request;
        request.path = "/api/version";
        request.headers.push_back({"X-Truc", "valeur\r\nX-Injecte: oui"});

        net::Response response;
        std::string error;
        assert(!transport.send(request, response, &error));
        assert(contains(error, "fin de ligne"));

        // Rien n'est parti : le serveur attend toujours. On le libère.
        net::Request propre;
        propre.path = "/api/version";
        assert(transport.send(propre, response, &error));
        server.wait();
        assert(!contains(server.received()[0], "X-Injecte"));
        std::cout << "Test 7 OK (injection d'en-tete refusee)\n";
    }

    // --- Test 8 : le client de protocole sur le vrai transport ---
    {
        // Toute la pile, de SyncClient jusqu'aux octets : c'est le seul
        // test qui prouve que les deux moitiés s'emboîtent réellement.
        ToyServer server({httpReply(200, "OK", "api 1\nserver editeur-sync 0.1\n")});
        assert(server.start());

        net::WinHttpTransport transport(baseUrlFor(server.port()));
        net::SyncClient client(transport, "jeton-secret");

        net::ServerInfo info;
        const net::Result result = client.version(info);
        assert(result.ok());
        assert(info.api == net::kApiVersion);
        assert(info.server == "editeur-sync 0.1");

        server.wait();
        // Le jeton n'a pas été envoyé à la poignée de main, jusque dans
        // les octets réellement transmis.
        assert(!contains(server.received()[0], "jeton-secret"));
        std::cout << "Test 8 OK (pile complete, jeton absent des octets emis)\n";
    }

    std::cout << "Tous les tests du transport reel sont passes.\n";
    return 0;
}
