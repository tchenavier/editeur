// SyncPlan.h
// -----------------------------------------------------------------------
// Le moteur de synchronisation : décider quoi faire, sans rien faire.
//
// POURQUOI TROIS INVENTAIRES et non deux. Deux inventaires qui diffèrent
// ne disent pas qui a bougé. Un fichier présent ici et absent là-bas, est-ce
// qu'on vient de le créer, ou est-ce qu'on vient de le supprimer ailleurs ?
// Les deux réponses mènent à des actions opposées — envoyer, ou effacer.
// Le témoin de la DERNIÈRE SYNCHRONISATION RÉUSSIE tranche : ce qui a
// changé par rapport à lui a bougé, le reste non.
//
// C'est le même raisonnement qu'une fusion de versions, et c'est pour la
// même raison : sans base commune, on ne fusionne pas, on devine.
//
// CE MODULE NE TOUCHE À RIEN. Il ne lit pas le disque, ne parle pas au
// réseau, n'écrit aucun fichier. Il reçoit trois inventaires et rend une
// liste d'actions. Tout se teste donc sur des inventaires écrits à la
// main, y compris les cas qu'on n'aurait aucun moyen de provoquer pour de
// vrai — deux modifications simultanées, une suppression contre une
// réécriture, un projet entier disparu.
//
// LE PLAN EST RECALCULÉ, JAMAIS MÉMORISÉ. Une file d'attente d'opérations
// survit mal à une coupure : elle décrit un passé qui n'est peut-être plus
// vrai. Un plan qui se recalcule à partir de l'état réel ne peut pas
// devenir faux — une synchronisation interrompue reprend simplement là où
// elle en est.
// -----------------------------------------------------------------------

#pragma once

#include "ProjectIndex.h"

#include <cstddef>
#include <string>
#include <vector>

namespace io {

enum class SyncAction {
    Download,     // récupérer du serveur (création ou remplacement)
    Upload,       // envoyer au serveur (création ou remplacement)
    DeleteLocal,  // effacer ici : le fichier a été supprimé sur le serveur
    DeleteRemote, // effacer là-bas : le fichier a été supprimé ici
    Conflict,     // modifié des deux côtés : à l'auteur de trancher
    Blocked,      // il y aurait eu à envoyer, mais le projet est en lecture seule
};

const char* syncActionName(SyncAction action);

struct SyncStep {
    SyncAction  action = SyncAction::Download;
    std::string path;

    // Les trois empreintes, vides quand le fichier est absent de
    // l'inventaire correspondant. Elles servent à exécuter l'action :
    // 'remoteSha' est ce qu'on croit être sur le serveur, donc ce qu'on
    // passera en « If-Match » (voir net/PROTOCOLE.md).
    std::string baseSha;
    std::string localSha;
    std::string remoteSha;

    uint64_t size = 0; // taille de la version à transférer, si connue
};

struct SyncPlan {
    std::vector<SyncStep> steps; // triés par chemin : deux calculs donnent le même plan

    size_t count(SyncAction action) const;
    bool hasConflicts() const { return count(SyncAction::Conflict) > 0; }
    bool empty() const { return steps.empty(); }

    // Volume à transférer, en octets : de quoi prévenir avant d'envoyer
    // trente mégaoctets d'illustrations sur une connexion lente.
    uint64_t bytesToTransfer() const;
};

enum class SyncMode {
    // Le partage en lecture seule, livré d'abord (décision du 26/07/2026) :
    // on récupère, on n'envoie jamais. Ce qui aurait été envoyé est
    // rapporté en « Blocked » plutôt que passé sous silence — l'auteur doit
    // savoir que sa modification locale reste locale.
    ReadOnly,
    TwoWay,
};

// Le cœur : compare les trois inventaires et rend les actions à mener.
//
// 'base' peut être vide (première synchronisation) : deux fichiers
// présents des deux côtés avec des contenus différents sont alors un
// conflit, faute de savoir lequel est le plus récent. C'est volontaire —
// une date de fichier ne prouve rien, et écraser au hasard est le seul
// résultat vraiment inacceptable.
SyncPlan planSync(const ProjectIndex& base, const ProjectIndex& local,
                  const ProjectIndex& remote, SyncMode mode);

// L'inventaire à enregistrer comme nouvelle base après une exécution.
// 'done' liste les chemins dont l'action a RÉELLEMENT abouti : ceux-là
// prennent leur nouvel état, les autres gardent celui de l'ancienne base.
// C'est ce qui rend une synchronisation interrompue reprenable sans rien
// perdre ni rien refaire deux fois.
ProjectIndex advanceBase(const ProjectIndex& base, const ProjectIndex& local,
                         const ProjectIndex& remote, const SyncPlan& plan,
                         const std::vector<std::string>& done);

} // namespace io
