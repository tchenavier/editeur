// Zip.h
// -----------------------------------------------------------------------
// Écrivain d'archives ZIP minimal, écrit à la main pour la Phase 4
// (l'EPUB est un fichier ZIP contenant du XHTML et du CSS).
//
// POURQUOI PAS UNE LIBRAIRIE (miniz, zlib) :
// le cadrage évoque « une librairie de compression type miniz », mais un
// EPUB reste parfaitement valide avec des entrées STOCKÉES (méthode 0,
// sans compression) : c'est même OBLIGATOIRE pour l'entrée « mimetype ».
// Écrire nous-mêmes ces ~200 lignes évite d'embarquer et de maintenir
// plusieurs milliers de lignes de C tiers, garde l'exécutable sans
// dépendance, et met tout le code de l'archive dans le périmètre de nos
// tests et de l'audit de sécurité (cf. cadrage : stabilité long terme
// prioritaire, aucune dépendance qui puisse casser dans le temps).
// Contrepartie assumée : le fichier produit est plus gros (le texte n'est
// pas compressé ; les images PNG, elles, le sont déjà par nature). Le jour
// où la taille gênerait, il suffira d'ajouter la méthode 8 (deflate) : la
// structure de l'archive, elle, ne changera pas.
//
// SÉCURITÉ : tous les noms d'entrées sont validés (pas de chemin absolu,
// pas de « .. », pas d'antislash) pour qu'aucune archive produite ici ne
// puisse écrire hors de son dossier une fois extraite ; et la taille
// totale est bornée bien en deçà des 4 Gio du format (pas de ZIP64), donc
// aucun champ ne peut déborder silencieusement.
// -----------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace io {

class ZipWriter {
public:
    // Taille maximale acceptée pour UNE entrée et pour l'archive entière.
    // Très au-dessus d'un roman illustré, très en dessous de la limite
    // 32 bits du format : aucun risque de dépassement de champ.
    static constexpr size_t kMaxEntrySize   = 256u * 1024u * 1024u; // 256 Mio
    static constexpr size_t kMaxArchiveSize = 512u * 1024u * 1024u; // 512 Mio

    // Nombre maximal d'entrées. Le format (sans ZIP64) code ce nombre sur
    // 16 BITS : au-delà de 65 535, l'écriture serait silencieusement
    // tronquée et l'archive illisible. On refuse donc d'ajouter au-delà,
    // plutôt que de produire un fichier faux (un manuscrit atteindrait
    // cette limite avec 65 000 chapitres — jamais en pratique, mais une
    // borne franchie en silence est exactement ce qu'on veut éviter).
    static constexpr size_t kMaxEntries = 65535u;

    // Ajoute une entrée non compressée. 'name' est le chemin DANS
    // l'archive, en UTF-8, avec '/' comme séparateur (« OEBPS/nav.xhtml »).
    // Les entrées sortent dans l'ordre où elles ont été ajoutées : la
    // première ajoutée est la première du fichier (l'EPUB l'exige pour
    // « mimetype »).
    // Retourne false — sans rien modifier — si le nom est invalide ou déjà
    // utilisé, ou si la taille dépasse les bornes ci-dessus.
    bool addFile(const std::string& name, const std::string& content);

    size_t count() const { return m_entries.size(); }
    bool contains(const std::string& name) const;

    // Horodatage inscrit dans l'archive. Par défaut une date fixe
    // (2020-01-01 00:00), pour que deux exports du même livre produisent
    // des octets IDENTIQUES : pratique pour les tests et pour comparer
    // deux versions d'un fichier. L'appelant peut fixer la date réelle.
    void setTimestamp(int year, int month, int day, int hour, int minute, int second);

    // Construit l'archive complète en mémoire (octets bruts).
    std::string build() const;

    // Écrit l'archive sur disque de façon ATOMIQUE (fichier temporaire
    // puis renommage, comme le reste du projet) : une coupure en cours
    // d'écriture ne laisse jamais un .epub à moitié écrit.
    bool writeToFile(const std::filesystem::path& path,
                     std::string* errorMessage = nullptr) const;

    // CRC-32 (polynôme IEEE 802.3), exposé pour les tests.
    static uint32_t crc32(const std::string& data);

    // Vérifie qu'un nom d'entrée est acceptable (voir SÉCURITÉ plus haut).
    static bool isValidEntryName(const std::string& name);

private:
    struct Entry {
        std::string name;
        std::string content;
        uint32_t    crc = 0;
        bool        utf8Name = false; // drapeau bit 11 (nom non ASCII)
    };

    std::vector<Entry> m_entries;   // ordre d'écriture (il compte : cf. mimetype)
    // Index des noms déjà pris. Sans lui, contains() parcourrait tout le
    // vecteur à chaque ajout : temps QUADRATIQUE dès qu'un livre contient
    // beaucoup d'illustrations ou de chapitres.
    std::set<std::string> m_names;
    size_t   m_totalSize = 0;   // somme des contenus déjà ajoutés
    uint16_t m_dosTime = 0;     // 00:00:00
    uint16_t m_dosDate = 0x5021; // 2020-01-01 : ((2020-1980)<<9)|(1<<5)|1
};

} // namespace io
