// WinHttpTransport.h
// -----------------------------------------------------------------------
// Le transport RÉEL, sur WinHTTP — la seule pièce du projet qui ouvre une
// connexion. Elle occupe la même place que Bridge/NativeDialog pour les
// fenêtres du système : tout ce qui est spécifique à la plateforme est
// enfermé ici, et rien au-dessus ne le sait.
//
// TROIS DÉCISIONS DE SÉCURITÉ, prises ici parce que c'est ici qu'elles
// s'appliquent :
//
//   1. LES REDIRECTIONS SONT REFUSÉES. WinHTTP les suit tout seul par
//      défaut, et c'est exactement ce qu'il ne faut pas : une redirection
//      emmènerait l'en-tête « Authorization » — donc le jeton de l'auteur —
//      vers un hôte que personne n'a choisi. Une redirection est donc
//      rendue telle quelle (code 3xx) et le client la traite comme un
//      refus.
//
//   2. LA VÉRIFICATION DU CERTIFICAT RESTE ACTIVE. Aucun réglage de ce
//      fichier ne l'assouplit, même « pour le développement » : c'est
//      précisément le genre de contournement qui survit jusqu'en
//      production. Un serveur de test se joint en HTTP simple sur la
//      machine locale, pas en TLS mal vérifié.
//
//   3. LA LECTURE EST BORNÉE par 'maxResponseBytes'. Un serveur — fautif
//      ou hostile — ne peut pas faire enfler la mémoire du client : au-delà
//      de la borne, on s'arrête et on le signale (drapeau 'truncated'),
//      sans jamais rendre un corps coupé qui passerait pour complet.
//
// Ce fichier ne compile que sous Windows. Le reste de la couche réseau
// n'en dépend pas : les tests utilisent net/FakeTransport.
// -----------------------------------------------------------------------

#pragma once

#include "Transport.h"

#include <memory>
#include <string>

namespace net {

class WinHttpTransport : public Transport {
public:
    // 'baseUrl' donne le schéma, l'hôte et le port : « https://exemple.fr »
    // ou « http://127.0.0.1:8080 ». Le chemin des requêtes est fourni
    // requête par requête (voir Request::path).
    explicit WinHttpTransport(const std::string& baseUrl);
    ~WinHttpTransport() override;

    WinHttpTransport(const WinHttpTransport&) = delete;
    WinHttpTransport& operator=(const WinHttpTransport&) = delete;

    // Faux si l'URL est inutilisable ou si WinHTTP n'a pas démarré. On le
    // demande AVANT d'envoyer : un transport invalide ne doit pas se
    // découvrir au milieu d'une synchronisation.
    bool valid() const;

    // La raison de l'invalidité, à montrer à l'auteur.
    const std::string& error() const;

    bool send(const Request& request, Response& response,
              std::string* errorMessage) override;

    // Découpe « https://hôte:port » — exposé pour les tests, parce qu'une
    // URL mal lue enverrait les requêtes ailleurs qu'où l'on croit.
    struct Endpoint {
        std::string host;
        uint16_t port = 0;
        bool secure = false;
        bool valid = false;
    };
    static Endpoint parseBaseUrl(const std::string& baseUrl);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace net
