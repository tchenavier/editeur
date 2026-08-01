// Transport.h
// -----------------------------------------------------------------------
// La couche réseau, réduite à une seule idée : « envoyer une requête HTTP,
// obtenir une réponse ». Rien d'autre.
//
// POURQUOI UNE INTERFACE plutôt qu'un appel direct à WinHTTP : c'est la
// même raison qui fait que le métier ne connaît pas le webview. Au-dessus
// de cette ligne, tout se teste sans réseau, sans serveur, sans attendre —
// et surtout, tout se teste avec des réponses HOSTILES qu'aucun vrai
// serveur ne renverrait spontanément. Le jour où le transport change
// (autre bibliothèque, autre plateforme), rien au-dessus ne bouge.
//
// CE QUI N'EST PAS ICI, volontairement : la notion de projet, de fichier,
// d'inventaire, de jeton. Ce module ne sait pas ce qu'il transporte. Le
// sens est donné par net/SyncClient, qui parle le contrat (PROTOCOLE.md).
//
// LA RÈGLE DE SÉCURITÉ de cette couche : une réponse est une ENTRÉE NON
// FIABLE, au même titre qu'un fichier ouvert. Tout ce qui vient du réseau
// est borné avant d'être cru — c'est pourquoi la taille maximale du corps
// fait partie de la requête et non d'un réglage global qu'on oublierait.
// -----------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace net {

// Verbes utilisés par le contrat. Une énumération plutôt qu'une chaîne :
// on ne peut pas se tromper de casse, ni inventer un verbe que le serveur
// ne connaît pas.
enum class Method { Get, Post, Put, Delete };

const char* methodName(Method method);

// Un en-tête. Le nom est comparé SANS tenir compte de la casse (HTTP
// l'autorise, et les serveurs en usent : « ETag », « etag », « Etag »).
struct Header {
    std::string name;
    std::string value;
};

// Recherche un en-tête par nom, casse ignorée. Retourne une chaîne vide
// s'il est absent : un en-tête manquant n'est pas une exception, c'est un
// cas ordinaire dont l'appelant décide quoi faire.
std::string findHeader(const std::vector<Header>& headers, const std::string& name);

struct Request {
    Method method = Method::Get;

    // Chemin ET paramètres, à partir de la racine : « /api/version ».
    // L'hôte n'est pas ici : il appartient au transport, qui seul sait à
    // qui il parle.
    std::string path;

    std::vector<Header> headers;
    std::string body;

    // Borne de LECTURE. Au-delà, le transport abandonne au lieu de
    // remplir la mémoire avec ce qu'un serveur lui envoie. Choisie par
    // l'appelant parce qu'elle dépend de ce qu'il attend : un inventaire
    // et un chapitre illustré n'ont pas le même ordre de grandeur.
    size_t maxResponseBytes = 16u * 1024u * 1024u;

    // Délai avant abandon, en millisecondes. Zéro = celui du transport.
    uint32_t timeoutMs = 30000;
};

struct Response {
    int status = 0; // code HTTP ; 0 si la requête n'a pas abouti
    std::vector<Header> headers;
    std::string body;

    // Le corps a-t-il été coupé parce qu'il dépassait la borne ? On le dit
    // plutôt que de rendre un corps tronqué qui passerait pour complet.
    bool truncated = false;

    bool ok() const { return status >= 200 && status < 300; }
};

// L'interface elle-même.
class Transport {
public:
    virtual ~Transport() = default;

    // Envoie la requête. Retourne false en cas d'échec de TRANSPORT (hôte
    // injoignable, TLS refusé, délai dépassé) — et non pour un code
    // d'erreur HTTP, qui est une réponse valide et remplit 'response'.
    //
    // La distinction compte : « le serveur a dit non » et « le serveur n'a
    // rien dit » appellent des conduites différentes. La première est une
    // information, la seconde une panne.
    virtual bool send(const Request& request, Response& response,
                      std::string* errorMessage) = 0;
};

} // namespace net
