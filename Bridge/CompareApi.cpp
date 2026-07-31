// CompareApi.cpp
// Implémentation des commandes compare.*. Voir CompareApi.h.

#include "CompareApi.h"

#include "Diff.h"
#include "EditorApi.h" // blockTypeName, serializeBlock
#include "MarkdownIO.h"
#include "PathUtf8.h"
#include "Reflow.h"
#include "Utf8.h"

#include <algorithm>
#include <stdexcept>

namespace bridge {

namespace {

// L'unité que l'on compare : un paragraphe, ou une phrase quand les
// paragraphes ne se correspondent plus (voir CompareApi.h).
struct Unit {
    std::u32string text;
    core::BlockType type = core::BlockType::Paragraph;
};

std::vector<Unit> blockUnits(const std::vector<core::TextBlock>& blocks) {
    std::vector<Unit> units;
    units.reserve(blocks.size());
    for (const core::TextBlock& block : blocks) {
        units.push_back({block.text(), block.type()});
    }
    return units;
}

// Une unité par phrase. Le type est celui du bloc qui la porte : une
// phrase venue d'un titre reste affichée comme un titre.
std::vector<Unit> sentenceUnits(const std::vector<core::TextBlock>& blocks) {
    std::vector<Unit> units;
    for (const core::TextRef& ref : core::documentSentences(blocks)) {
        units.push_back({core::refText(blocks, ref), blocks[ref.blockIndex].type()});
    }
    return units;
}

std::vector<uint64_t> hashesOf(const std::vector<Unit>& units) {
    std::vector<uint64_t> hashes;
    hashes.reserve(units.size());
    for (const Unit& unit : units) hashes.push_back(core::blockHash(unit.text));
    return hashes;
}

size_t charsOf(const std::vector<Unit>& units) {
    size_t total = 0;
    for (const Unit& unit : units) total += unit.text.size();
    return total;
}

// Part du texte (des deux côtés) que l'alignement retrouve à l'identique.
// Vaut 1 pour deux versions identiques, et tombe vers 0 quand les unités
// comparées ne se correspondent plus du tout — ce qui est précisément le
// symptôme d'une structure de paragraphes perdue.
double matchScore(const std::vector<Unit>& left, const std::vector<Unit>& right,
                  const std::vector<core::DiffOp>& ops) {
    const size_t total = charsOf(left) + charsOf(right);
    if (total == 0) return 1.0;

    size_t equal = 0;
    for (const core::DiffOp& op : ops) {
        if (op.kind != core::DiffKind::Equal) continue;
        for (size_t k = 0; k < op.oldCount && k < op.newCount; ++k) {
            equal += left[op.oldStart + k].text.size() + right[op.newStart + k].text.size();
        }
    }
    return static_cast<double>(equal) / static_cast<double>(total);
}

// Un côté d'une ligne : l'unité telle qu'elle apparaît dans sa version,
// avec les plages de mots à mettre en évidence. Les positions sont en
// points de code, comme partout ailleurs.
json sideJson(const Unit& unit, const std::vector<core::WordSpan>& marks) {
    json spans = json::array();
    for (const core::WordSpan& span : marks) {
        // Couple compact [début, longueur] : sur un manuscrit entier, des
        // objets nommés tripleraient la taille de la réponse.
        spans.push_back(json::array({span.start, span.length}));
    }
    return {{"type", blockTypeName(unit.type)},
            {"text", utils::u32ToUtf8(unit.text)},
            {"marks", spans}};
}

// Nom du côté qui a perdu ses paragraphes, ou "none". Le critère est une
// mesure, pas une intuition : « la moitié des phrases dans un seul bloc,
// alors qu'en face elles sont réparties » (voir core::flatness).
const char* flattenedSide(const std::vector<core::TextBlock>& left,
                          const std::vector<core::TextBlock>& right) {
    const double flatLeft = core::flatness(left);
    const double flatRight = core::flatness(right);
    if (flatRight >= CompareApi::kFlatThreshold && flatLeft < flatRight / 2) return "right";
    if (flatLeft >= CompareApi::kFlatThreshold && flatRight < flatLeft / 2) return "left";
    return "none";
}

core::Document loadOther(const std::string& path) {
    if (path.empty()) throw std::runtime_error("Aucun fichier à comparer.");
    core::Document other;
    std::string error;
    if (!io::loadFromFile(other, utils::pathFromUtf8(path), &error)) {
        throw std::runtime_error(error);
    }
    return other;
}

} // namespace

CompareApi::CompareApi(core::Document& document, core::UndoManager& undo)
    : m_document(document), m_undo(undo) {}

bool CompareApi::registerOn(Bridge& bridge) {
    bool ok = bridge.registerCommand("compare.withFile",
                                     [this](const json& p) { return withFile(p); });
    ok &= bridge.registerCommand("compare.reparagraph",
                                 [this](const json& p) { return reparagraph(p); });
    return ok;
}

json CompareApi::withFile(const json& payload) {
    const core::Document other = loadOther(requireString(payload, "path"));
    const std::vector<core::TextBlock>& left = other.blocks();
    const std::vector<core::TextBlock>& right = m_document.blocks();

    // 1. Apparier les paragraphes. On compare des EMPREINTES : un
    //    paragraphe déplacé, ajouté ou supprimé est repéré comme tel sans
    //    faire passer ses voisins pour modifiés.
    std::vector<Unit> leftUnits = blockUnits(left);
    std::vector<Unit> rightUnits = blockUnits(right);
    std::vector<core::DiffOp> ops = core::diffSequences(hashesOf(leftUnits),
                                                        hashesOf(rightUnits));
    double score = matchScore(leftUnits, rightUnits, ops);
    const char* unitName = "block";

    // 2. MESURER plutôt que supposer. Si l'appariement des paragraphes ne
    //    retrouve presque rien, ce n'est pas forcément que le texte a
    //    changé : c'est souvent qu'un des deux côtés a perdu son découpage
    //    (un correcteur qui rend tout d'un bloc). On refait alors le
    //    calcul sur les PHRASES, et on garde le meilleur des deux — sur
    //    deux versions vraiment différentes, aucun ne l'emporte et
    //    l'affichage par paragraphes reste le plus lisible.
    if (score < kStructureThreshold) {
        std::vector<Unit> leftSentences = sentenceUnits(left);
        std::vector<Unit> rightSentences = sentenceUnits(right);
        const std::vector<core::DiffOp> sentenceOps =
            core::diffSequences(hashesOf(leftSentences), hashesOf(rightSentences));
        const double sentenceScore = matchScore(leftSentences, rightSentences, sentenceOps);
        if (sentenceScore > score) {
            leftUnits = std::move(leftSentences);
            rightUnits = std::move(rightSentences);
            ops = sentenceOps;
            score = sentenceScore;
            unitName = "sentence";
        }
    }

    // 3. Mettre en lignes. Une suppression suivie d'une insertion décrit
    //    en réalité des passages RÉÉCRITS : on les apparie deux à deux et
    //    on compare leurs mots, plutôt que d'afficher le même passage deux
    //    fois, une fois en rouge et une fois en vert.
    json rows = json::array();
    size_t changed = 0, added = 0, removed = 0, equal = 0;
    size_t wordsAdded = 0, wordsRemoved = 0;
    bool truncated = false;

    auto pushRow = [&](const char* kind, const json& leftSide, const json& rightSide) {
        if (rows.size() >= kMaxRows) { truncated = true; return; }
        rows.push_back({{"kind", kind}, {"left", leftSide}, {"right", rightSide}});
    };

    for (size_t i = 0; i < ops.size(); ++i) {
        const core::DiffOp& op = ops[i];

        if (op.kind == core::DiffKind::Equal) {
            for (size_t k = 0; k < op.oldCount; ++k) {
                ++equal;
                pushRow("equal", sideJson(leftUnits[op.oldStart + k], {}),
                        sideJson(rightUnits[op.newStart + k], {}));
            }
            continue;
        }

        if (op.kind == core::DiffKind::Deleted) {
            // Une insertion juste après ? Alors ce sont des remaniements.
            const bool pairs = (i + 1 < ops.size() &&
                                ops[i + 1].kind == core::DiffKind::Inserted);
            const size_t paired = pairs ? std::min(op.oldCount, ops[i + 1].newCount) : 0;

            for (size_t k = 0; k < paired; ++k) {
                const Unit& a = leftUnits[op.oldStart + k];
                const Unit& b = rightUnits[ops[i + 1].newStart + k];
                const core::WordChanges words = core::diffText(a.text, b.text);
                wordsRemoved += words.removed.size();
                wordsAdded += words.added.size();
                ++changed;
                pushRow("changed", sideJson(a, words.removed), sideJson(b, words.added));
            }
            for (size_t k = paired; k < op.oldCount; ++k) {
                ++removed;
                pushRow("removed", sideJson(leftUnits[op.oldStart + k], {}), json());
            }
            if (pairs) {
                const core::DiffOp& next = ops[i + 1];
                for (size_t k = paired; k < next.newCount; ++k) {
                    ++added;
                    pushRow("added", json(), sideJson(rightUnits[next.newStart + k], {}));
                }
                ++i; // l'insertion vient d'être traitée avec la suppression
            }
            continue;
        }

        // Insertion seule.
        for (size_t k = 0; k < op.newCount; ++k) {
            ++added;
            pushRow("added", json(), sideJson(rightUnits[op.newStart + k], {}));
        }
    }

    const bool identical = (changed == 0 && added == 0 && removed == 0);
    return {{"path", utils::pathToUtf8(utils::pathFromUtf8(requireString(payload, "path")))},
            {"title", other.metadata().title},
            {"identical", identical},
            {"truncated", truncated},
            {"structure", {{"unit", unitName},
                           {"flattened", flattenedSide(left, right)},
                           {"match", score}}},
            {"stats", {{"equal", equal},
                       {"changed", changed},
                       {"added", added},
                       {"removed", removed},
                       {"wordsAdded", wordsAdded},
                       {"wordsRemoved", wordsRemoved}}},
            {"rows", rows}};
}

json CompareApi::reparagraph(const json& payload) {
    const core::Document other = loadOther(requireString(payload, "path"));

    // Garde-fou : refuser de « redécouper » un manuscrit qui a déjà ses
    // paragraphes. L'opération y détruirait la structure de l'auteur pour
    // lui substituer celle d'un autre fichier — ce n'est jamais ce qu'on
    // veut, et l'interface ne propose le bouton que dans le bon cas.
    if (core::flatness(m_document.blocks()) < kFlatThreshold) {
        throw std::runtime_error(
            "Le manuscrit ouvert a déjà ses paragraphes : le redécoupage ne "
            "s'applique qu'à une version rendue d'un seul tenant.");
    }

    std::vector<core::TextBlock> before = m_document.blocks();
    std::vector<core::TextBlock> after = core::reflowLike(before, other.blocks());

    const bool changed = (after.size() != before.size());
    if (changed) {
        m_document.blocks() = std::move(after);
        m_undo.recordStructural(std::move(before), m_document.blocks());
    }

    json blocks = json::array();
    for (const core::TextBlock& block : m_document.blocks()) {
        blocks.push_back(serializeBlock(block));
    }
    return {{"changed", changed},
            {"count", m_document.blocks().size()},
            {"blocks", blocks}};
}

} // namespace bridge
