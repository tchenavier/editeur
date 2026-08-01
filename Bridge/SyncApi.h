// SyncApi.h
// -----------------------------------------------------------------------
// Enregistre les commandes "sync.*" : voir ce qui va se passer, puis le
// faire (Phase 5, jalon D).
//
// Commandes exposées :
//   sync.plan -> { path }
//       Calcule ce qu'une synchronisation ferait, SANS RIEN FAIRE : elle
//       lit l'inventaire local, demande celui du serveur, et rend la liste
//       des envois, réceptions, suppressions et conflits.
//   sync.run  -> { path }
//       Recalcule le plan, l'exécute, puis fait avancer le témoin de
//       dernière synchronisation — uniquement sur ce qui a réellement
//       abouti.
//
// POURQUOI DEUX COMMANDES ET NON UNE. « Synchroniser » est une opération
// qui écrase des fichiers. L'auteur doit pouvoir regarder avant de
// décider, exactement comme il peut comparer deux versions avant
// d'enregistrer par-dessus. Le plan est d'ailleurs recalculé par
// sync.run : ce qu'on a vu peut avoir changé entre-temps, et c'est le
// serveur qui aura le dernier mot grâce à « If-Match ».
//
// TROIS CONDITIONS, vérifiées dans cet ordre, et refusées avec une phrase
// qui dit quoi faire :
//   - le manuscrit doit être enregistré (sans fichier, pas d'inventaire) ;
//   - il faut être connecté (sinon rien n'existe pour parler au serveur) ;
//   - le projet doit être appairé (être connecté ne suffit pas — voir
//     Bridge/AccountApi.h).
//
// CE MODULE EST SYNCHRONE, et c'est une limite assumée pour ce jalon :
// l'interface attend la fin de la synchronisation. Sur un réseau local et
// pour un manuscrit, c'est l'affaire d'un instant ; sur une liaison lente
// avec beaucoup d'illustrations, cela se verrait. Rendre l'opération
// asynchrone demanderait de sortir le modèle du fil principal, ce qui est
// un chantier en soi — à faire quand le besoin sera constaté, pas avant.
// -----------------------------------------------------------------------

#pragma once

#include "Bridge.h"
#include "Connection.h"
#include "TextModel.h"

namespace bridge {

class SyncApi {
public:
    // 'connection' et 'document' doivent rester valides aussi longtemps
    // que le Bridge. Construire un SyncApi n'ouvre rien et ne lit rien.
    SyncApi(net::Connection& connection, core::Document& document);

    bool registerOn(Bridge& bridge);

private:
    net::Connection& m_connection;
    core::Document& m_document;

    json plan(const json& payload);
    json run(const json& payload);
};

} // namespace bridge
