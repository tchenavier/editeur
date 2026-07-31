// CompareApi.cpp
// Implémentation de compare.withFile. Voir CompareApi.h.

#include "CompareApi.h"

#include "Diff.h"
#include "EditorApi.h" // blockTypeName
#include "MarkdownIO.h"
#include "PathUtf8.h"
#include "Utf8.h"

#include <stdexcept>

namespace bridge {

namespace {

// Un côté d'une ligne : le bloc tel qu'il apparaît dans sa version, avec
// les plages de mots à mettre en évidence. Les positions sont en points
// de code, comme partout ailleurs.
json sideJson(const core::TextBlock& block, const std::vector<core::WordSpan>& marks) {
    json spans = json::array();
    for (const core::WordSpan& span : marks) {
        // Couple compact [début, longueur] : sur un manuscrit entier, des
        // objets nommés tripleraient la taille de la réponse.
        spans.push_back(json::array({span.start, span.length}));
    }
    return {{"type", blockTypeName(block.type())},
            {"text", utils::u32ToUtf8(block.text())},
            {"marks", spans}};
}

} // namespace

CompareApi::CompareApi(core::Document& document) : m_document(document) {}

bool CompareApi::registerOn(Bridge& bridge) {
    return bridge.registerCommand("compare.withFile",
                                  [this](const json& p) { return withFile(p); });
}

json CompareApi::withFile(const json& payload) {
    const std::string path = requireString(payload, "path");
    if (path.empty()) throw std::runtime_error("Aucun fichier à comparer.");

    core::Document other;
    std::string error;
    if (!io::loadFromFile(other, utils::pathFromUtf8(path), &error)) {
        throw std::runtime_error(error);
    }

    const std::vector<core::TextBlock>& left = other.blocks();
    const std::vector<core::TextBlock>& right = m_document.blocks();

    // 1. Apparier les blocs. On compare des EMPREINTES de blocs : un
    //    paragraphe déplacé, ajouté ou supprimé est repéré comme tel sans
    //    faire passer ses voisins pour modifiés.
    std::vector<uint64_t> leftHashes, rightHashes;
    leftHashes.reserve(left.size());
    rightHashes.reserve(right.size());
    for (const core::TextBlock& block : left) leftHashes.push_back(core::blockHash(block.text()));
    for (const core::TextBlock& block : right) rightHashes.push_back(core::blockHash(block.text()));

    const std::vector<core::DiffOp> ops = core::diffSequences(leftHashes, rightHashes);

    // 2. Mettre en lignes. Une suppression suivie d'une insertion décrit
    //    en réalité des paragraphes RÉÉCRITS : on les apparie deux à deux
    //    et on compare leurs mots, plutôt que d'afficher le même passage
    //    deux fois, une fois en rouge et une fois en vert.
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
                pushRow("equal", sideJson(left[op.oldStart + k], {}),
                        sideJson(right[op.newStart + k], {}));
            }
            continue;
        }

        if (op.kind == core::DiffKind::Deleted) {
            // Une insertion juste après ? Alors ce sont des remaniements.
            const bool pairs = (i + 1 < ops.size() &&
                                ops[i + 1].kind == core::DiffKind::Inserted);
            const size_t paired = pairs ? std::min(op.oldCount, ops[i + 1].newCount) : 0;

            for (size_t k = 0; k < paired; ++k) {
                const core::TextBlock& a = left[op.oldStart + k];
                const core::TextBlock& b = right[ops[i + 1].newStart + k];
                const core::WordChanges words = core::diffText(a.text(), b.text());
                wordsRemoved += words.removed.size();
                wordsAdded += words.added.size();
                ++changed;
                pushRow("changed", sideJson(a, words.removed), sideJson(b, words.added));
            }
            for (size_t k = paired; k < op.oldCount; ++k) {
                ++removed;
                pushRow("removed", sideJson(left[op.oldStart + k], {}), json());
            }
            if (pairs) {
                const core::DiffOp& next = ops[i + 1];
                for (size_t k = paired; k < next.newCount; ++k) {
                    ++added;
                    pushRow("added", json(), sideJson(right[next.newStart + k], {}));
                }
                ++i; // l'insertion vient d'être traitée avec la suppression
            }
            continue;
        }

        // Insertion seule.
        for (size_t k = 0; k < op.newCount; ++k) {
            ++added;
            pushRow("added", json(), sideJson(right[op.newStart + k], {}));
        }
    }

    const bool identical = (changed == 0 && added == 0 && removed == 0);
    return {{"path", utils::pathToUtf8(utils::pathFromUtf8(path))},
            {"title", other.metadata().title},
            {"identical", identical},
            {"truncated", truncated},
            {"stats", {{"equal", equal},
                       {"changed", changed},
                       {"added", added},
                       {"removed", removed},
                       {"wordsAdded", wordsAdded},
                       {"wordsRemoved", wordsRemoved}}},
            {"rows", rows}};
}

} // namespace bridge
