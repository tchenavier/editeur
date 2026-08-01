// SyncClient.h
// -----------------------------------------------------------------------
// Le contrat de PROTOCOLE.md, traduit en appels C++. C'est la seule pièce
// qui sait ce que veulent dire les chemins et les en-têtes ; le transport
// en dessous ne sait que transporter, le moteur au-dessus ne sait que
// décider.
//
// CE MODULE NE DÉCIDE RIEN. Il n'ouvre pas de fichier, n'écrit pas sur le
// disque, ne choisit pas quoi envoyer. Il demande, et il rapporte. Ce qui
// le rend testable de bout en bout avec un transport simulé.
//
// SA VRAIE MISSION est défensive : une réponse de serveur est une entrée
// non fiable, exactement comme un fichier .md ouvert. Ce module est la
// frontière où l'on cesse de croire ce qui vient du réseau. Trois règles
// y sont appliquées sans exception :
//
//   1. Le contenu reçu est VÉRIFIÉ contre son empreinte. Un corps qui ne
//      correspond pas à son ETag est rejeté, quelle qu'en soit la cause.
//   2. Un corps tronqué par la borne de lecture est un échec, jamais un
//      succès partiel qu'on prendrait pour le fichier entier.
//   3. Le chemin envoyé est validé AVANT de construire l'URL — on ne
//      compte pas sur le serveur pour refuser ce qu'on n'aurait pas dû
//      demander.
// -----------------------------------------------------------------------

#pragma once

#include "ProjectIndex.h"
#include "Transport.h"

#include <string>

namespace net {

// Version du contrat que ce client sait parler (voir PROTOCOLE.md).
constexpr int kApiVersion = 1;

// Ce que le serveur dit de lui-même.
struct ServerInfo {
    int api = 0;
    std::string server;
};

// L'issue d'une opération. Une énumération plutôt qu'un booléen : « le
// serveur a dit non » et « le serveur n'a rien dit » n'appellent pas la
// même conduite, et le conflit n'est pas une erreur du tout.
enum class Outcome {
    Ok,
    Unreachable,   // panne de transport : rien n'a été dit
    Unauthorized,  // 401 : jeton absent, invalide ou expiré
    Forbidden,     // 403 : droits insuffisants (projet en lecture seule)
    NotFound,      // 404 : projet ou fichier inconnu
    Conflict,      // 409 : l'empreinte attendue ne correspond plus
    TooLarge,      // 413 : le serveur refuse la taille
    Busy,          // 429 : trop de requêtes, réessayer plus tard
    ServerError,   // 5xx : panne côté serveur
    BadResponse,   // réponse reçue mais inutilisable (empreinte fausse, tronquée…)
    Refused,       // requête refusée pour une autre raison, ou serveur incompatible
};

struct Result {
    Outcome outcome = Outcome::Ok;
    std::string message; // phrase destinée à être montrée telle quelle à l'auteur

    bool ok() const { return outcome == Outcome::Ok; }

    // Vaut-il la peine de réessayer plus tard sans rien changer ? Vrai
    // pour une panne réseau ou serveur, faux pour un refus (qui se
    // reproduirait à l'identique).
    bool worthRetrying() const {
        return outcome == Outcome::Unreachable || outcome == Outcome::Busy ||
               outcome == Outcome::ServerError;
    }
};

// Bornes de lecture. Elles font partie du contrat côté client : au-delà,
// on refuse au lieu de remplir la mémoire avec ce qu'un serveur envoie.
constexpr size_t kMaxIndexBytes = 8u * 1024u * 1024u;   // un inventaire, même énorme
constexpr size_t kMaxFileBytes = 64u * 1024u * 1024u;   // une illustration généreuse
constexpr size_t kMaxVersionBytes = 4096u;              // la poignée de main

class SyncClient {
public:
    // 'transport' doit rester valide aussi longtemps que le client. Le
    // jeton est vide tant qu'aucun compte n'existe (jalon C) : le contrat
    // prévoit que le serveur de développement l'ignore.
    explicit SyncClient(Transport& transport, std::string token = {});

    void setToken(std::string token) { m_token = std::move(token); }

    // Poignée de main. Échoue si la version annoncée n'est pas celle que
    // ce client sait parler : mieux vaut s'arrêter là que d'envoyer des
    // requêtes qu'on interprétera de travers.
    Result version(ServerInfo& info);

    // L'inventaire du projet tel que le serveur le connaît.
    Result remoteIndex(const std::string& projectId, io::ProjectIndex& index);

    // Le contenu d'un fichier, vérifié contre son empreinte.
    Result readFile(const std::string& projectId, const std::string& path,
                    std::string& content, std::string& sha256);

    // Dépose un fichier. 'expectedSha' est l'empreinte que l'on croit être
    // celle du serveur ; vide signifie « je crois ce fichier absent »
    // (traduit par « If-Match: * »). Une empreinte qui ne correspond plus
    // donne Outcome::Conflict — une information, pas une panne.
    Result writeFile(const std::string& projectId, const std::string& path,
                     const std::string& content, const std::string& expectedSha,
                     std::string& newSha);

    Result removeFile(const std::string& projectId, const std::string& path,
                      const std::string& expectedSha);

    // --- Exposés pour les tests, et parce qu'ils valent d'être vérifiés --

    // Un chemin acceptable : relatif, séparateur '/', sans « .. », sans
    // antislash, sans caractère de contrôle. Refusé AVANT de construire
    // une URL, jamais après.
    static bool isSafePath(const std::string& path);

    // Encodage pour URL. Les '/' restent des séparateurs ; tout le reste
    // hors caractères non réservés est encodé.
    static std::string percentEncode(const std::string& path);

    // Lit un corps « clé valeur », une paire par ligne (poignée de main).
    static std::string readKey(const std::string& body, const std::string& key);

private:
    Transport& m_transport;
    std::string m_token;

    // Envoie et traduit le code d'état en Outcome. Le corps de la réponse
    // sert de message quand il est court et lisible.
    //
    // 'authenticated' à false pour la poignée de main : c'est le PREMIER
    // contact avec un serveur dont on ne sait encore rien, et le contrat
    // n'y demande aucun jeton. Ne pas l'envoyer là où il ne sert à rien
    // est gratuit ; le laisser partir vers un serveur qu'on n'a pas encore
    // reconnu ne l'est pas.
    Result exchange(Request& request, Response& response, bool authenticated = true);

    std::string projectPath(const std::string& projectId, const std::string& suffix) const;
};

} // namespace net
