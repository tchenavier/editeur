// Speller.h
// -----------------------------------------------------------------------
// Correcteur orthographique (fin de la Phase 1).
//
// Ce module ne lit AUCUN fichier : il reçoit une liste de formes déjà
// développée (voir io/AffixDictionary) et se contente de raisonner
// dessus. C'est la règle du projet — core/ est testable seul — et elle
// vaut ici plus qu'ailleurs : on peut vérifier tout le comportement du
// correcteur avec vingt mots inventés, sans dictionnaire de 20 Mo.
//
// CHOIX DE CONCEPTION
//
// Découpage du texte : l'apostrophe SÉPARE les mots. « l'arbre » donne
// « l » et « arbre ». La partie élidée est reconnue par une petite liste
// fermée (l, d, qu, jusqu, aujourd...), ce qui évite d'avoir à développer
// toutes les formes élidées du dictionnaire — elles quadrupleraient sa
// taille pour rien.
//
// Trait d'union : le mot entier est cherché d'abord (« arc-en-ciel » est
// dans le dictionnaire), et seulement s'il est inconnu on vérifie chaque
// morceau. Sans cela, « c'est-à-dire » ferait trois fautes.
//
// Casse : « Élise », « ÉLISE » et « élise » sont acceptés dès que la
// forme minuscule est connue. En revanche les accents ne sont JAMAIS
// ignorés — « eleve » n'est pas « élevé ». C'est tout l'intérêt d'un
// correcteur français.
// -----------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace core {

class Speller {
public:
    // Formes du dictionnaire, TRIÉES (la recherche est binaire). Le
    // vecteur est consommé : le correcteur en devient propriétaire.
    void setForms(std::vector<std::string> sortedForms);
    bool loaded() const { return !m_forms.empty(); }
    size_t formCount() const { return m_forms.size(); }

    // Confusions fréquentes déclarées par le dictionnaire (REP) : elles
    // guident les suggestions bien mieux qu'une distance d'édition seule.
    void setReplacements(std::vector<std::pair<std::string, std::string>> pairs);

    // Un mot isolé est-il connu ? Tient compte de la casse, du
    // dictionnaire personnel et des mots ignorés.
    bool isKnown(const std::string& word) const;

    // Mot inconnu repéré dans un texte. Les positions sont en POINTS DE
    // CODE, comme partout ailleurs dans le projet (le JS les convertit).
    struct Misspelling {
        size_t start = 0;
        size_t length = 0;
        std::string word;
    };

    // Découpe le texte et retourne les mots inconnus, dans l'ordre.
    std::vector<Misspelling> check(const std::u32string& text) const;

    // Corrections proposées, de la plus plausible à la moins plausible.
    std::vector<std::string> suggest(const std::string& word, size_t max = 7) const;

    // --- Dictionnaire personnel (persistant, tenu par l'interface) ----
    // Indispensable pour un roman : les noms inventés ne seront jamais
    // dans un dictionnaire général.
    void setPersonal(std::vector<std::string> words);
    bool addPersonal(const std::string& word);
    bool removePersonal(const std::string& word);
    const std::vector<std::string>& personal() const { return m_personal; }

    // --- Mots ignorés (le temps de la session seulement) --------------
    void ignoreWord(const std::string& word);
    void clearIgnored();

    // Index phonétique construit ? Utile aux tests et aux mesures : il
    // coûte de la mémoire et du temps au chargement, on doit pouvoir
    // vérifier ce qu'il pèse.
    bool hasPhoneticIndex() const { return !m_phonetic.empty(); }

private:
    // Une forme du dictionnaire, rangée par le SON de son écriture.
    //
    // On garde l'EMPREINTE de la clé, pas la clé : 454 000 chaînes
    // coûteraient une vingtaine de mégaoctets, ces couples en coûtent
    // sept. Les collisions sont possibles et sans conséquence — le
    // candidat est vérifié en recalculant sa vraie clé avant d'être
    // proposé.
    struct PhoneticEntry {
        uint64_t hash = 0;
        uint32_t form = 0;
    };

    std::vector<std::string> m_forms;    // triées
    std::vector<std::string> m_personal; // triées
    std::vector<std::string> m_ignored;  // triées
    std::vector<std::pair<std::string, std::string>> m_replacements;
    std::vector<PhoneticEntry> m_phonetic; // triées par empreinte

    bool inDictionaries(const std::string& word) const;
    bool knownWithCase(const std::string& word) const;

    // Formes dont l'écriture sonne comme 'word', classées de la plus
    // proche à la plus lointaine. Bornée en nombre ET en distance : une
    // liste de cinquante mots qui riment n'aide personne.
    std::vector<std::string> soundAlike(const std::u32string& lowered) const;
};

// --- Aides exposées pour les tests ------------------------------------

// Minuscule française : ne touche QUE la casse, jamais les accents
// (contrairement au repli de casse de la recherche, qui les ignore).
std::u32string toLowerFr(const std::u32string& text);

// Le caractère peut-il faire partie d'un mot ? (lettres et chiffres,
// mais ni ponctuation, ni espaces, ni guillemets)
bool isWordChar(char32_t c);

// Particule élidée reconnue devant une apostrophe (« l' », « jusqu' »).
bool isElision(const std::u32string& lowered);

} // namespace core
