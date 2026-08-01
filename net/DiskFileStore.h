// DiskFileStore.h
// -----------------------------------------------------------------------
// Le dossier du projet, vu par l'exécution d'une synchronisation.
//
// Il implémente net::FileStore avec de vrais fichiers, et n'apporte qu'une
// chose que le disque ne fait pas tout seul : la CERTITUDE DE NE PAS EN
// SORTIR. Les chemins qu'il reçoit viennent d'un inventaire distant,
// c'est-à-dire d'une machine que l'auteur ne contrôle pas forcément. Un
// serveur fautif ou hostile ne doit pas pouvoir écrire ailleurs que dans
// le dossier du manuscrit.
//
// DEUX BARRIÈRES, et la seconde ne suppose rien de la première :
//   1. le chemin passe la même validation que côté envoi (relatif, sans
//      « .. », sans antislash, sans caractère de contrôle) ;
//   2. le chemin absolu est calculé, puis VÉRIFIÉ comme restant sous la
//      racine du projet. Cette vérification-là ne dépend d'aucune
//      hypothèse sur la forme de l'entrée — c'est elle qui attrapera ce à
//      quoi la première n'aura pas pensé.
//
// Les écritures sont ATOMIQUES (fichier temporaire puis renommage), comme
// toutes les écritures du projet : une coupure au milieu d'une réception
// ne doit pas laisser un chapitre à moitié écrit à la place du bon.
// -----------------------------------------------------------------------

#pragma once

#include "SyncRunner.h"

#include <filesystem>
#include <string>

namespace net {

class DiskFileStore : public FileStore {
public:
    // 'root' est le dossier du manuscrit — celui auquel les chemins de
    // l'inventaire sont relatifs (voir io::buildIndex).
    explicit DiskFileStore(std::filesystem::path root);

    bool read(const std::string& path, std::string& content, std::string* error) override;
    bool write(const std::string& path, const std::string& content,
               std::string* error) override;
    bool remove(const std::string& path, std::string* error) override;

    // Chemin absolu correspondant, ou chemin VIDE si l'entrée est refusée.
    // Exposé parce que c'est la fonction qu'il faut éprouver : tout le
    // reste de ce module n'est que lecture et écriture ordinaires.
    std::filesystem::path resolve(const std::string& path) const;

    const std::filesystem::path& root() const { return m_root; }

private:
    std::filesystem::path m_root;
};

} // namespace net
