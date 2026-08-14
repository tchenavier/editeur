// TextModel.h
// -----------------------------------------------------------------------
// Modèle de données central de l'éditeur.
// Ce fichier ne dépend d'aucune bibliothèque graphique (pas de Qt, pas de
// webview) : il est testable seul, ce qui correspond au découpage core/
// défini dans l'architecture du projet.
//
// Principe retenu (cf. discussion) :
//   - Le stockage du formatage se fait PAR CARACTÈRE (comme QTextCharFormat
//     dans Qt), via des "plages de formatage" (FormatRun) contiguës.
//   - La granularité "par mot" n'est PAS imposée aux données, mais appliquée
//     seulement au niveau de l'ergonomie : la barre d'outils et le
//     regroupement de l'historique d'annulation utilisent les limites de
//     mots (voir WordBoundary ci-dessous), sans jamais limiter ce que le
//     modèle peut représenter (ex: exposant sur "1er").
// -----------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <algorithm>

namespace core {

// -------------------------------------------------------------------
// Styles de caractères, combinables (bitmask).
// Utiliser un masque de bits plutôt qu'une struct à booléens permet de
// comparer/fusionner des styles avec de simples opérations binaires,
// ce qui simplifie la fusion des plages contiguës (voir mergeAdjacentRuns).
// -------------------------------------------------------------------
enum class TextStyle : uint32_t {
    None          = 0,
    Bold          = 1u << 0,
    Italic        = 1u << 1,
    Underline     = 1u << 2,
    Strikethrough = 1u << 3,
    Superscript   = 1u << 4,  // nécessaire pour "1er", "2e", etc.
    Subscript     = 1u << 5,
};

inline TextStyle operator|(TextStyle a, TextStyle b) {
    return static_cast<TextStyle>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline TextStyle operator&(TextStyle a, TextStyle b) {
    return static_cast<TextStyle>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline TextStyle operator~(TextStyle a) {
    return static_cast<TextStyle>(~static_cast<uint32_t>(a));
}
inline bool hasStyle(TextStyle set, TextStyle flag) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(flag)) != 0;
}

// -------------------------------------------------------------------
// Type de bloc : correspond aux titres H1-H6 et au paragraphe standard.
// -------------------------------------------------------------------
enum class BlockType {
    Paragraph,
    Heading1, Heading2, Heading3,
    Heading4, Heading5, Heading6,
};

// -------------------------------------------------------------------
// FormatRun : une plage de caractères contigus partageant le même style.
// [start, start + length) en indices de caractères dans le texte du bloc.
// Sécurité : les plages ne se chevauchent jamais (invariant maintenu par
// TextBlock, jamais construit à la main ailleurs).
// -------------------------------------------------------------------
struct FormatRun {
    size_t start;
    size_t length;
    TextStyle style;
    // Taille des caractères, en POURCENTAGE de la taille normale du corps
    // de texte (100 = normal). Volontairement RELATIVE : elle se combine
    // avec le zoom de lecture et reste correcte quel que soit le thème.
    // Sérialisée en em (pct/100) via une balise <span> (cf. MarkdownIO).
    // Dimension indépendante du masque de style : le gras reste un booléen,
    // la taille est une valeur.
    uint16_t sizePercent = 100;
    // Couleur des caractères. 0 = par défaut (hérite de la couleur du thème).
    // Sinon 0xFF000000 | RVB (24 bits) : le bit de poids fort marque « couleur
    // définie », ce qui distingue le noir explicite (#000000 -> 0xFF000000) de
    // l'absence de couleur (0). Autre dimension indépendante du style, comme la
    // taille. Sérialisée en <span style="color:#rrggbb"> (cf. MarkdownIO).
    uint32_t color = 0;

    size_t end() const { return start + length; }
    bool hasColor() const { return (color & 0xFF000000u) != 0; }

