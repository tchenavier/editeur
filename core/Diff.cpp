// Diff.cpp
// Alignement et comparaison de deux versions d'un texte. Voir Diff.h.

#include "Diff.h"

#include "Speller.h" // isWordChar : une seule définition de « qu'est-ce qu'un mot »

#include <algorithm>
#include <unordered_map>

namespace core {

namespace {

// Au-delà de ce nombre de cases, on renonce à la plus longue
// sous-séquence commune et on déclare le segment remplacé. Le calcul est
// quadratique : sans borne, comparer deux textes entièrement différents
// ferait enfler la mémoire sans produire un résultat plus lisible.
constexpr size_t kMaxCells = 4000000;

// Normalisation d'un caractère pour la comparaison. Voir Diff.h pour ce
// qui est volontairement laissé tel quel (casse, accents).
char32_t normalize(char32_t c) {
    switch (c) {
        case U' ': // espace insécable
        case U' ': // espace insécable étroite
        case U'\t':
            return U' ';
        case U'’': // apostrophe courbe
            return U'\'';
        default:
            return c;
    }
}

// --- Découpage en phrases : les quelques tests de caractères -----------

bool isTerminator(char32_t c) {
    return c == U'.' || c == U'!' || c == U'?' || c == U'…';
}

// Signes qui appartiennent encore à la phrase qui se termine : la
// fermeture vient APRÈS le point dans « … dit-il. » » et « (…). ».
bool isClosingMark(char32_t c) {
    return c == U'»' || c == U'"' || c == U'”' || c == U'’' || c == U'\'' ||
           c == U')' || c == U']';
}

// Majuscule, au sens utile ici : latin de base et lettres accentuées
// françaises. Sert à reconnaître un début de phrase, jamais à transformer
// du texte — inutile d'aller chercher une table Unicode complète.
bool isUpperLetter(char32_t c) {
    if (c >= U'A' && c <= U'Z') return true;
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return true; // À..Þ sauf ×
    return c == 0x0152 /* Œ */ || c == 0x0178 /* Ÿ */;
}

// Ce par quoi une phrase peut commencer. Une MINUSCULE n'en fait pas
// partie : après un point, elle signale presque toujours une abréviation
// que la liste ci-dessous n'a pas prévue — mieux vaut alors ne pas couper.
bool canStartSentence(char32_t c) {
    if (isUpperLetter(c)) return true;
    if (c >= U'0' && c <= U'9') return true;
    return c == U'«' || c == U'"' || c == U'“' || c == U'—' || c == U'–' ||
           c == U'-' || c == U'(' || c == U'[' || c == U'…';
}

// Abréviations après lesquelles un point suivi d'une majuscule n'ouvre pas
// une phrase : ce sont celles qui précèdent un nom propre ou un nombre.
// « etc. » n'y figure pas volontairement — suivi d'une majuscule, il
// termine bel et bien la phrase.
bool isAbbreviation(const std::u32string& text, size_t dotIndex) {
    // Le mot qui précède le point, en minuscules et sans accents utiles
    // ici (les abréviations visées n'en portent pas, sauf « éd. » et
    // « réf. » traitées telles quelles).
    size_t start = dotIndex;
    while (start > 0 && isWordChar(text[start - 1])) --start;
    if (start == dotIndex) return false;

    std::u32string word = text.substr(start, dotIndex - start);
    for (char32_t& c : word) {
        if (c >= U'A' && c <= U'Z') c = static_cast<char32_t>(c - U'A' + U'a');
    }

    // Initiale isolée : « J. R. R. Tolkien ».
    if (word.size() == 1 && isUpperLetter(text[start])) return true;

    static const char32_t* const kAbbreviations[] = {
        U"m", U"mm", U"mme", U"mmes", U"mlle", U"mlles", U"me", U"mgr",
        U"dr", U"drs", U"pr", U"st", U"ste", U"sts", U"stes",
        U"cf", U"env", U"art", U"chap", U"vol", U"fig", U"no", U"nos",
        U"av", U"apr", U"éd", U"réf", U"tél", U"p", U"pp", U"ex",
        U"ibid", U"op", U"al", U"jc",
    };
    for (const char32_t* candidate : kAbbreviations) {
        if (word == candidate) return true;
    }
    return false;
}

uint64_t hashRange(const std::u32string& text, size_t start, size_t end,
                   bool collapseSpaces) {
    uint64_t h = 14695981039346656037ull; // FNV-1a
    bool previousWasSpace = false;
    for (size_t i = start; i < end && i < text.size(); ++i) {
        const char32_t c = normalize(text[i]);
        if (collapseSpaces) {
            const bool isSpace = (c == U' ' || c == U'\n' || c == U'\r');
            if (isSpace) {
                if (previousWasSpace) continue; // suites d'espaces : une seule
                previousWasSpace = true;
            } else {
                previousWasSpace = false;
            }
        }
        h ^= static_cast<uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

// --- Plus longue sous-séquence commune, bornée -------------------------
// Utilisée seulement sur les segments résiduels, ceux où aucun élément
// unique ne sert d'ancre.
void lcsDiff(const std::vector<uint64_t>& a, size_t a0, size_t a1,
             const std::vector<uint64_t>& b, size_t b0, size_t b1,
             std::vector<DiffOp>& out) {
    const size_t n = a1 - a0;
    const size_t m = b1 - b0;
    if (n * m > kMaxCells) {
        // Trop gros : on ne cherche plus l'alignement minimal, on déclare
        // le segment remplacé. C'est de toute façon ce qu'un lecteur
        // comprendrait d'un texte entièrement réécrit.
        if (n) out.push_back({DiffKind::Deleted, a0, n, b0, 0});
        if (m) out.push_back({DiffKind::Inserted, a0 + n, 0, b0, m});
        return;
    }

    // Table des longueurs (n+1) x (m+1), en entiers 32 bits.
    std::vector<uint32_t> table((n + 1) * (m + 1), 0);
    auto at = [m](size_t i, size_t j) { return i * (m + 1) + j; };
    for (size_t i = n; i-- > 0;) {
        for (size_t j = m; j-- > 0;) {
            table[at(i, j)] = (a[a0 + i] == b[b0 + j])
                                  ? table[at(i + 1, j + 1)] + 1
                                  : std::max(table[at(i + 1, j)], table[at(i, j + 1)]);
        }
    }

    size_t i = 0, j = 0;
    while (i < n && j < m) {
        if (a[a0 + i] == b[b0 + j]) {
            out.push_back({DiffKind::Equal, a0 + i, 1, b0 + j, 1});
            ++i; ++j;
        } else if (table[at(i + 1, j)] >= table[at(i, j + 1)]) {
            out.push_back({DiffKind::Deleted, a0 + i, 1, b0 + j, 0});
            ++i;
        } else {
            out.push_back({DiffKind::Inserted, a0 + i, 0, b0 + j, 1});
            ++j;
        }
    }
    if (i < n) out.push_back({DiffKind::Deleted, a0 + i, n - i, b0 + j, 0});
    if (j < m) out.push_back({DiffKind::Inserted, a0 + i, 0, b0 + j, m - j});
}

// --- Ancres : éléments présents UNE SEULE FOIS de chaque côté ----------
// Un tel élément ne peut correspondre qu'à un seul autre : c'est un point
// d'appui certain, qui découpe le problème en morceaux indépendants.
std::vector<std::pair<size_t, size_t>> uniqueAnchors(
    const std::vector<uint64_t>& a, size_t a0, size_t a1,
    const std::vector<uint64_t>& b, size_t b0, size_t b1) {

    std::unordered_map<uint64_t, size_t> countA, countB, firstA, firstB;
    for (size_t i = a0; i < a1; ++i) {
        if (++countA[a[i]] == 1) firstA[a[i]] = i;
    }
    for (size_t j = b0; j < b1; ++j) {
        if (++countB[b[j]] == 1) firstB[b[j]] = j;
    }

    std::vector<std::pair<size_t, size_t>> pairs;
    for (const auto& entry : countA) {
        if (entry.second != 1) continue;
        const auto itB = countB.find(entry.first);
        if (itB == countB.end() || itB->second != 1) continue;
        pairs.emplace_back(firstA[entry.first], firstB[entry.first]);
    }
    std::sort(pairs.begin(), pairs.end());

    // On ne garde que la plus longue suite croissante côté b : deux ancres
    // qui se croisent décriraient un déplacement, que l'on préfère rendre
    // comme une suppression suivie d'une insertion.
    std::vector<size_t> tails;      // fins des suites, par longueur
    std::vector<size_t> previous(pairs.size(), SIZE_MAX);
    std::vector<size_t> indexOf;    // index dans 'pairs' de chaque fin
    for (size_t k = 0; k < pairs.size(); ++k) {
        const size_t value = pairs[k].second;
        const auto position = std::lower_bound(tails.begin(), tails.end(), value);
        const size_t slot = static_cast<size_t>(position - tails.begin());
        if (slot > 0) previous[k] = indexOf[slot - 1];
        if (position == tails.end()) {
            tails.push_back(value);
            indexOf.push_back(k);
        } else {
            *position = value;
            indexOf[slot] = k;
        }
    }

    std::vector<std::pair<size_t, size_t>> chain;
    if (!indexOf.empty()) {
        for (size_t k = indexOf.back(); k != SIZE_MAX; k = previous[k]) {
            chain.push_back(pairs[k]);
        }
        std::reverse(chain.begin(), chain.end());
    }
    return chain;
}

void diffRange(const std::vector<uint64_t>& a, size_t a0, size_t a1,
               const std::vector<uint64_t>& b, size_t b0, size_t b1,
               std::vector<DiffOp>& out) {
    // 1. Début commun. Une correction est presque toujours locale : ce
    //    retrait suffit souvent à ramener le problème à quelques éléments.
    size_t prefix = 0;
    while (a0 + prefix < a1 && b0 + prefix < b1 && a[a0 + prefix] == b[b0 + prefix]) {
        ++prefix;
    }
    if (prefix) {
        out.push_back({DiffKind::Equal, a0, prefix, b0, prefix});
        a0 += prefix;
        b0 += prefix;
    }

    // 2. Fin commune (émise après le traitement du milieu).
    size_t suffix = 0;
    while (a1 - suffix > a0 && b1 - suffix > b0 &&
           a[a1 - suffix - 1] == b[b1 - suffix - 1]) {
        ++suffix;
    }
    const size_t aEnd = a1 - suffix;
    const size_t bEnd = b1 - suffix;

    if (a0 == aEnd && b0 == bEnd) {
        // Rien au milieu.
    } else if (a0 == aEnd) {
        out.push_back({DiffKind::Inserted, a0, 0, b0, bEnd - b0});
    } else if (b0 == bEnd) {
        out.push_back({DiffKind::Deleted, a0, aEnd - a0, b0, 0});
    } else {
        const auto anchors = uniqueAnchors(a, a0, aEnd, b, b0, bEnd);
        if (anchors.empty()) {
            lcsDiff(a, a0, aEnd, b, b0, bEnd, out);
        } else {
            size_t ai = a0, bi = b0;
            for (const auto& anchor : anchors) {
                diffRange(a, ai, anchor.first, b, bi, anchor.second, out);
                out.push_back({DiffKind::Equal, anchor.first, 1, anchor.second, 1});
                ai = anchor.first + 1;
                bi = anchor.second + 1;
            }
            diffRange(a, ai, aEnd, b, bi, bEnd, out);
        }
    }

    if (suffix) {
        out.push_back({DiffKind::Equal, aEnd, suffix, bEnd, suffix});
    }
}

// Fusionne les opérations consécutives de même nature : le résultat est
// plus court à parcourir, et surtout plus lisible à l'affichage.
std::vector<DiffOp> mergeOps(const std::vector<DiffOp>& ops) {
    std::vector<DiffOp> merged;
    for (const DiffOp& op : ops) {
        if (op.oldCount == 0 && op.newCount == 0) continue;
        if (!merged.empty() && merged.back().kind == op.kind) {
            DiffOp& last = merged.back();
            if (last.oldStart + last.oldCount == op.oldStart &&
                last.newStart + last.newCount == op.newStart) {
                last.oldCount += op.oldCount;
                last.newCount += op.newCount;
                continue;
            }
        }
        merged.push_back(op);
    }
    return merged;
}

} // namespace

// ---------------------------------------------------------------------
// Interface publique
// ---------------------------------------------------------------------

bool isSpaceChar(char32_t c) {
    // Les insécables sont désignées par leur point de code plutôt que par
    // le caractère lui-même : à l'œil nu, rien ne les distingue d'une
    // espace ordinaire dans le code source.
    return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' ||
           c == 0x00A0 /* insécable */ || c == 0x202F /* insécable étroite */;
}

std::vector<WordSpan> tokenizeWords(const std::u32string& text) {
    std::vector<WordSpan> words;
    size_t i = 0;
    while (i < text.size()) {
        if (!isWordChar(text[i])) { ++i; continue; }
        const size_t start = i;
        while (i < text.size() && isWordChar(text[i])) ++i;
        words.push_back({start, i - start});
    }
    return words;
}

std::vector<WordSpan> splitSentences(const std::u32string& text) {
    std::vector<WordSpan> sentences;
    const size_t n = text.size();

    // Ferme la phrase en cours sur [start, end), espaces du bord retirés.
    auto emit = [&](size_t start, size_t end) {
        while (start < end && isSpaceChar(text[start])) ++start;
        while (end > start && isSpaceChar(text[end - 1])) --end;
        if (start < end) sentences.push_back({start, end - start});
    };

    size_t start = 0;
    size_t i = 0;
    while (i < n) {
        if (!isTerminator(text[i])) { ++i; continue; }

        // Un point entre deux chiffres appartient au nombre (« 3.14 ») :
        // il ne peut pas terminer une phrase.
        if (text[i] == U'.' && i > 0 && i + 1 < n &&
            text[i - 1] >= U'0' && text[i - 1] <= U'9' &&
            text[i + 1] >= U'0' && text[i + 1] <= U'9') {
            ++i;
            continue;
        }

        const size_t dot = i;
        size_t end = i;
        while (end < n && isTerminator(text[end])) ++end; // « ?! », « ... »

        // Fermetures : elles appartiennent encore à la phrase qui
        // s'achève — « ." », « .) », et la variante française où une
        // espace précède le guillemet fermant : « Au feu ! »
        for (;;) {
            if (end < n && isClosingMark(text[end])) { ++end; continue; }
            size_t after = end;
            while (after < n && isSpaceChar(text[after])) ++after;
            if (after < n && (text[after] == U'»' || text[after] == U'”')) {
                end = after + 1;
                continue;
            }
            break;
        }

        if (end >= n) break; // fin du texte : la phrase se ferme plus bas

        if (!isSpaceChar(text[end])) { i = end; continue; }

        size_t next = end;
        while (next < n && isSpaceChar(text[next])) ++next;
        if (next >= n) break;

        if (canStartSentence(text[next]) &&
            !(text[dot] == U'.' && isAbbreviation(text, dot))) {
            emit(start, end);
            start = next;
            i = next;
            continue;
        }
        i = end;
    }

    emit(start, n);
    return sentences;
}

uint64_t wordHash(const std::u32string& text, const WordSpan& span) {
    return hashRange(text, span.start, span.start + span.length, /*collapseSpaces=*/false);
}

uint64_t blockHash(const std::u32string& text) {
    return hashRange(text, 0, text.size(), /*collapseSpaces=*/true);
}

std::vector<DiffOp> diffSequences(const std::vector<uint64_t>& oldItems,
                                  const std::vector<uint64_t>& newItems) {
    std::vector<DiffOp> ops;
    diffRange(oldItems, 0, oldItems.size(), newItems, 0, newItems.size(), ops);
    return mergeOps(ops);
}

WordChanges diffText(const std::u32string& oldText, const std::u32string& newText) {
    WordChanges changes;
    const std::vector<WordSpan> oldWords = tokenizeWords(oldText);
    const std::vector<WordSpan> newWords = tokenizeWords(newText);

    std::vector<uint64_t> oldHashes, newHashes;
    oldHashes.reserve(oldWords.size());
    newHashes.reserve(newWords.size());
    for (const WordSpan& span : oldWords) oldHashes.push_back(wordHash(oldText, span));
    for (const WordSpan& span : newWords) newHashes.push_back(wordHash(newText, span));

    // Regroupe des mots CONSÉCUTIFS en une seule plage : surligner mot à
    // mot une phrase entièrement réécrite donnerait un affichage haché.
    auto append = [](std::vector<WordSpan>& target, const std::vector<WordSpan>& words,
                     size_t start, size_t count) {
        if (count == 0 || start >= words.size()) return;
        const size_t last = std::min(start + count, words.size()) - 1;
        const size_t from = words[start].start;
        const size_t to = words[last].start + words[last].length;
        if (!target.empty() && target.back().start + target.back().length >= from) {
            target.back().length = to - target.back().start;
        } else {
            target.push_back({from, to - from});
        }
    };

    for (const DiffOp& op : diffSequences(oldHashes, newHashes)) {
        if (op.kind == DiffKind::Deleted) {
            append(changes.removed, oldWords, op.oldStart, op.oldCount);
        } else if (op.kind == DiffKind::Inserted) {
            append(changes.added, newWords, op.newStart, op.newCount);
        }
    }
    return changes;
}

} // namespace core
