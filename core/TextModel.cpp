// TextModel.cpp
// Implémentation de core::TextBlock, core::WordBoundary et core::Document.
// Aucune dépendance externe : uniquement la bibliothèque standard.

#include "TextModel.h"

namespace core {

// ---------------------------------------------------------------------
// TextBlock::insertText
// ---------------------------------------------------------------------
void TextBlock::insertText(size_t pos, const std::u32string& text) {
    // Sécurité : on ne fait jamais confiance à 'pos' venant de l'extérieur
    // (ex: position envoyée par le JS côté webview). On la borne plutôt
    // que de risquer un accès hors limites.
    pos = std::min(pos, m_text.size());
    const size_t insertedLength = text.size();
    if (insertedLength == 0) return;

    m_text.insert(pos, text);

    // Décale ou étend les plages existantes.
    for (auto& run : m_runs) {
        if (run.start >= pos) {
            // La plage est entièrement après le point d'insertion : décalage.
            run.start += insertedLength;
        } else if (run.start + run.length > pos) {
            // Le point d'insertion tombe à l'intérieur de la plage :
            // le nouveau texte hérite du style environnant (comportement
            // attendu quand on tape au milieu d'un mot en gras).
            run.length += insertedLength;
        }
        // Sinon (run.end() <= pos) : la plage est avant, rien à faire.
    }

    mergeAdjacentRuns();
}

// ---------------------------------------------------------------------
// TextBlock::removeText
// ---------------------------------------------------------------------
void TextBlock::removeText(size_t pos, size_t count) {
    pos = std::min(pos, m_text.size());
    count = std::min(count, m_text.size() - pos);
    if (count == 0) return;

    const size_t removeEnd = pos + count;
    m_text.erase(pos, count);

    std::vector<FormatRun> updated;
    updated.reserve(m_runs.size());
    for (auto& run : m_runs) {
        size_t runStart = run.start;
        size_t runEnd = run.end();

        if (runEnd <= pos) {
            // Entièrement avant la suppression : inchangée.
            updated.push_back(run);
            continue;
        }
        if (runStart >= removeEnd) {
            // Entièrement après : décalée vers la gauche.
            run.start -= count;
            updated.push_back(run);
            continue;
        }
        // La plage chevauche la zone supprimée : on retire l'intersection.
        size_t newStart = std::min(runStart, pos);
        size_t overlapStart = std::max(runStart, pos);
        size_t overlapEnd = std::min(runEnd, removeEnd);
        size_t remainingLength = (runEnd - runStart) - (overlapEnd - overlapStart);

        if (runStart >= pos) {
            // Le début de la plage est dans la zone supprimée : elle
            // commence maintenant au point de suppression.
            newStart = pos;
        }
        if (remainingLength > 0) {
            run.start = newStart;
            run.length = remainingLength;
            updated.push_back(run);
        }
        // Si remainingLength == 0, la plage disparaît entièrement.
    }

    m_runs = std::move(updated);
    mergeAdjacentRuns();
}

// ---------------------------------------------------------------------
// TextBlock::applyStyle
// ---------------------------------------------------------------------
void TextBlock::applyStyle(size_t pos, size_t count, TextStyle style) {
    pos = std::min(pos, m_text.size());
    count = std::min(count, m_text.size() - pos);
    if (count == 0) return;
    const size_t rangeEnd = pos + count;

    // Approche volontairement simple et robuste plutôt qu'optimisée :
    // on reconstruit la liste de plages en "découpant" celles qui
    // chevauchent [pos, rangeEnd), puis on fusionne à la fin. Un roman
    // (quelques centaines de milliers de caractères par chapitre, donc
    // par bloc dans un sens large) reste largement dans les capacités
    // de cette approche en O(n).
    std::vector<FormatRun> result;
    result.reserve(m_runs.size() + 2);

    for (const auto& run : m_runs) {
        if (run.end() <= pos || run.start >= rangeEnd) {
            result.push_back(run); // aucun chevauchement
            continue;
        }
        // Partie avant la zone stylée (si présente). On conserve sa taille.
        if (run.start < pos) {
            result.push_back({run.start, pos - run.start, run.style, run.sizePercent});
        }
        // Partie chevauchante : on ajoute le nouveau style au style existant,
        // en gardant la taille de la plage d'origine.
        size_t overlapStart = std::max(run.start, pos);
        size_t overlapEnd = std::min(run.end(), rangeEnd);
        result.push_back({overlapStart, overlapEnd - overlapStart,
                          run.style | style, run.sizePercent});
        // Partie après la zone stylée (si présente).
        if (run.end() > rangeEnd) {
            result.push_back({rangeEnd, run.end() - rangeEnd, run.style, run.sizePercent});
        }
    }

    // Zone de [pos, rangeEnd) non couverte par une plage existante :
    // on la retrouve en soustrayant les plages déjà traitées ci-dessus.
    // Méthode simple : on marque ce qui est déjà couvert, puis on comble
    // les trous avec le style demandé seul.
    std::vector<bool> covered(count, false);
    for (const auto& run : result) {
        size_t s = std::max(run.start, pos);
        size_t e = std::min(run.end(), rangeEnd);
        for (size_t i = s; i < e; ++i) covered[i - pos] = true;
    }
    size_t i = 0;
    while (i < count) {
        if (!covered[i]) {
            size_t j = i;
            while (j < count && !covered[j]) ++j;
            result.push_back({pos + i, j - i, style});
            i = j;
        } else {
            ++i;
        }
    }

    std::sort(result.begin(), result.end(),
              [](const FormatRun& a, const FormatRun& b) { return a.start < b.start; });
    m_runs = std::move(result);
    mergeAdjacentRuns();
}

// ---------------------------------------------------------------------
// TextBlock::removeStyle
// ---------------------------------------------------------------------
void TextBlock::removeStyle(size_t pos, size_t count, TextStyle style) {
    pos = std::min(pos, m_text.size());
    count = std::min(count, m_text.size() - pos);
    if (count == 0) return;
    const size_t rangeEnd = pos + count;
    const TextStyle inverse = ~style;

    std::vector<FormatRun> result;
    result.reserve(m_runs.size() + 2);
    for (const auto& run : m_runs) {
        if (run.end() <= pos || run.start >= rangeEnd) {
            result.push_back(run);
            continue;
        }
        if (run.start < pos) {
            result.push_back({run.start, pos - run.start, run.style, run.sizePercent});
        }
        size_t overlapStart = std::max(run.start, pos);
        size_t overlapEnd = std::min(run.end(), rangeEnd);
        result.push_back({overlapStart, overlapEnd - overlapStart,
                          run.style & inverse, run.sizePercent});
        if (run.end() > rangeEnd) {
            result.push_back({rangeEnd, run.end() - rangeEnd, run.style, run.sizePercent});
        }
    }

    std::sort(result.begin(), result.end(),
              [](const FormatRun& a, const FormatRun& b) { return a.start < b.start; });
    m_runs = std::move(result);
    mergeAdjacentRuns();
}

// ---------------------------------------------------------------------
// TextBlock::setFontSize
// ---------------------------------------------------------------------
// Même structure que applyStyle (on découpe les plages chevauchantes puis
// on comble les trous), à une différence près : la taille REMPLACE celle
// de la zone visée au lieu de s'y cumuler, tout en préservant le masque de
// style existant. Une valeur de 100 rétablit la taille normale ; les
// plages ainsi redevenues « par défaut » sont éliminées par la fusion.
void TextBlock::setFontSize(size_t pos, size_t count, uint16_t sizePercent) {
    pos = std::min(pos, m_text.size());
    count = std::min(count, m_text.size() - pos);
    if (count == 0) return;
    const size_t rangeEnd = pos + count;

    std::vector<FormatRun> result;
    result.reserve(m_runs.size() + 2);
    for (const auto& run : m_runs) {
        if (run.end() <= pos || run.start >= rangeEnd) {
            result.push_back(run); // aucun chevauchement
            continue;
        }
        // Partie avant la zone visée : taille inchangée.
        if (run.start < pos) {
            result.push_back({run.start, pos - run.start, run.style, run.sizePercent});
        }
        // Partie chevauchante : nouvelle taille, style conservé.
        size_t overlapStart = std::max(run.start, pos);
        size_t overlapEnd = std::min(run.end(), rangeEnd);
        result.push_back({overlapStart, overlapEnd - overlapStart, run.style, sizePercent});
        // Partie après la zone visée : taille inchangée.
        if (run.end() > rangeEnd) {
            result.push_back({rangeEnd, run.end() - rangeEnd, run.style, run.sizePercent});
        }
    }

    // Zone non couverte par une plage existante : caractères sans style,
    // auxquels on donne la nouvelle taille (style None).
    std::vector<bool> covered(count, false);
    for (const auto& run : result) {
        size_t s = std::max(run.start, pos);
        size_t e = std::min(run.end(), rangeEnd);
        for (size_t k = s; k < e; ++k) covered[k - pos] = true;
    }
    size_t i = 0;
    while (i < count) {
        if (!covered[i]) {
            size_t j = i;
            while (j < count && !covered[j]) ++j;
            result.push_back({pos + i, j - i, TextStyle::None, sizePercent});
            i = j;
        } else {
            ++i;
        }
    }

    std::sort(result.begin(), result.end(),
              [](const FormatRun& a, const FormatRun& b) { return a.start < b.start; });
    m_runs = std::move(result);
    mergeAdjacentRuns();
}

// ---------------------------------------------------------------------
// TextBlock::styleAt
// ---------------------------------------------------------------------
TextStyle TextBlock::styleAt(size_t pos) const {
    for (const auto& run : m_runs) {
        if (pos >= run.start && pos < run.end()) return run.style;
    }
    return TextStyle::None;
}

// ---------------------------------------------------------------------
// TextBlock::sizeAt
// ---------------------------------------------------------------------
uint16_t TextBlock::sizeAt(size_t pos) const {
    for (const auto& run : m_runs) {
        if (pos >= run.start && pos < run.end()) return run.sizePercent;
    }
    return 100; // hors plage : taille normale
}

// ---------------------------------------------------------------------
// TextBlock::mergeAdjacentRuns
// ---------------------------------------------------------------------
void TextBlock::mergeAdjacentRuns() {
    if (m_runs.empty()) return;
    std::sort(m_runs.begin(), m_runs.end(),
              [](const FormatRun& a, const FormatRun& b) { return a.start < b.start; });

    std::vector<FormatRun> merged;
    merged.reserve(m_runs.size());
    merged.push_back(m_runs.front());

    for (size_t i = 1; i < m_runs.size(); ++i) {
        FormatRun& last = merged.back();
        const FormatRun& current = m_runs[i];
        // Fusion seulement si MÊME format (style ET taille) et contigus.
        if (current.sameFormat(last) && current.start == last.end()) {
            last.length += current.length;
        } else if (current.length > 0) {
            merged.push_back(current);
        }
    }

    // Retire les plages de longueur nulle (sécurité) ET celles redevenues
    // « par défaut » (aucun style, taille normale) : elles n'apportent
    // aucune information et sont équivalentes à un simple trou non stylé.
    // Les éliminer garde m_runs compact même après de nombreux cycles
    // « agrandir puis remettre à 100 % ».
    merged.erase(std::remove_if(merged.begin(), merged.end(),
                                 [](const FormatRun& r) {
                                     return r.length == 0 || r.isDefault();
                                 }),
                 merged.end());

    m_runs = std::move(merged);
}

// ---------------------------------------------------------------------
// WordBoundary
// ---------------------------------------------------------------------
namespace WordBoundary {

bool isWordSeparator(char32_t ch) {
    // Liste volontairement explicite plutôt qu'un isspace/ispunct
    // générique : on garde le contrôle total sur les cas particuliers
    // du français (ex: l'apostrophe typographique ' ne doit PAS couper
    // un mot dans "aujourd'hui", contrairement à l'apostrophe simple
    // dans certains imports, à trancher au cas par cas si besoin).
    switch (ch) {
        case U' ': case U'\t': case U'\n': case U'\r':
        case U'.': case U',': case U';': case U':': case U'!': case U'?':
        case U'(': case U')': case U'[': case U']': case U'{': case U'}':
        case U'"': case U'«': case U'»':
            return true;
        default:
            return false;
    }
}

Range wordAt(const std::u32string& text, size_t pos) {
    if (pos >= text.size() || isWordSeparator(text[pos])) {
        return {pos, pos}; // plage vide : pas de mot sous le curseur
    }
    size_t start = pos;
    while (start > 0 && !isWordSeparator(text[start - 1])) --start;
    size_t end = pos;
    while (end < text.size() && !isWordSeparator(text[end])) ++end;
    return {start, end};
}

} // namespace WordBoundary

// ---------------------------------------------------------------------
// Document::setBlockType
// ---------------------------------------------------------------------
void Document::setBlockType(size_t index, BlockType type) {
    if (index >= m_blocks.size()) return; // hors limites : rien à faire
    m_blocks[index].setType(type);
}

// ---------------------------------------------------------------------
// Document::splitBlock
// ---------------------------------------------------------------------
void Document::splitBlock(size_t index, size_t pos) {
    if (index >= m_blocks.size()) return;
    TextBlock& block = m_blocks[index];
    pos = std::min(pos, block.length());

    // Construit le bloc "queue" (la partie après la coupure) via l'API
    // publique de TextBlock : on insère le texte, puis on ré-applique
    // les styles décalés. Aucune manipulation directe des plages, donc
    // aucun risque de casser leurs invariants.
    const std::u32string tail = block.text().substr(pos);
    TextBlock newBlock(BlockType::Paragraph);
    newBlock.insertText(0, tail);
    for (const auto& run : block.formatRuns()) {
        const size_t s = std::max(run.start, pos);
        const size_t e = std::min(run.end(), block.length());
        if (s >= e) continue;
        if (run.style != TextStyle::None) {
            newBlock.applyStyle(s - pos, e - s, run.style);
        }
        if (run.sizePercent != 100) {
            newBlock.setFontSize(s - pos, e - s, run.sizePercent);
        }
    }

    // Tronque le bloc d'origine, puis insère la queue juste après.
    block.removeText(pos, block.length() - pos);
    m_blocks.insert(m_blocks.begin() + static_cast<std::ptrdiff_t>(index) + 1,
                    std::move(newBlock));
}

// ---------------------------------------------------------------------
// Document::mergeWithPrevious
// ---------------------------------------------------------------------
void Document::mergeWithPrevious(size_t index) {
    if (index == 0 || index >= m_blocks.size()) return;
    TextBlock& prev = m_blocks[index - 1];
    const TextBlock& cur = m_blocks[index];

    // On capture les plages de 'cur' AVANT d'insérer son texte : après
    // l'insertion, ce sont les plages de 'prev' qui comptent.
    const std::vector<FormatRun> curRuns(cur.formatRuns().begin(),
                                         cur.formatRuns().end());
    const size_t offset = prev.length();
    prev.insertText(offset, cur.text());
    for (const auto& run : curRuns) {
        if (run.style != TextStyle::None) {
            prev.applyStyle(offset + run.start, run.length, run.style);
        }
        if (run.sizePercent != 100) {
            prev.setFontSize(offset + run.start, run.length, run.sizePercent);
        }
    }
    m_blocks.erase(m_blocks.begin() + static_cast<std::ptrdiff_t>(index));
}

// ---------------------------------------------------------------------
// Document::moveBlocks
// ---------------------------------------------------------------------
bool Document::moveBlocks(size_t start, size_t count, size_t before) {
    const size_t n = m_blocks.size();
    if (count == 0 || start >= n) return false;
    count = std::min(count, n - start);
    if (before > n) before = n;

    // Sans effet si la destination tombe dans la plage déplacée (ou à ses
    // bords immédiats) : le chapitre reviendrait exactement à sa place.
    if (before >= start && before <= start + count) return false;

    using diff = std::ptrdiff_t;
    std::vector<TextBlock> moved(
        std::make_move_iterator(m_blocks.begin() + static_cast<diff>(start)),
        std::make_move_iterator(m_blocks.begin() + static_cast<diff>(start + count)));
    m_blocks.erase(m_blocks.begin() + static_cast<diff>(start),
                   m_blocks.begin() + static_cast<diff>(start + count));

    // La suppression décale vers la gauche tout ce qui suivait la plage :
    // on ajuste l'indice d'insertion en conséquence.
    size_t insertAt = (before > start) ? before - count : before;
    m_blocks.insert(m_blocks.begin() + static_cast<diff>(insertAt),
                    std::make_move_iterator(moved.begin()),
                    std::make_move_iterator(moved.end()));
    return true;
}

// ---------------------------------------------------------------------
// Document::wordCount
// ---------------------------------------------------------------------
size_t Document::wordCount() const {
    size_t count = 0;
    for (const auto& block : m_blocks) {
        bool inWord = false;
        for (char32_t ch : block.text()) {
            if (WordBoundary::isWordSeparator(ch)) {
                inWord = false;
            } else if (!inWord) {
                inWord = true;
                ++count;
            }
        }
    }
    return count;
}

} // namespace core