    // Deux plages ont « le même format » si style, taille ET couleur
    // coïncident : seules celles-là peuvent fusionner (voir mergeAdjacentRuns).
    bool sameFormat(const FormatRun& other) const {
        return style == other.style && sizePercent == other.sizePercent &&
               color == other.color;
    }
    // Plage sans aucune information de mise en forme (équivalente à un
    // « trou » non stylé) : inutile de la conserver.
    bool isDefault() const {
        return style == TextStyle::None && sizePercent == 100 && color == 0;
    }
};

// Conversions couleur <-> texte, partagées par le pont et la sérialisation.
// colorToHex : "#rrggbb" si la couleur est définie, "" sinon.
// colorFromHex : "#rrggbb" ou "rrggbb" -> 0xFF000000|RVB ; 0 si vide/invalide.
std::string colorToHex(uint32_t color);
uint32_t colorFromHex(const std::string& hex);

// -------------------------------------------------------------------
// TextBlock : un paragraphe ou un titre, avec son texte et ses plages
// de formatage. C'est l'équivalent d'un QTextBlock dans Qt, en plus
// simple.
// -------------------------------------------------------------------
class TextBlock {
public:
    explicit TextBlock(BlockType type = BlockType::Paragraph) : m_type(type) {}

    BlockType type() const { return m_type; }
    void setType(BlockType type) { m_type = type; }

    const std::u32string& text() const { return m_text; }
    size_t length() const { return m_text.size(); }

    // Insère du texte à la position donnée. Les plages existantes après
    // cette position sont décalées, celles qui contiennent la position
    // sont étendues. Ne casse jamais l'invariant "plages non chevauchantes".
    void insertText(size_t pos, const std::u32string& text);

    // Supprime [pos, pos+count). Les plages sont raccourcies ou fusionnées
    // en conséquence ; une plage vidée est retirée.
    void removeText(size_t pos, size_t count);

    // Applique (ajoute) un style sur [pos, pos+count), caractère par
    // caractère si nécessaire : peut créer une plage plus fine qu'un mot,
    // c'est voulu (cf. cas "1er").
    void applyStyle(size_t pos, size_t count, TextStyle style);

    // Retire un style sur [pos, pos+count).
    void removeStyle(size_t pos, size_t count, TextStyle style);

    // Fixe la taille des caractères de [pos, pos+count) au pourcentage
    // donné (100 = normal). Comme applyStyle, découpe les plages au
    // besoin ; contrairement à un style, elle REMPLACE la taille (ce
    // n'est pas un cumul). Le masque de style existant est préservé.
    void setFontSize(size_t pos, size_t count, uint16_t sizePercent);

    // Fixe la couleur des caractères de [pos, pos+count) (0 = revenir à la
    // couleur par défaut). Comme setFontSize, découpe les plages au besoin,
    // REMPLACE la couleur, et préserve le style et la taille existants.
    void setColor(size_t pos, size_t count, uint32_t color);

    // Style effectif au caractère donné (None si hors plage ou texte vide).
    TextStyle styleAt(size_t pos) const;

    // Taille effective (en %) au caractère donné (100 si hors plage).
    uint16_t sizeAt(size_t pos) const;

    // Couleur effective au caractère donné (0 = par défaut, si hors plage).
    uint32_t colorAt(size_t pos) const;

    const std::vector<FormatRun>& formatRuns() const { return m_runs; }

private:
    BlockType m_type;
    std::u32string m_text;
    std::vector<FormatRun> m_runs; // triées par 'start', jamais chevauchantes

    // Fusionne les plages adjacentes ayant exactement le même style.
    // Appelé après chaque modification pour garder le vecteur m_runs
    // aussi compact que possible (moins de plages = sauvegarde et
    // rendu plus rapides, et diffs plus lisibles pour le débogage).
    void mergeAdjacentRuns();
};

// -------------------------------------------------------------------
// WordBoundary : utilitaires de limites de mots.
// Ne stocke rien : calcule à la volée les frontières autour d'une
// position, à partir du texte d'un bloc. Utilisé uniquement par la
// couche ergonomie (barre d'outils, regroupement de l'undo), jamais
// par le modèle de données lui-même.
// -------------------------------------------------------------------
namespace WordBoundary {

