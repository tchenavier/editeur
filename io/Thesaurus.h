// Thesaurus.h
// -----------------------------------------------------------------------
// Dictionnaire de synonymes au format MyThes (fin de la Phase 1 ; le
// cadrage prévoyait ce format ou des données ouvertes équivalentes).
//
// Le format est un simple fichier texte :
//
//     UTF-8                                  <- encodage, première ligne
//     roman|1                                <- mot, nombre de sens
//     (Adjectif)|conte|histoire|fiction|...  <- un sens, puis ses synonymes
//
// Aucune bibliothèque n'est nécessaire pour le lire — d'où ce module de
// cent lignes plutôt qu'une dépendance de plus.
//
// STRATÉGIE DE MÉMOIRE : le fichier fait environ 5 Mo. On le garde en
// mémoire tel quel et on n'indexe QUE la position de chaque mot ; le
// détail d'une entrée n'est analysé qu'au moment où on la demande.
// Analyser d'emblée les 36 000 entrées coûterait quatre fois plus de
// mémoire pour un résultat dont on n'utilisera qu'une poignée de lignes.
// -----------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace io {

// Un sens du mot, avec ses synonymes.
struct ThesaurusMeaning {
    std::string label;                   // « (Nom) », « (Adjectif) »...
    std::vector<std::string> synonyms;
};

class Thesaurus {
public:
    // Charge depuis le contenu du fichier (séparé de la lecture disque
    // pour être testable sans fichier). Retourne false si le contenu ne
    // ressemble pas à un thésaurus.
    bool load(std::string content);

    // Charge depuis un fichier .dat.
    bool loadFile(const std::filesystem::path& path, std::string* errorMessage = nullptr);

    bool loaded() const { return !m_index.empty(); }
    size_t entryCount() const { return m_index.size(); }

    // Sens et synonymes d'un mot. Liste vide si le mot est absent. La
    // recherche essaie le mot tel quel, puis sa forme minuscule. Les
    // couples exclus (voir plus bas) sont retirés du résultat.
    std::vector<ThesaurusMeaning> lookup(const std::string& word) const;

    // --- Corrections -------------------------------------------------
    //
    // POURQUOI CE MÉCANISME : ce thésaurus est associatif plus que
    // strictement synonymique, et comporte de vraies erreurs (« gâteau »
    // y figure comme synonyme de « noir »). Aucun critère automatique ne
    // permet de les distinguer : la réciprocité, mesurée sur les 422 000
    // couples du fichier, écarterait surtout de bons synonymes.
    //
    // La correction vient donc de l'auteur, d'un clic, et se conserve
    // dans un fichier VERSIONNÉ du projet — et non dans le fichier de
    // dictionnaire, qui est retéléchargeable et dont toute retouche
    // disparaîtrait au prochain fetch_deps.
    //
    // Les exclusions sont SYMÉTRIQUES : déclarer que « gâteau » n'est pas
    // un synonyme de « noir » vaut aussi dans l'autre sens. C'est la même
    // relation fausse, l'auteur n'a pas à la corriger deux fois.

    // Format du fichier : un couple « mot|synonyme » par ligne, lignes
    // vides et lignes commençant par # ignorées. Lecture tolérante : une
    // ligne abîmée est passée, le reste s'applique.
    bool loadExclusions(const std::filesystem::path& path,
                        std::string* errorMessage = nullptr);
    void setExclusions(const std::vector<std::pair<std::string, std::string>>& pairs);

    // Ajoute un couple. Retourne false s'il était déjà connu.
    bool addExclusion(const std::string& word, const std::string& synonym);

    // Réécrit le fichier (écriture atomique), trié, avec un en-tête
    // explicatif pour qui l'ouvrirait à la main.
    bool saveExclusions(const std::filesystem::path& path,
                        std::string* errorMessage = nullptr) const;

    size_t exclusionCount() const { return m_exclusions.size(); }
    bool isExcluded(const std::string& word, const std::string& synonym) const;

private:
    std::string m_content;
    // Position de chaque mot dans m_content, triée par mot : la
    // recherche est binaire, sans copier les 36 000 mots.
    std::vector<std::pair<std::string, size_t>> m_index;

    // Couples refusés, en minuscules et ordonnés (petit mot d'abord) pour
    // que la symétrie soit acquise par construction plutôt que testée
    // deux fois à chaque recherche.
    std::set<std::pair<std::string, std::string>> m_exclusions;

    std::vector<ThesaurusMeaning> parseAt(size_t offset) const;
};

} // namespace io
