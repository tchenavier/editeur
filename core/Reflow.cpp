// Reflow.cpp
// Redécoupage d'un texte aplati d'après une version de référence.
// Voir Reflow.h pour le pourquoi.

#include "Reflow.h"

#include "Diff.h"

#include <algorithm>

namespace core {

namespace {

// Une position dans le document : bloc et rang dans ce bloc.
struct Cut {
    size_t block = 0;
    size_t pos = 0;
};

// Ajoute à 'target' la tranche [from, to) de 'source', STYLES COMPRIS.
//
// Les plages de formatage sont recopiées plage par plage plutôt que
// caractère par caractère : sur un roman, la différence n'est pas un
// détail (une opération par plage au lieu d'une par lettre).
void appendSlice(TextBlock& target, const TextBlock& source, size_t from, size_t to) {
    from = std::min(from, source.length());
    to = std::min(to, source.length());
    if (from >= to) return;

    const size_t base = target.length();
    target.insertText(base, source.text().substr(from, to - from));

    for (const FormatRun& run : source.formatRuns()) {
        const size_t start = std::max(run.start, from);
        const size_t end = std::min(run.end(), to);
        if (start >= end) continue;
        const size_t pos = base + (start - from);
        if (run.style != TextStyle::None) target.applyStyle(pos, end - start, run.style);
        if (run.sizePercent != 100) target.setFontSize(pos, end - start, run.sizePercent);
    }
}

// Ajoute à 'target' tout le texte compris entre deux positions. Quand la
// plage traverse plusieurs blocs d'origine, la jointure se fait par une
// espace : c'est la seule chose qu'on invente, et il faut bien qu'un mot
// ne se colle pas au suivant.
void appendRange(TextBlock& target, const std::vector<TextBlock>& blocks,
                 const Cut& from, const Cut& to) {
    for (size_t b = from.block; b <= to.block && b < blocks.size(); ++b) {
        const size_t start = (b == from.block) ? from.pos : 0;
        const size_t end = (b == to.block) ? to.pos : blocks[b].length();
        if (b != from.block && target.length() > 0) {
            target.insertText(target.length(), U" ");
        }
        appendSlice(target, blocks[b], start, end);
    }
}

// Retire les espaces de tête et de queue (le formatage suit le texte).
void trim(TextBlock& block) {
    size_t end = block.length();
    while (end > 0 && isSpaceChar(block.text()[end - 1])) --end;
    if (end < block.length()) block.removeText(end, block.length() - end);

    size_t start = 0;
    while (start < block.length() && isSpaceChar(block.text()[start])) ++start;
    if (start > 0) block.removeText(0, start);
}

// Où couper entre deux mots que le redécoupage sépare ?
//
// La ponctuation qui SUIT le premier mot lui appartient (le point final),
// et celle qui PRÉCÈDE le second appartient au second (tiret de dialogue,
// guillemet ouvrant). La frontière est donc la première espace du passage
// entre les deux — et le blanc lui-même disparaît, puisqu'il devient un
// changement de paragraphe.
void cutBetween(const std::vector<TextBlock>& blocks,
                const TextRef& before, const TextRef& after,
                Cut& endOfFirst, Cut& startOfSecond) {
    if (before.blockIndex != after.blockIndex) {
        endOfFirst = {before.blockIndex, blocks[before.blockIndex].length()};
        startOfSecond = {after.blockIndex, 0};
        return;
    }
    const std::u32string& text = blocks[before.blockIndex].text();
    const size_t limit = std::min(after.start, text.size());
    size_t i = std::min(before.start + before.length, limit);
    while (i < limit && !isSpaceChar(text[i])) ++i;
    endOfFirst = {before.blockIndex, i};
    while (i < limit && isSpaceChar(text[i])) ++i;
    startOfSecond = {after.blockIndex, i};
}

std::vector<uint64_t> wordHashes(const std::vector<TextBlock>& blocks,
                                 const std::vector<TextRef>& words) {
    std::vector<uint64_t> hashes;
    hashes.reserve(words.size());
    for (const TextRef& ref : words) {
        hashes.push_back(wordHash(blocks[ref.blockIndex].text(),
                                  WordSpan{ref.start, ref.length}));
    }
    return hashes;
}

} // namespace

// ---------------------------------------------------------------------
// Repérage
// ---------------------------------------------------------------------

std::vector<TextRef> documentSentences(const std::vector<TextBlock>& blocks) {
    std::vector<TextRef> sentences;
    for (size_t b = 0; b < blocks.size(); ++b) {
        for (const WordSpan& span : splitSentences(blocks[b].text())) {
            sentences.push_back({b, span.start, span.length});
        }
    }
    return sentences;
}

std::vector<TextRef> documentWords(const std::vector<TextBlock>& blocks) {
    std::vector<TextRef> words;
    for (size_t b = 0; b < blocks.size(); ++b) {
        for (const WordSpan& span : tokenizeWords(blocks[b].text())) {
            words.push_back({b, span.start, span.length});
        }
    }
    return words;
}

std::u32string refText(const std::vector<TextBlock>& blocks, const TextRef& ref) {
    if (ref.blockIndex >= blocks.size()) return {};
    return blocks[ref.blockIndex].text().substr(ref.start, ref.length);
}

// ---------------------------------------------------------------------
// Mesure
// ---------------------------------------------------------------------

double flatness(const std::vector<TextBlock>& blocks) {
    const std::vector<TextRef> sentences = documentSentences(blocks);
    if (sentences.empty()) return 0.0;

    size_t best = 0, current = 0;
    size_t previousBlock = sentences.front().blockIndex;
    for (const TextRef& ref : sentences) {
        if (ref.blockIndex != previousBlock) {
            previousBlock = ref.blockIndex;
            current = 0;
        }
        ++current;
        best = std::max(best, current);
    }

    // En dessous de trois phrases dans un même bloc, la mesure ne dit
    // rien : un texte de deux phrases est « tout entier dans un bloc »
    // sans avoir rien perdu.
    if (best < 3) return 0.0;
    return static_cast<double>(best) / static_cast<double>(sentences.size());
}

// ---------------------------------------------------------------------
// Redécoupage
// ---------------------------------------------------------------------

std::vector<TextBlock> reflowLike(const std::vector<TextBlock>& flat,
                                  const std::vector<TextBlock>& reference) {
    const std::vector<TextRef> flatWords = documentWords(flat);
    const std::vector<TextRef> refWords = documentWords(reference);
    if (flatWords.empty() || refWords.empty()) return flat;

    // 1. Aligner les MOTS des deux versions. Les paragraphes ne servent
    //    à rien ici — c'est précisément ce qui manque d'un côté — mais la
    //    suite des mots, elle, a survécu : deux versions d'un même texte
    //    ne diffèrent que par les corrections. L'alignement est donc
    //    quasi immédiat (le préfixe commun avale presque tout).
    const std::vector<DiffOp> ops = diffSequences(wordHashes(reference, refWords),
                                                  wordHashes(flat, flatWords));

    // 2. Attribuer à chaque mot du texte aplati le bloc de référence d'où
    //    il vient.
    const size_t kAucun = static_cast<size_t>(-1);
    std::vector<size_t> owner(flatWords.size(), kAucun);
    size_t ancres = 0;
    for (const DiffOp& op : ops) {
        if (op.kind != DiffKind::Equal) continue;
        for (size_t k = 0; k < op.oldCount && k < op.newCount; ++k) {
            owner[op.newStart + k] = refWords[op.oldStart + k].blockIndex;
            ++ancres;
        }
    }

    // GARDE-FOU. Les mots retrouvés à l'identique sont ce sur quoi tout
    // repose : les autres se contentent de suivre leur voisin de gauche.
    // Une version simplement corrigée en garde plus de neuf sur dix. Si
    // moins de la moitié se raccroche, ce n'est pas la même œuvre — ou
    // elle a trop changé — et le découpage produit serait deviné, pas
    // retrouvé. On rend alors le texte tel quel.
    if (ancres * 2 < flatWords.size()) return flat;

    // Un mot sans vis-à-vis — corrigé, ou ajouté — reste avec celui qui
    // le précède : c'est dans ce paragraphe qu'il a été écrit.
    size_t last = kAucun;
    for (size_t k = 0; k < owner.size(); ++k) {
        if (owner[k] == kAucun) owner[k] = last;
        else last = owner[k];
    }
    size_t first = kAucun;
    for (const size_t value : owner) {
        if (value != kAucun) { first = value; break; }
    }
    if (first == kAucun) return flat; // aucun mot commun : on ne touche à rien
    for (size_t k = 0; k < owner.size() && owner[k] == kAucun; ++k) owner[k] = first;

    // 3. Regrouper les mots consécutifs de même origine : un groupe, un
    //    paragraphe — du type qu'il avait dans la version de référence,
    //    ce qui rend aussi leur niveau aux titres.
    std::vector<TextBlock> result;
    size_t k = 0;
    Cut from{flatWords.front().blockIndex, 0};
    while (k < flatWords.size()) {
        size_t j = k;
        while (j + 1 < flatWords.size() && owner[j + 1] == owner[k]) ++j;

        Cut to;
        Cut nextFrom;
        if (j + 1 < flatWords.size()) {
            cutBetween(flat, flatWords[j], flatWords[j + 1], to, nextFrom);
        } else {
            const size_t lastBlock = flatWords[j].blockIndex;
            to = {lastBlock, flat[lastBlock].length()};
        }

        TextBlock block(reference[owner[k]].type());
        appendRange(block, flat, from, to);
        trim(block);
        if (block.length() > 0) result.push_back(std::move(block));

        from = nextFrom;
        k = j + 1;
    }
    if (result.empty()) return flat;
    return result;
}

} // namespace core