    // Un caractère "sépare" les mots s'il s'agit d'un espace ou d'une
    // ponctuation. Centralisé ici pour n'avoir qu'un seul endroit à
    // ajuster si on veut affiner la règle plus tard (ex: apostrophes
    // françaises dans "aujourd'hui" ne doivent pas couper le mot).
    bool isWordSeparator(char32_t ch);

    // Retourne [start, end) du mot contenant 'pos'. Si 'pos' tombe sur
    // un séparateur, retourne une plage vide (start == end).
    struct Range { size_t start; size_t end; };
    Range wordAt(const std::u32string& text, size_t pos);

} // namespace WordBoundary

// -------------------------------------------------------------------
// DocumentMetadata : métadonnées du manuscrit, sérialisées en en-tête
// YAML du fichier Markdown (cf. cadrage : titre, auteur, couverture,
// résumé). Stockées en UTF-8 (std::string) : elles ne sont jamais
// éditées caractère par caractère, contrairement au texte des blocs.
// -------------------------------------------------------------------
struct DocumentMetadata {
    std::string title;
    std::string author;
    std::string coverPath; // chemin vers l'image de couverture (PNG)
    std::string summary;

    // Identifiant stable du projet (Phase 5). Vide tant que le manuscrit
    // n'a jamais été enregistré ; attribué à la première sauvegarde.
    //
    // Il est rangé ici, donc écrit dans l'en-tête du fichier, pour qu'il
    // VOYAGE AVEC LE PROJET : renommer le fichier ou déplacer le dossier
    // ne doit pas faire perdre son identité au manuscrit. Ce n'est pas un
    // secret : il identifie, il n'autorise rien.
    std::string projectId;
};

// -------------------------------------------------------------------
// Document : ensemble ordonné de blocs (chapitres/paragraphes).
// -------------------------------------------------------------------
class Document {
public:
    std::vector<TextBlock>& blocks() { return m_blocks; }
    const std::vector<TextBlock>& blocks() const { return m_blocks; }

    DocumentMetadata& metadata() { return m_metadata; }
    const DocumentMetadata& metadata() const { return m_metadata; }

    size_t wordCount() const;

    // --- Opérations structurelles (utilisées par l'UI via le pont) ------
    // Toutes bornent leurs indices/positions et ne font rien en silence
    // si l'entrée est hors limites (jamais d'accès invalide). Elles
    // s'appuient sur l'API publique de TextBlock : les invariants de
    // formatage restent garantis par TextBlock, jamais réécrits ici.

    // Change le type d'un bloc (paragraphe <-> titre H1..H6).
    void setBlockType(size_t index, BlockType type);

    // Coupe le bloc à 'pos' : la partie [0,pos) reste dans le bloc, la
    // partie [pos,fin) part dans un NOUVEAU paragraphe inséré juste
    // après (comportement de la touche Entrée). Les styles suivent leur
    // texte. Le nouveau bloc est toujours un paragraphe.
    void splitBlock(size_t index, size_t pos);

    // Fusionne le bloc 'index' dans le précédent (index-1) : son texte
    // et ses styles sont ajoutés à la fin du précédent, puis il est
    // retiré (comportement du Retour arrière en début de bloc). Sans
    // effet si index == 0 (pas de bloc précédent).
    void mergeWithPrevious(size_t index);

    // Déplace la plage de 'count' blocs commençant à 'start' pour qu'elle
    // vienne se placer JUSTE AVANT le bloc qui occupait l'indice 'before'
    // (before == nombre de blocs => déplacement à la fin). Sert à la
    // réorganisation des chapitres par glisser-déposer (un chapitre = un
    // titre H1 + les blocs qui le suivent, la couche UI calcule la plage).
    // Tout est borné. Retourne true si un déplacement a réellement eu
    // lieu, false si l'opération était sans effet (destination à
    // l'intérieur de la plage, indices hors limites...).
    bool moveBlocks(size_t start, size_t count, size_t before);

private:
    std::vector<TextBlock> m_blocks;
    DocumentMetadata m_metadata;
};

} // namespace core
