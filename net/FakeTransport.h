// FakeTransport.h
// -----------------------------------------------------------------------
// Un transport qui ne parle à personne : il rend les réponses qu'on lui a
// données d'avance, et note ce qu'on lui a demandé d'envoyer.
//
// À QUOI ÇA SERT, et pourquoi c'est le cœur du dispositif de test : un
// vrai serveur ne renvoie presque jamais ce qui casse un client. Il
// répond bien. Or c'est précisément le mauvais comportement qu'il faut
// éprouver — corps tronqué, empreinte qui ne correspond pas, type de
// contenu faux, code inattendu, coupure au milieu. Ici, tout cela
// s'écrit en une ligne.
//
// FIDÉLITÉ : ce transport applique la même borne de lecture que le vrai
// (maxResponseBytes, avec le drapeau 'truncated'). Un double qui serait
// plus gentil que l'original ne prouverait rien.
//
// EN-TÊTE SEUL, jamais compilé dans l'application : seuls les tests et le
// fuzzer l'incluent.
// -----------------------------------------------------------------------

#pragma once

#include "Transport.h"

#include <string>
#include <vector>

namespace net {

class FakeTransport : public Transport {
public:
    // Un échange préparé : soit une réponse, soit une panne de transport.
    struct Exchange {
        Response response;
        bool fails = false;
        std::string failure = "hôte injoignable (simulé)";
    };

    // Les réponses à servir, dans l'ordre des requêtes.
    std::vector<Exchange> planned;

    // Ce qui a réellement été envoyé — pour vérifier les en-têtes, les
    // chemins, les corps.
    std::vector<Request> sent;

    // Quand la file est vide. Par défaut une panne : un test qui envoie
    // une requête qu'il n'avait pas prévue doit s'en apercevoir, pas
    // recevoir un « 200 OK » complaisant.
    bool failWhenEmpty = true;

    bool send(const Request& request, Response& response,
              std::string* errorMessage) override {
        sent.push_back(request);

        if (planned.empty()) {
            if (failWhenEmpty) {
                if (errorMessage) *errorMessage = "aucune réponse prévue (simulé)";
                response = Response{};
                return false;
            }
            response = Response{};
            response.status = 200;
            return true;
        }

        const Exchange exchange = planned.front();
        planned.erase(planned.begin());

        if (exchange.fails) {
            if (errorMessage) *errorMessage = exchange.failure;
            response = Response{};
            return false;
        }

        response = exchange.response;
        // Même borne que le vrai transport : un corps trop gros est coupé
        // ET signalé, jamais rendu comme s'il était complet.
        if (response.body.size() > request.maxResponseBytes) {
            response.body.resize(request.maxResponseBytes);
            response.truncated = true;
        }
        return true;
    }

    // --- Aides d'écriture des tests ------------------------------------

    void reply(int status, const std::string& body,
               const std::string& contentType = "text/plain; charset=utf-8",
               const std::vector<Header>& extra = {}) {
        Exchange exchange;
        exchange.response.status = status;
        exchange.response.body = body;
        if (!contentType.empty()) {
            exchange.response.headers.push_back({"Content-Type", contentType});
        }
        for (const Header& header : extra) exchange.response.headers.push_back(header);
        planned.push_back(exchange);
    }

    void replyBytes(int status, const std::string& body, const std::string& etag) {
        reply(status, body, "application/octet-stream", {{"ETag", "\"" + etag + "\""}});
    }

    void breakDown(const std::string& reason = "hôte injoignable (simulé)") {
        Exchange exchange;
        exchange.fails = true;
        exchange.failure = reason;
        planned.push_back(exchange);
    }

    const Request& lastRequest() const { return sent.back(); }
};

} // namespace net
