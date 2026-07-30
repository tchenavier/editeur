// ProjectIndex.h
// -----------------------------------------------------------------------
// Inventaire du projet (Phase 5, jalon A).
//
// C'est la photographie de ce que contient un projet à un instant donné :
// pour chaque fichier, son chemin, sa taille, sa date et l'empreinte de
// son contenu. Deux inventaires se comparent pour savoir ce qui a changé
// — c'est la base de toute synchronisation ultérieure, mais c'est déjà
// utile seul (savoir ce qui a bougé depuis la dernière sauvegarde).
//
// CE QUI EST INVENTORIÉ, et pourquoi cette règle : tout ce qui vit DANS
// le dossier du manuscrit, à savoir le fichier .md lui-même, le dossier
// des fiches, et les images rangées à côté. Sont exclus le dossier des
// sauvegardes horodatées (elles se reconstituent, les synchroniser
// n'aurait pas de sens) et le dossier technique de la synchronisation
// lui-même (il décrirait alors sa propre description).
//
// LIMITE ASSUMÉE : une illustration rangée AILLEURS que dans le dossier
// du projet n'est pas inventoriée. Elle n'a pas de chemin relatif au
// projet, donc rien à quoi la rattacher sur un autre ordinateur. Le jour
// où la synchronisation des images comptera vraiment, la bonne réponse
// sera de proposer à l'auteur de les recopier dans son dossier de projet,
// pas d'inventer des chemins absolus qui ne voudront rien dire ailleurs.
//
// Le calcul est du PUR TRAITEMENT une fois les fichiers lus : la
// comparaison de deux inventaires se teste sans toucher au disque.
// -----------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace io {

struct ProjectFile {
    std::string path;      // chemin RELATIF au dossier du projet, séparateur '/'
    uint64_t    size = 0;
    std::string modToken;  // date de dernière modification (utils::fileModToken)
    std::string sha256;    // empreinte du contenu
};

struct ProjectIndex {
    std::string projectId;
    std::vector<ProjectFile> files; // toujours triés par chemin

    const ProjectFile* find(const std::string& path) const;
};

// Nature d'un changement entre deux inventaires.
enum class ChangeKind { Added, Modified, Removed };

struct Change {
    ChangeKind  kind = ChangeKind::Added;
    std::string path;
    std::string sha256; // empreinte APRÈS le changement (vide si supprimé)
    uint64_t    size = 0;
};

// Nom lisible d'un changement, tel qu'il apparaît dans le journal.
const char* changeKindName(ChangeKind kind);
bool changeKindFromName(const std::string& name, ChangeKind& out);

// --- Emplacements techniques -----------------------------------------
// Même convention que « <nom>_backups » et « <nom>_fiches » : un dossier
// voisin du manuscrit, au nom explicite, visible. Rien de caché : l'auteur
// doit pouvoir comprendre ce qu'il y a dans son dossier.
std::filesystem::path syncFolderFor(const std::filesystem::path& manuscriptPath);
std::filesystem::path indexPathFor(const std::filesystem::path& manuscriptPath);
std::filesystem::path journalPathFor(const std::filesystem::path& manuscriptPath);

// Construit l'inventaire en lisant le disque. Un fichier illisible est
// IGNORÉ (comme une fiche abîmée au chargement) plutôt que de faire
// échouer tout l'inventaire ; le compteur 'skipped', s'il est fourni, dit
// combien.
ProjectIndex buildIndex(const std::filesystem::path& manuscriptPath,
                        const std::string& projectId,
                        size_t* skipped = nullptr);

// --- Sérialisation ----------------------------------------------------
// Format en lignes, lisible : une ligne d'en-tête, une ligne par fichier,
// le chemin en DERNIER (il peut contenir des espaces, pas les autres
// champs). La lecture est tolérante : une ligne abîmée est ignorée.
std::string serializeIndex(const ProjectIndex& index);
ProjectIndex parseIndex(const std::string& text);

bool saveIndex(const ProjectIndex& index, const std::filesystem::path& file,
               std::string* errorMessage = nullptr);
bool loadIndex(ProjectIndex& index, const std::filesystem::path& file,
               std::string* errorMessage = nullptr);

// Différences entre deux inventaires, triées par chemin. Pur calcul :
// aucun accès au disque, donc entièrement testable.
std::vector<Change> diffIndex(const ProjectIndex& previous, const ProjectIndex& current);

} // namespace io
