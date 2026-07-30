// ChangeLog.h
// -----------------------------------------------------------------------
// Journal des modifications du projet (Phase 5, jalon A).
//
// À chaque enregistrement, on compare l'inventaire courant au précédent
// et on AJOUTE les différences à la fin d'un journal. Ce journal répond à
// « qu'est-ce qui a changé depuis la dernière synchronisation ? » sans
// avoir à relire tout le projet, et il servira de point de reprise quand
// un transfert sera coupé en cours de route.
//
// POURQUOI UNE ÉCRITURE PAR AJOUT, alors que tout le reste du projet
// écrit de façon atomique (fichier temporaire puis renommage) : parce que
// les deux protègent de choses différentes. Réécrire tout le journal à
// chaque enregistrement mettrait en jeu, à chaque fois, la TOTALITÉ de
// l'historique ; une coupure au mauvais moment le perdrait en entier.
// Avec un simple ajout en fin de fichier, une coupure ne peut abîmer que
// la DERNIÈRE ligne — et la lecture, qui ignore les lignes malformées,
// s'en remet toute seule. Le seul moment où l'on réécrit le journal
// entier est le compactage, et celui-là est atomique.
//
// Le format est en clair, une ligne par changement : l'auteur peut
// l'ouvrir et comprendre ce qui s'est passé. Rien d'illisible dans son
// dossier de travail.
// -----------------------------------------------------------------------

#pragma once

#include "ProjectIndex.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace io {

struct JournalEntry {
    std::string timestamp; // « AAAA-MM-JJTHH:MM:SSZ » (temps universel)
    ChangeKind  kind = ChangeKind::Added;
    std::string path;
    std::string sha256;
    uint64_t    size = 0;
};

// Au-delà de ce nombre de lignes, le journal est compacté : l'historique
// détaillé ancien n'apporte plus rien une fois les changements
// synchronisés, et un fichier qui grossit sans fin dans le dossier d'un
// auteur serait une mauvaise surprise.
constexpr size_t kJournalMaxEntries = 5000;

// Ajoute des changements à la fin du journal (créé au besoin). L'ajout se
// fait en UNE écriture, pour qu'une coupure ne puisse pas entrelacer deux
// lignes.
bool appendChanges(const std::filesystem::path& journalPath,
                   const std::vector<Change>& changes,
                   std::string* errorMessage = nullptr);

// Analyse le contenu d'un journal. Les lignes abîmées (coupure
// d'alimentation pendant une écriture, fichier bricolé à la main) sont
// IGNORÉES : mieux vaut un journal amputé d'une ligne qu'un projet qui
// refuse de s'ouvrir. Séparé de la lecture disque pour être testable et
// « fuzzable » sans toucher au système de fichiers.
std::vector<JournalEntry> parseJournal(const std::string& text);

// Relit le journal depuis le disque. Journal absent = liste vide, ce
// n'est pas une erreur.
std::vector<JournalEntry> readJournal(const std::filesystem::path& journalPath);

// Ne conserve que les 'keep' dernières entrées. Réécriture atomique, car
// ici on remet en jeu tout le fichier. Sans effet si le journal est plus
// court. Retourne le nombre d'entrées supprimées.
size_t compactJournal(const std::filesystem::path& journalPath, size_t keep,
                      std::string* errorMessage = nullptr);

// Horodatage courant en temps universel, au format du journal. Exposé
// pour que les tests puissent vérifier la forme.
std::string utcTimestamp();

} // namespace io
