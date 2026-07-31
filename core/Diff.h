// Diff.h
// -----------------------------------------------------------------------
// Comparaison de deux versions d'un texte.
//
// À QUOI ÇA SERT : relire une version corrigée en vérifiant qu'elle n'a
// rien touché d'autre, et — quand le fichier a changé en dehors de
// l'éditeur — voir ce qui diffère avant de décider quoi écraser.
//
// DEUX PROPRIÉTÉS ATTENDUES, et comment elles sont obtenues :
//
//   « Ignorer le style. » Gratuit : le modèle range le texte d'un côté et
//   les plages de gras/italique de l'autre. Comparer les textes ignore
//   donc le formatage par construction, sans rien filtrer.
//
//   « Ignorer le décalage. » C'est tout le travail. Insérer un mot décale
//   la suite du paragraphe ; une comparaison position par position
//   signalerait tout le reste comme modifié. On ALIGNE donc les deux
//   versions avant de les comparer, à deux étages : les blocs d'abord
//   (un paragraphe ajouté ne contamine pas ses voisins), puis les mots à
//   l'intérieur des blocs qui diffèrent.
//
// Ce module est du PUR CALCUL : aucune entrée/sortie, aucune dépendance à
// l'interface. Il se teste sur des chaînes écrites à la main.
// -----------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace core {

// -------------------------------------------------------------------
// Découpage en mots
// -------------------------------------------------------------------

// Un mot repéré dans un texte. Les positions sont en POINTS DE CODE,
// comme partout ailleurs dans le projet : l'interface peut donc surligner
// directement la plage renvoyée.
struct WordSpan {
    size_t start = 0;
    size_t length = 0;
};

// Un caractère « blanc », insécables comprises. Une seule définition,
// partagée : le découpage en phrases et le redécoupage en paragraphes
// doivent voir exactement les mêmes séparations.
bool isSpaceChar(char32_t c);

// Découpe un texte en mots. La ponctuation et les espaces séparent ; les
// signes eux-mêmes ne sont pas des mots (comparer la ponctuation
// produirait un bruit constant sur du texte remanié).
std::vector<WordSpan> tokenizeWords(const std::u32string& text);

// Découpe un texte en PHRASES. Les plages sont sans espaces au bord, et
// couvrent tout le texte utile dans l'ordre.
//
// À QUOI ÇA SERT : certains outils extérieurs — les correcteurs, en
// particulier — rendent le texte corrigé d'un seul tenant, toute la
// structure en paragraphes perdue. Face à un manuscrit resté découpé,
// l'alignement des blocs n'a alors plus rien à quoi s'accrocher : un bloc
// unique ne peut s'apparier avec aucun des cent autres. La phrase, elle,
// survit à ce traitement — un correcteur retouche des mots, il ne fusionne
// ni ne coupe les phrases. Elle sert donc d'unité de repli.
//
// RÈGLES (français) : coupure après « . ! ? … » et les guillemets ou
// parenthèses qui les suivent, à condition qu'un espace vienne ensuite et
// que la suite commence comme une phrase (majuscule, chiffre, guillemet
// ouvrant, tiret de dialogue). Pas de coupure après une abréviation
// courante (« M. », « Dr », « n° ») ni après une initiale (« J. R. R. »),
// pas de coupure au milieu d'un nombre.
std::vector<WordSpan> splitSentences(const std::u32string& text);

// -------------------------------------------------------------------
// Comparaison de séquences
// -------------------------------------------------------------------

enum class DiffKind {
    Equal,    // présent des deux côtés, inchangé
    Deleted,  // seulement dans l'ancienne version
    Inserted, // seulement dans la nouvelle
};

// Une opération d'alignement portant sur des éléments consécutifs.
struct DiffOp {
    DiffKind kind = DiffKind::Equal;
    size_t oldStart = 0, oldCount = 0;
    size_t newStart = 0, newCount = 0;
};

// Aligne deux suites d'empreintes et retourne les opérations, dans
// l'ordre. C'est la brique commune aux deux étages : on lui donne des
// empreintes de blocs, ou des empreintes de mots.
//
// Méthode : on retire d'abord le début et la fin communs (une correction
// est presque toujours locale), puis on s'appuie sur les éléments qui
// n'apparaissent QU'UNE FOIS de chaque côté — ce sont des points d'ancrage
// sûrs — et on recommence entre ces ancres. Le cas résiduel est traité par
// une plus longue sous-séquence commune, bornée en mémoire : au-delà, le
// segment est déclaré remplacé plutôt que de faire enfler le calcul.
std::vector<DiffOp> diffSequences(const std::vector<uint64_t>& oldItems,
                                  const std::vector<uint64_t>& newItems);

// Empreinte d'un mot, normalisée pour la comparaison.
//
// CE QUI EST NORMALISÉ : les variantes invisibles ou interchangeables —
// espace insécable, tabulation, apostrophe droite contre courbe. Les
// signaler serait du bruit pur.
//
// CE QUI NE L'EST PAS : la casse et les accents. « Le chat » corrigé en
// « le chat » EST une modification, et « eleve » n'est pas « élevé ».
// Pour qui relit une version corrigée, ce sont précisément les
// différences à voir.
uint64_t wordHash(const std::u32string& text, const WordSpan& span);

// Empreinte d'un texte entier de bloc, avec la même normalisation.
uint64_t blockHash(const std::u32string& text);

// -------------------------------------------------------------------
// Comparaison de deux textes de bloc
// -------------------------------------------------------------------

// Plages de mots à mettre en évidence dans chacune des deux versions.
struct WordChanges {
    std::vector<WordSpan> removed; // à marquer dans l'ancien texte
    std::vector<WordSpan> added;   // à marquer dans le nouveau
};

// Compare deux textes mot à mot. Les plages renvoyées sont fusionnées
// quand elles se touchent : surligner « très » puis « beau » séparément
// alors qu'ils se suivent produirait un affichage haché.
WordChanges diffText(const std::u32string& oldText, const std::u32string& newText);

} // namespace core
