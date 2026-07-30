// Speller.cpp
// Implémentation du correcteur orthographique. Voir Speller.h.

#include "Speller.h"

#include "Utf8.h"

#include <algorithm>

namespace core {

namespace {

// Alphabet servant à fabriquer les corrections candidates. Il contient
// les lettres françaises accentuées : sans elles, « eleve » ne pourrait
// jamais être corrigé en « élevé », qui est pourtant la faute la plus
// courante au clavier.
const char32_t* const kAlphabet =
    U"abcdefghijklmnopqrstuvwxyzàâäçéèêëîïôöùûüÿœæ-";

// Particules qui s'élident devant une voyelle. Liste FERMÉE : elles ne
// sont pas des mots à elles seules, mais leur présence devant une
// apostrophe est parfaitement correcte.
const char32_t* const kElisions[] = {
    U"l", U"d", U"j", U"n", U"m", U"t", U"s", U"c", U"y", U"qu",
    U"jusqu", U"lorsqu", U"puisqu", U"quoiqu", U"presqu", U"quelqu",
    U"entr", U"aujourd", U"prud", U"grand", U"aujourd", U"nn",
};

bool binaryContains(const std::vector<std::string>& sorted, const std::string& value) {
    return std::binary_search(sorted.begin(), sorted.end(), value);
}

void insertSorted(std::vector<std::string>& sorted, const std::string& value) {
    const auto position = std::lower_bound(sorted.begin(), sorted.end(), value);
    if (position == sorted.end() || *position != value) sorted.insert(position, value);
}

bool isUpperFr(char32_t c) {
    if (c >= U'A' && c <= U'Z') return true;
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return true; // À-Þ sauf ×
    if (c == 0x0152) return true;                               // Œ
    if (c == 0x0178) return true;                               // Ÿ
    return false;
}

char32_t lowerFr(char32_t c) {
    if (c >= U'A' && c <= U'Z') return c + 32;
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return c + 32; // À-Þ -> à-þ
    if (c == 0x0152) return 0x0153;                               // Œ -> œ
    if (c == 0x0178) return 0x00FF;                               // Ÿ -> ÿ
    return c;
}

char32_t upperFr(char32_t c) {
    if (c >= U'a' && c <= U'z') return c - 32;
    if (c >= 0x00E0 && c <= 0x00FE && c != 0x00F7) return c - 32;
    if (c == 0x0153) return 0x0152;
    if (c == 0x00FF) return 0x0178;
    return c;
}

bool containsDigit(const std::u32string& word) {
    for (char32_t c : word) {
        if (c >= U'0' && c <= U'9') return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------
// Aides publiques
// ---------------------------------------------------------------------

std::u32string toLowerFr(const std::u32string& text) {
    std::u32string out;
    out.reserve(text.size());
    for (char32_t c : text) out += lowerFr(c);
    return out;
}

bool isWordChar(char32_t c) {
    if (c >= U'a' && c <= U'z') return true;
    if (c >= U'A' && c <= U'Z') return true;
    if (c >= U'0' && c <= U'9') return true;
    if (c < 0x00C0) return false;
    // Au-delà : lettres latines étendues, MAIS pas la ponctuation
    // typographique (apostrophe courbe, tirets longs, guillemets...).
    switch (c) {
        case 0x00D7: case 0x00F7:                    // × ÷
        case 0x2018: case 0x2019:                    // ‘ ’
        case 0x201C: case 0x201D:                    // “ ”
        case 0x2013: case 0x2014: case 0x2026:       // – — …
        case 0x00AB: case 0x00BB:                    // « »
            return false;
        default: break;
    }
    return c <= 0x024F || c == 0x0152 || c == 0x0153 || c == 0x0178;
}

bool isElision(const std::u32string& lowered) {
    for (const char32_t* candidate : kElisions) {
        if (lowered == candidate) return true;
    }
    return false;
}

// ---------------------------------------------------------------------
// Dictionnaires
// ---------------------------------------------------------------------

void Speller::setForms(std::vector<std::string> sortedForms) {
    m_forms = std::move(sortedForms);
    // On ne fait pas confiance à l'appelant sur le tri : toute la
    // recherche en dépend, et un tri manquant donnerait des « fautes »
    // incompréhensibles.
    if (!std::is_sorted(m_forms.begin(), m_forms.end())) {
        std::sort(m_forms.begin(), m_forms.end());
    }
}

void Speller::setReplacements(std::vector<std::pair<std::string, std::string>> pairs) {
    m_replacements = std::move(pairs);
}

void Speller::setPersonal(std::vector<std::string> words) {
    m_personal = std::move(words);
    std::sort(m_personal.begin(), m_personal.end());
    m_personal.erase(std::unique(m_personal.begin(), m_personal.end()), m_personal.end());
}

bool Speller::addPersonal(const std::string& word) {
    if (word.empty()) return false;
    if (binaryContains(m_personal, word)) return false;
    insertSorted(m_personal, word);
    return true;
}

bool Speller::removePersonal(const std::string& word) {
    const auto position = std::lower_bound(m_personal.begin(), m_personal.end(), word);
    if (position == m_personal.end() || *position != word) return false;
    m_personal.erase(position);
    return true;
}

void Speller::ignoreWord(const std::string& word) {
    if (!word.empty()) insertSorted(m_ignored, word);
}

void Speller::clearIgnored() {
    m_ignored.clear();
}

bool Speller::inDictionaries(const std::string& word) const {
    return binaryContains(m_forms, word) || binaryContains(m_personal, word) ||
           binaryContains(m_ignored, word);
}

// Le mot est-il connu, à la casse près ? « Élise » et « ÉLISE » passent
// dès que « élise » est connu ; l'inverse n'est pas vrai (un nom propre
// ajouté au dictionnaire personnel avec sa majuscule reste exigé tel
// quel en minuscule seulement s'il y figure).
bool Speller::knownWithCase(const std::string& word) const {
    if (inDictionaries(word)) return true;
    const std::u32string wide = utils::utf8ToU32(word);
    const std::u32string lowered = toLowerFr(wide);
    if (lowered != wide && inDictionaries(utils::u32ToUtf8(lowered))) return true;

    // « ÉLISE » : tout en majuscules -> on teste aussi la forme
    // « Élise » (première lettre seule en majuscule).
    bool allUpper = true;
    for (char32_t c : wide) {
        if (!isUpperFr(c) && !(c >= U'0' && c <= U'9') && c != U'-') { allUpper = false; break; }
    }
    if (allUpper && lowered.size() > 1) {
        std::u32string capitalized = lowered;
        capitalized[0] = upperFr(capitalized[0]);
        if (inDictionaries(utils::u32ToUtf8(capitalized))) return true;
    }
    return false;
}

bool Speller::isKnown(const std::string& word) const {
    if (word.empty()) return true;
    const std::u32string wide = utils::utf8ToU32(word);
    // Un « mot » qui contient un chiffre (1er, 2026, A4) n'est pas du
    // ressort d'un correcteur orthographique.
    if (containsDigit(wide)) return true;
    if (knownWithCase(word)) return true;

    // Composé : le tout est inconnu, mais chaque morceau peut être bon
    // (« porte-plume », « c'est-à-dire »).
    if (wide.find(U'-') != std::u32string::npos) {
        size_t start = 0;
        bool allKnown = true;
        while (start <= wide.size()) {
            const size_t dash = wide.find(U'-', start);
            const size_t end = (dash == std::u32string::npos) ? wide.size() : dash;
            const std::u32string piece = wide.substr(start, end - start);
            if (!piece.empty() && !knownWithCase(utils::u32ToUtf8(piece))) {
                allKnown = false;
                break;
            }
            if (dash == std::u32string::npos) break;
            start = dash + 1;
        }
        if (allKnown) return true;
    }
    return false;
}

// ---------------------------------------------------------------------
// Vérification d'un texte
// ---------------------------------------------------------------------

std::vector<Speller::Misspelling> Speller::check(const std::u32string& text) const {
    std::vector<Misspelling> result;
    if (m_forms.empty()) return result; // dictionnaire absent : on ne signale rien

    const size_t size = text.size();
    size_t i = 0;
    while (i < size) {
        if (!isWordChar(text[i])) { ++i; continue; }
        const size_t start = i;
        while (i < size && isWordChar(text[i])) ++i;

        // Le mot peut se poursuivre après une apostrophe ou un trait
        // d'union (« aujourd'hui », « arc-en-ciel ») : on prend le groupe
        // entier, puis on décide.
        while (i + 1 < size &&
               (text[i] == U'\'' || text[i] == U'’' || text[i] == U'-') &&
               isWordChar(text[i + 1])) {
            ++i;
            while (i < size && isWordChar(text[i])) ++i;
        }

        const std::u32string group = text.substr(start, i - start);

        // Découpage sur les apostrophes : chaque segment est vérifié,
        // sauf les particules élidées, qui sont correctes par nature.
        size_t segStart = 0;
        while (segStart <= group.size()) {
            size_t apos = group.size();
            for (size_t k = segStart; k < group.size(); ++k) {
                if (group[k] == U'\'' || group[k] == U'’') { apos = k; break; }
            }
            const std::u32string segment = group.substr(segStart, apos - segStart);
            const bool elided = (apos < group.size()); // suivi d'une apostrophe

            if (!segment.empty() && !(elided && isElision(toLowerFr(segment)))) {
                const std::string utf8 = utils::u32ToUtf8(segment);
                if (!isKnown(utf8)) {
                    Misspelling entry;
                    entry.start = start + segStart;
                    entry.length = segment.size();
                    entry.word = utf8;
                    result.push_back(std::move(entry));
                }
            }
            if (apos >= group.size()) break;
            segStart = apos + 1;
        }
    }
    return result;
}

// ---------------------------------------------------------------------
// Suggestions
// ---------------------------------------------------------------------

std::vector<std::string> Speller::suggest(const std::string& word, size_t max) const {
    std::vector<std::string> out;
    if (word.empty() || m_forms.empty() || max == 0) return out;

    const std::u32string wide = utils::utf8ToU32(word);
    const std::u32string lowered = toLowerFr(wide);
    const bool wasCapitalized = !wide.empty() && isUpperFr(wide[0]);

    // La casse d'origine est rendue aux propositions : corriger « Bonjur »
    // en « bonjour » obligerait l'auteur à remettre la majuscule.
    auto restoreCase = [&wasCapitalized](const std::string& candidate) {
        if (!wasCapitalized) return candidate;
        std::u32string wideCandidate = utils::utf8ToU32(candidate);
        if (!wideCandidate.empty()) wideCandidate[0] = upperFr(wideCandidate[0]);
        return utils::u32ToUtf8(wideCandidate);
    };

    auto propose = [&](const std::u32string& candidate) {
        if (out.size() >= max || candidate.empty() || candidate == lowered) return;
        const std::string plain = utils::u32ToUtf8(candidate);
        const std::string restored = restoreCase(plain);
        // Le candidat peut figurer en minuscules (dictionnaire général)
        // ou avec sa majuscule (un nom propre du dictionnaire personnel,
        // « Ysalinde ») : chercher une seule des deux formes priverait
        // l'auteur des corrections sur ses propres noms inventés.
        auto inAny = [this](const std::string& value) {
            return binaryContains(m_forms, value) || binaryContains(m_personal, value);
        };
        if (!inAny(plain) && !(restored != plain && inAny(restored))) return;
        if (std::find(out.begin(), out.end(), restored) == out.end()) out.push_back(restored);
    };

    // 1. Confusions déclarées par le dictionnaire (REP) : « ph » -> « f »,
    //    « ai » -> « é »... Ce sont les fautes réellement fréquentes en
    //    français, elles passent donc avant tout le reste.
    for (const auto& pair : m_replacements) {
        const std::u32string from = utils::utf8ToU32(pair.first);
        const std::u32string to = utils::utf8ToU32(pair.second);
        if (from.empty()) continue;
        size_t position = lowered.find(from);
        while (position != std::u32string::npos) {
            std::u32string candidate = lowered;
            candidate.replace(position, from.size(), to);
            propose(candidate);
            position = lowered.find(from, position + 1);
        }
    }

    // 2. Une seule opération d'édition : suppression, transposition,
    //    remplacement, insertion. Au-delà, les propositions deviennent
    //    fantaisistes et ralentissent tout.
    const size_t n = lowered.size();
    for (size_t i = 0; i < n && out.size() < max; ++i) {         // suppression
        std::u32string candidate = lowered;
        candidate.erase(i, 1);
        propose(candidate);
    }
    for (size_t i = 0; i + 1 < n && out.size() < max; ++i) {     // transposition
        std::u32string candidate = lowered;
        std::swap(candidate[i], candidate[i + 1]);
        propose(candidate);
    }
    for (size_t i = 0; i < n && out.size() < max; ++i) {         // remplacement
        for (const char32_t* letter = kAlphabet; *letter && out.size() < max; ++letter) {
            if (*letter == lowered[i]) continue;
            std::u32string candidate = lowered;
            candidate[i] = *letter;
            propose(candidate);
        }
    }
    for (size_t i = 0; i <= n && out.size() < max; ++i) {        // insertion
        for (const char32_t* letter = kAlphabet; *letter && out.size() < max; ++letter) {
            std::u32string candidate = lowered;
            candidate.insert(i, 1, *letter);
            propose(candidate);
        }
    }
    return out;
}

} // namespace core
