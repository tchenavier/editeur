// Search.cpp
// Implémentation de la recherche / remplacement. Voir Search.h.

#include "Search.h"

#include <vector>

namespace core {

// Repli de casse volontairement limité à l'ASCII et au Latin-1 (À-Þ),
// ce qui couvre les majuscules accentuées du français (É, È, À, Ç, Ô...).
// Un repli Unicode complet nécessiterait une table (ICU) : hors périmètre
// de la Phase 1, à étendre plus tard si le besoin apparaît.
char32_t foldLower(char32_t c) {
    if (c >= U'A' && c <= U'Z') return c + 0x20;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20; // À-Þ sauf × (0xD7)
    return c;
}

namespace {

bool charEq(char32_t a, char32_t b, bool caseSensitive) {
    if (a == b) return true;
    if (caseSensitive) return false;
    return foldLower(a) == foldLower(b);
}

// Vrai si 'query' apparaît exactement à la position 'pos' dans 'text'.
bool matchAt(const std::u32string& text, size_t pos,
             const std::u32string& query, const SearchOptions& opts) {
    if (pos + query.size() > text.size()) return false;
    for (size_t k = 0; k < query.size(); ++k) {
        if (!charEq(text[pos + k], query[k], opts.caseSensitive)) return false;
    }
    if (opts.wholeWord) {
        const bool okBefore = (pos == 0) || WordBoundary::isWordSeparator(text[pos - 1]);
        const size_t after = pos + query.size();
        const bool okAfter = (after == text.size()) || WordBoundary::isWordSeparator(text[after]);
        if (!okBefore || !okAfter) return false;
    }
    return true;
}

// Positions (points de code) de toutes les occurrences NON CHEVAUCHANTES
// dans un bloc donné (parcours de gauche à droite).
std::vector<size_t> matchesInBlock(const std::u32string& text,
                                   const std::u32string& query,
                                   const SearchOptions& opts) {
    std::vector<size_t> out;
    if (query.empty() || query.size() > text.size()) return out;
    size_t i = 0;
    while (i + query.size() <= text.size()) {
        if (matchAt(text, i, query, opts)) {
            out.push_back(i);
            i += query.size(); // non chevauchant
        } else {
            ++i;
        }
    }
    return out;
}

struct GlobalMatch { size_t block; size_t start; };

// Toutes les occurrences de la portée, en ordre de document.
std::vector<GlobalMatch> allMatches(const Document& doc, const std::u32string& query,
                                    size_t rangeStart, size_t rangeEnd,
                                    const SearchOptions& opts) {
    std::vector<GlobalMatch> out;
    const size_t n = doc.blocks().size();
    rangeEnd = (rangeEnd > n) ? n : rangeEnd;
    for (size_t b = rangeStart; b < rangeEnd; ++b) {
        for (size_t pos : matchesInBlock(doc.blocks()[b].text(), query, opts)) {
            out.push_back({b, pos});
        }
    }
    return out;
}

} // namespace

SearchHit findNext(const Document& doc, const std::u32string& query,
                   size_t rangeStart, size_t rangeEnd,
                   size_t fromBlock, size_t fromPos,
                   const SearchOptions& opts, bool backward, bool wrap) {
    SearchHit hit;
    if (query.empty()) return hit;

    const std::vector<GlobalMatch> matches =
        allMatches(doc, query, rangeStart, rangeEnd, opts);
    hit.total = matches.size();
    if (matches.empty()) return hit;

    // Recherche du bon élément par rapport à la position de départ.
    // On compare (block, start) lexicographiquement.
    auto isAfterStart = [&](const GlobalMatch& m) {
        return m.block > fromBlock || (m.block == fromBlock && m.start >= fromPos);
    };
    auto isBeforeStart = [&](const GlobalMatch& m) {
        return m.block < fromBlock || (m.block == fromBlock && m.start < fromPos);
    };

    size_t chosen = matches.size(); // sentinelle « aucun »
    if (!backward) {
        for (size_t i = 0; i < matches.size(); ++i) {
            if (isAfterStart(matches[i])) { chosen = i; break; }
        }
        if (chosen == matches.size() && wrap) chosen = 0; // enroulement
    } else {
        for (size_t i = matches.size(); i-- > 0;) {
            if (isBeforeStart(matches[i])) { chosen = i; break; }
        }
        if (chosen == matches.size() && wrap) chosen = matches.size() - 1;
    }
    if (chosen == matches.size()) return hit; // rien sans enroulement

    hit.found = true;
    hit.block = matches[chosen].block;
    hit.start = matches[chosen].start;
    hit.end = matches[chosen].start + query.size();
    hit.index = chosen + 1; // 1-based
    return hit;
}

size_t countMatches(const Document& doc, const std::u32string& query,
                    size_t rangeStart, size_t rangeEnd,
                    const SearchOptions& opts) {
    if (query.empty()) return 0;
    return allMatches(doc, query, rangeStart, rangeEnd, opts).size();
}

bool contains(const std::u32string& text, const std::u32string& query,
              const SearchOptions& opts) {
    if (query.empty()) return false;
    return !matchesInBlock(text, query, opts).empty();
}

std::u32string replaceInString(const std::u32string& text,
                               const std::u32string& query,
                               const std::u32string& replacement,
                               const SearchOptions& opts, size_t* count) {
    if (count) *count = 0;
    if (query.empty()) return text;
    const std::vector<size_t> positions = matchesInBlock(text, query, opts);
    if (positions.empty()) return text;

    // Reconstruction de gauche à droite : on recopie ce qui précède chaque
    // occurrence, puis le remplacement.
    std::u32string out;
    out.reserve(text.size());
    size_t last = 0;
    for (size_t pos : positions) {
        out.append(text, last, pos - last);
        out.append(replacement);
        last = pos + query.size();
        if (count) ++*count;
    }
    out.append(text, last, text.size() - last);
    return out;
}

size_t replaceAll(Document& doc, const std::u32string& query,
                  const std::u32string& replacement,
                  size_t rangeStart, size_t rangeEnd,
                  const SearchOptions& opts) {
    if (query.empty()) return 0;
    const size_t n = doc.blocks().size();
    rangeEnd = (rangeEnd > n) ? n : rangeEnd;

    size_t count = 0;
    for (size_t b = rangeStart; b < rangeEnd; ++b) {
        TextBlock& block = doc.blocks()[b];
        const std::vector<size_t> positions =
            matchesInBlock(block.text(), query, opts);
        // Remplacement de DROITE À GAUCHE : les positions à gauche restent
        // valides même si la longueur du texte change.
        for (size_t i = positions.size(); i-- > 0;) {
            const size_t pos = positions[i];
            block.removeText(pos, query.size());
            if (!replacement.empty()) block.insertText(pos, replacement);
            ++count;
        }
    }
    return count;
}

} // namespace core
