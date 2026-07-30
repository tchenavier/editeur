// Book.h
// -----------------------------------------------------------------------
// Structure intermédiaire « livre » de la Phase 4 (export).
//
// POURQUOI UNE ÉTAPE INTERMÉDIAIRE : deux sources très différentes
// doivent produire des livres (le manuscrit, et la bible narrative faite
// de fiches), et plusieurs sorties doivent les consommer (EPUB
// aujourd'hui, impression PDF, et tout export HTML futur). Sans pivot, on
// écrirait N×M convertisseurs. Avec, chaque source se convertit une fois
// vers Book, et chaque sortie ne connaît que Book.
//
//        Document (manuscrit) ─┐            ┌─> EPUB (io/Epub)
//                              ├─> Book ────┤
//        Fiches (bible)       ─┘            └─> XHTML / impression PDF
//
// Un Book est du contenu DÉJÀ RENDU en XHTML en ligne (via HtmlText) :
// texte échappé, balises bien imbriquées. Il ne reste plus qu'à
// l'envelopper dans une balise de bloc et un document.
// -----------------------------------------------------------------------

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace core {
class Document;
class CardLibrary;
class SchemaRegistry;
}

namespace io {

// -------------------------------------------------------------------
// Un bloc = une balise de bloc et son contenu XHTML déjà rendu.
// 'tag' vaut "p", "h1".."h6", "ul" (contenu = des <li>) ou "figure"
// (contenu = un <img/>). Volontairement une chaîne et non un énuméré :
// la sortie n'a alors qu'à écrire <tag>contenu</tag>, et ajouter un
// genre de bloc ne demande de toucher à aucun convertisseur.
// -------------------------------------------------------------------
struct BookBlock {
    std::string tag;   // balise de bloc
    std::string html;  // contenu XHTML (déjà échappé)
    std::string cls;   // classe CSS optionnelle
};

// Un chapitre = une unité de découpage (un fichier XHTML dans l'EPUB,
// une page dans le PDF). Pour le manuscrit : un titre de niveau 1.
struct BookChapter {
    std::string title;              // texte brut (table des matières)
    std::vector<BookBlock> blocks;
};

// Une image embarquée (couverture, illustration de fiche).
struct BookImage {
    std::string id;         // identifiant manifeste ("img001")
    std::string fileName;   // nom dans le livre ("img001.png")
    std::string mediaType;  // "image/png", "image/jpeg", "image/gif"
    std::string data;       // octets du fichier
    std::string sourcePath; // chemin d'origine, en UTF-8 (dédoublonnage)
};

struct BookMetadata {
    std::string title;
    std::string author;
    std::string summary;        // texte de présentation (4e de couverture)
    std::string language = "fr";
};

struct Book {
    BookMetadata meta;
    std::vector<BookChapter> chapters;
    std::vector<BookImage> images;
    std::string coverImageId;   // vide si le livre n'a pas de couverture

    // Taille maximale acceptée pour une image embarquée. Au-delà, l'image
    // est ignorée (l'export continue) : mieux vaut un livre sans une
    // illustration qu'un export qui échoue ou un fichier démesuré.
    static constexpr size_t kMaxImageSize = 24u * 1024u * 1024u; // 24 Mio

    // Lit un fichier image et l'ajoute au livre. Retourne son identifiant,
    // ou une chaîne VIDE si le fichier est absent, trop gros, ou n'est pas
    // une image reconnue (le type est déterminé par les octets d'en-tête,
    // pas par l'extension : un « .png » qui est en fait un JPEG est
    // correctement déclaré). Le même fichier ajouté deux fois renvoie le
    // même identifiant (aucune duplication dans le livre).
    std::string addImageFile(const std::filesystem::path& path);

    // Image du livre par identifiant, ou nullptr. ATTENTION : le pointeur
    // désigne un élément de 'images' — un addImageFile() ultérieur peut
    // l'invalider (réallocation du vecteur). À utiliser tout de suite,
    // jamais à conserver.
    const BookImage* image(const std::string& id) const;
};

// Convertit le manuscrit. Le découpage en chapitres se fait aux titres de
// NIVEAU 1 (même règle que le plan et le compteur par chapitre de
// l'interface). Ce qui précède le premier titre forme un chapitre
// d'ouverture. Les paragraphes vides sont ignorés.
// 'baseDir' sert à résoudre un chemin de couverture relatif (dossier du
// manuscrit) ; la couverture est embarquée si elle est lisible.
Book bookFromDocument(const core::Document& doc,
                      const std::filesystem::path& baseDir = {});

// Construit la bible narrative : un chapitre par TYPE de fiche (dans
// l'ordre des schémas), une section par fiche, un paragraphe par champ
// renseigné. Les références vers d'autres fiches sont affichées par leur
// titre, et les illustrations sont embarquées quand elles sont lisibles.
// 'imageBase' résout les chemins d'illustrations relatifs (dossier des
// fiches).
Book bookFromCards(const core::CardLibrary& library,
                   const core::SchemaRegistry& registry,
                   const std::filesystem::path& imageBase = {});

// Type MIME d'une image déduit de ses PREMIERS OCTETS ("image/png",
// "image/jpeg", "image/gif"), ou chaîne vide si ce n'est pas une image
// reconnue. On ne se fie pas à l'extension : un fichier renommé « .png »
// qui est en réalité un JPEG serait déclaré avec un type faux, et refusé
// par les liseuses. Partagé avec le pont (aperçu de la couverture).
std::string sniffImageType(const std::string& data);

} // namespace io
