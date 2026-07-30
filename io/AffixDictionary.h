// AffixDictionary.h
// -----------------------------------------------------------------------
// Lecture d'un dictionnaire orthographique au format Hunspell (fin de la
// Phase 1 : correcteur orthographique basique).
//
// POURQUOI CE MODULE EXISTE : un fichier « .dic » n'est PAS une liste de
// mots. C'est une liste de LEMMES accompagnés de drapeaux, plus un
// fichier « .aff » qui décrit ce que chaque drapeau autorise comme
// préfixes et suffixes. Sans appliquer ces règles, « mangeaient » ou
// « chevaux » seraient signalés comme des fautes alors que « manger » et
// « cheval » sont dans le dictionnaire.
//
// On développe donc les règles UNE FOIS au chargement, pour obtenir la
// liste complète des formes fléchies. C'est le choix retenu plutôt que
// d'embarquer la bibliothèque Hunspell : le code reste le nôtre, donc
// couvert par nos tests et notre audit, et il n'y a aucune dépendance à
// suivre dans le temps. La contrepartie est assumée : ce moteur couvre ce
// dont le français a besoin ici, pas la totalité d'un format qui sert à
// des dizaines de langues.
//
// CE QUI EST GÉRÉ (et suffit au dictionnaire français de Grammalecte) :
//   SET, FLAG (long), PFX/SFX avec conditions, FULLSTRIP, NEEDAFFIX,
//   FORBIDDENWORD, CIRCUMFIX, KEEPCASE, NOSUGGEST, REP.
// CE QUI NE L'EST PAS, faute d'être utilisé par ce dictionnaire :
//   les règles de mots composés (COMPOUND*), les alias de drapeaux (AF),
//   et les champs morphologiques (ils ne servent qu'à l'analyse
//   grammaticale, pas à la vérification orthographique).
// -----------------------------------------------------------------------

#pragma once

#include <map>
#include <string>
#include <vector>

namespace io {

// Une règle : retirer 'strip' à la fin (ou au début), ajouter 'append',
// à condition que le mot corresponde à 'condition'.
struct AffixRule {
    std::string strip;
    std::string append;
    std::string condition;                 // motif simplifié, voir conditionMatches
    std::vector<std::string> continuation; // drapeaux gagnés par la forme produite
};

// Toutes les règles partageant un même drapeau.
struct AffixClass {
    std::string flag;
    bool suffix = true;        // faux = préfixe
    bool crossProduct = false; // peut se combiner avec un affixe de l'autre bord
    std::vector<AffixRule> rules;
};

struct AffixData {
    std::map<std::string, AffixClass> classes;

    // Drapeaux de service, tels que déclarés par le fichier.
    std::string needAffix;      // le lemme seul n'est PAS un mot valide
    std::string forbiddenWord;  // forme explicitement fautive
    std::string circumfix;      // préfixe et suffixe obligatoirement ensemble
    std::string keepCase;       // casse imposée
    std::string noSuggest;      // valide, mais à ne jamais proposer

    bool fullStrip = false;     // le retrait peut consommer tout le mot
    int flagLength = 1;         // 2 avec « FLAG long »

    // Confusions fréquentes déclarées par le dictionnaire (REP) : elles
    // guident les suggestions bien mieux qu'une distance d'édition seule.
    std::vector<std::pair<std::string, std::string>> replacements;
};

// Découpe une suite de drapeaux (« S.X.() ») selon la longueur déclarée.
std::vector<std::string> splitFlags(const std::string& flags, int flagLength);

// Le mot satisfait-il la condition ? Sous-ensemble du format Hunspell :
// caractères littéraux, « . » (n'importe quel caractère) et classes
// « [abc] » / « [^abc] ». Pour un suffixe la condition porte sur la FIN
// du mot, pour un préfixe sur son DÉBUT.
bool conditionMatches(const std::string& word, const std::string& condition, bool suffix);

// Analyse le contenu d'un fichier .aff.
AffixData parseAff(const std::string& text);

struct ExpandOptions {
    // Les formes élidées (« l'arbre », « qu'il ») ne sont pas conservées :
    // le découpage du texte sépare déjà sur l'apostrophe, ces formes ne
    // seraient jamais recherchées et pèseraient pour rien.
    bool dropApostrophes = true;
    // Garde-fou : un fichier bricolé ne doit pas pouvoir faire enfler la
    // mémoire sans limite.
    size_t maxForms = 4000000;
};

// Développe le contenu d'un fichier .dic avec les règles données.
// Retourne les formes TRIÉES et dédoublonnées, prêtes pour la recherche
// binaire. Les formes marquées « fautives » sont retirées à la fin.
std::vector<std::string> expandDictionary(const std::string& dicText,
                                          const AffixData& affix,
                                          const ExpandOptions& options = {});

} // namespace io
