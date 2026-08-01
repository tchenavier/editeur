// SyncRunner.h
// -----------------------------------------------------------------------
// Exécuter un plan de synchronisation : la seule pièce qui, du côté
// client, fait réellement bouger des fichiers.
//
// LE PARTAGE DES RÔLES, et il est strict :
//   io/SyncPlan   décide     (quoi faire, sans rien faire)
//   net/SyncClient parle     (une requête, une réponse, rien de plus)
//   net/SyncRunner exécute   (ce fichier)
//
// Aucune de ces trois pièces ne fait le travail d'une autre. C'est ce qui
// permet de tester la décision sans réseau, le dialogue sans disque, et
// l'exécution sans ni l'un ni l'autre.
//
// DEUX RÈGLES DE CONDUITE, qui font toute la différence quand ça se passe
// mal :
//
//   1. UNE PANNE ARRÊTE TOUT. Serveur injoignable, en panne ou débordé :
//      on s'arrête net. Continuer reviendrait à répéter la même erreur
//      pour chaque fichier restant, à faire attendre l'auteur, et à
//      remplir son écran de messages identiques.
//
//   2. UN REFUS N'ARRÊTE RIEN. Un fichier en conflit, trop gros ou
//      interdit ne concerne que lui : on le note et on passe au suivant.
//      Renoncer à tout le reste parce qu'un chapitre pose problème serait
//      punir l'auteur pour un incident local.
//
// CE QUI A RÉELLEMENT ABOUTI est rendu à l'appelant, chemin par chemin.
// C'est ce qui permet à la base de n'avancer que sur ce qui est acquis, et
// donc à une synchronisation interrompue de reprendre exactement où elle
// s'est arrêtée — sans rien perdre ni rien refaire deux fois.
// -----------------------------------------------------------------------

#pragma once

#include "SyncClient.h"
#include "SyncPlan.h"

#include <string>
#include <vector>

namespace net {

// Le disque, réduit à ce dont l'exécution a besoin. Une interface, pour la
// même raison que le transport en est une : pouvoir éprouver les cas
// pénibles — écriture refusée, fichier disparu entre-temps — sans les
// provoquer pour de vrai.
class FileStore {
public:
    virtual ~FileStore() = default;

    // Les chemins sont RELATIFS au dossier du projet, séparateur '/'.
    virtual bool read(const std::string& path, std::string& content,
                      std::string* error) = 0;
    virtual bool write(const std::string& path, const std::string& content,
                       std::string* error) = 0;
    virtual bool remove(const std::string& path, std::string* error) = 0;
};

// Ce qu'un incident sur un fichier donné vaut la peine de dire à l'auteur.
struct SyncProblem {
    std::string path;
    std::string message;
    bool conflict = false; // vrai pour un conflit, qui n'est pas une panne
};

struct SyncReport {
    size_t downloaded = 0;
    size_t uploaded = 0;
    size_t deletedLocal = 0;
    size_t deletedRemote = 0;
    size_t conflicts = 0;  // détectés au plan, ou révélés par le serveur
    size_t blocked = 0;    // n'ont pas pu partir : projet en lecture seule
    size_t failed = 0;     // refusés pour une autre raison

    // Interrompu par une panne : ce qui reste est à refaire, et le
    // prochain plan le retrouvera tout seul.
    bool interrupted = false;
    std::string interruption;

    // Chemins dont l'action a ABOUTI. À passer tel quel à io::advanceBase.
    std::vector<std::string> done;
    std::vector<SyncProblem> problems;

    bool clean() const {
        return !interrupted && conflicts == 0 && failed == 0 && blocked == 0;
    }
    size_t changedFiles() const {
        return downloaded + uploaded + deletedLocal + deletedRemote;
    }
};

// Exécute le plan. Ne touche ni à l'inventaire ni au journal : l'appelant
// décide quoi en faire à partir de 'done' — c'est lui qui sait où ils
// vivent, et lui qui doit pouvoir décider de ne rien enregistrer.
SyncReport runSync(SyncClient& client, const std::string& projectId,
                   const io::SyncPlan& plan, FileStore& files);

} // namespace net
