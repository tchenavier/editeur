// Epub.h
// -----------------------------------------------------------------------
// Export EPUB 3 (Phase 4), construit à la main : une archive ZIP
// contenant du XHTML et une feuille de style (cf. cadrage — « Export EPUB
// construit soi-même : zip + XHTML/CSS »).
//
// Structure produite (celle qu'attendent les liseuses) :
//
//   mimetype                     <- EN PREMIER, non compressé (exigence)
//   META-INF/container.xml       <- désigne le fichier .opf
//   OEBPS/content.opf            <- métadonnées, manifeste, ordre de lecture
//   OEBPS/nav.xhtml              <- table des matières EPUB 3
//   OEBPS/toc.ncx                <- table des matières EPUB 2 (compatibilité
//                                   des liseuses anciennes)
//   OEBPS/style.css
//   OEBPS/text/titre.xhtml       <- page de titre (couverture, auteur, résumé)
//   OEBPS/text/chapNNN.xhtml     <- un fichier par chapitre
//   OEBPS/images/imgNNN.png      <- couverture et illustrations embarquées
//
// Tout le contenu vient d'un io::Book, donc le manuscrit et la bible
// narrative passent par exactement le même code.
//
// Le fichier produit est DÉTERMINISTE : deux exports du même livre
// donnent des octets identiques (utile pour les tests, et pour voir d'un
// coup d'œil si un export a changé). L'appelant peut fournir la date
// réelle s'il préfère qu'elle figure dans le fichier.
// -----------------------------------------------------------------------

#pragma once

#include "Book.h"

#include <filesystem>
#include <string>

namespace io {

struct EpubOptions {
    // Page de titre : couverture, titre, auteur, texte de présentation.
    bool titlePage = true;
    // Page « Table des matières » lisible, EN PLUS de la table de
    // navigation interne (que les liseuses affichent dans leur menu).
    bool tableOfContents = true;
    // Date de dernière modification, au format « AAAA-MM-JJTHH:MM:SSZ ».
    // Vide = date fixe (sortie déterministe).
    std::string modified;
    // Identifiant unique du livre (« urn:uuid:... »). Vide = dérivé du
    // titre et de l'auteur : le même livre garde le même identifiant
    // d'un export à l'autre, ce qui évite qu'une liseuse le prenne pour
    // un nouveau livre à chaque mise à jour.
    std::string identifier;
    // Horodatage inscrit dans le ZIP (0 = date fixe). Renseigné par
    // l'appelant s'il veut la date réelle sur les fichiers de l'archive.
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
};

// Construit l'archive EPUB complète en mémoire (octets du fichier .epub).
std::string buildEpub(const Book& book, const EpubOptions& options = {});

// Écrit l'EPUB sur disque (écriture atomique, comme toute écriture du
// projet). Retourne false et renseigne *errorMessage en cas d'échec.
bool writeEpub(const Book& book,
               const std::filesystem::path& path,
               const EpubOptions& options = {},
               std::string* errorMessage = nullptr);

// Identifiant « urn:uuid:... » DÉTERMINISTE dérivé d'un texte (même
// texte -> même identifiant). Ce n'est pas un UUID aléatoire : c'est
// voulu, et c'est ce qui permet à une liseuse de reconnaître une nouvelle
// version du même livre. Exposé pour les tests.
std::string uuidFromText(const std::string& text);

} // namespace io
