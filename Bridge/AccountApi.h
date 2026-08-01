// AccountApi.h
// -----------------------------------------------------------------------
// Enregistre les commandes "account.*" : se connecter, se déconnecter,
// et décider projet par projet si l'on synchronise (Phase 5, jalon C).
//
// Commandes exposées :
//   account.state    -> état de la connexion, et l'appairage du projet
//                       courant s'il est enregistré. NE TOUCHE PAS AU
//                       RÉSEAU : c'est la commande que l'interface appelle
//                       au démarrage, elle doit être gratuite.
//   account.signIn   -> { server, user, secret }
//   account.signOut  -> ferme la session et LIBÈRE le transport
//   account.pair     -> { path, mode? }  appaire le projet courant
//   account.unpair   -> { path }
//   account.never    -> { path }  « ne plus me proposer pour ce projet »
//   account.setAutoSync -> { path, enabled }
//
// LA RÈGLE QUE CE MODULE FAIT RESPECTER : aucune de ces commandes, sauf
// signIn, n'ouvre quoi que ce soit sur le réseau. L'application démarre,
// s'utilise et se ferme sans qu'une seule poignée réseau existe, tant que
// l'auteur ne demande pas explicitement à se connecter. Voir
// net/Connection.h — la paresse est dans la connexion, ce module ne fait
// que ne pas la réveiller.
//
// L'APPAIRAGE N'EST PAS LA CONNEXION. On peut être connecté sans qu'aucun
// projet soit appairé : c'est même l'état normal juste après une première
// connexion. Rien ne part tant qu'un projet n'a pas été explicitement
// appairé, et le refus « jamais pour ce projet » est définitif.
// -----------------------------------------------------------------------

#pragma once

#include "Bridge.h"
#include "Connection.h"
#include "TextModel.h"

namespace bridge {

class AccountApi {
public:
    // 'connection' et 'document' doivent rester valides aussi longtemps
    // que le Bridge. Construire un AccountApi n'ouvre RIEN.
    AccountApi(net::Connection& connection, core::Document& document);

    bool registerOn(Bridge& bridge);

private:
    net::Connection& m_connection;
    core::Document& m_document;

    json state(const json& payload);
    json signIn(const json& payload);
    json signOut(const json& payload);
    json pair(const json& payload);
    json unpair(const json& payload);
    json never(const json& payload);
    json setAutoSync(const json& payload);

    // Décrit l'appairage d'un manuscrit pour l'interface. Chemin vide =
    // manuscrit jamais enregistré : il ne peut pas encore être appairé,
    // faute d'endroit où ranger la décision.
    json pairingJson(const std::string& path) const;
};

} // namespace bridge
