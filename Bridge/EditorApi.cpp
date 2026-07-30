// EditorApi.cpp
// Implémentation des commandes editor.*.
// (La conversion UTF-8 <-> UTF-32, historiquement ici, vit désormais
// dans utils/Utf8.h : la couche io/ en a également besoin.)

#include "EditorApi.h"

#include "Search.h"
#include "Utf8.h"

#include <stdexcept>

namespace bridge {

// =====================================================================
// Sérialisation du document vers JSON (pour le rendu côté JS)
// =====================================================================
namespace {

const char* blockTypeName(core::BlockType type) {
    switch (type) {
        case core::BlockType::Heading1: return "h1";
        case core::BlockType::Heading2: return "h2";
        case core::BlockType::Heading3: return "h3";
        case core::BlockType::Heading4: return "h4";
        case core::BlockType::Heading5: return "h5";
        case core::BlockType::Heading6: return "h6";
        default:                        return "p";
    }
}

// Lecture optionnelle (optIndex / optBool) et obligatoire (requireIndex /
// requireString) des champs du payload : voir Bridge.h, ces aides sont
// communes à tous les modules *Api.

core::BlockType blockTypeFromName(const std::string& name) {
    using T = core::BlockType;
    if (name == "p")  return T::Paragraph;
    if (name == "h1") return T::Heading1;
    if (name == "h2") return T::Heading2;
    if (name == "h3") return T::Heading3;
    if (name == "h4") return T::Heading4;
    if (name == "h5") return T::Heading5;
    if (name == "h6") return T::Heading6;
    throw std::runtime_error("Type de bloc inconnu : " + name);
}

// Convertit un bitmask de styles en tableau de noms JS.
json styleToNames(core::TextStyle style) {
    json names = json::array();
    using S = core::TextStyle;
    if (core::hasStyle(style, S::Bold))          names.push_back("bold");
    if (core::hasStyle(style, S::Italic))        names.push_back("italic");
    if (core::hasStyle(style, S::Underline))     names.push_back("underline");
    if (core::hasStyle(style, S::Strikethrough)) names.push_back("strikethrough");
    if (core::hasStyle(style, S::Superscript))   names.push_back("superscript");
    if (core::hasStyle(style, S::Subscript))     names.push_back("subscript");
    return names;
}

} // namespace

json serializeBlock(const core::TextBlock& block) {
    json runs = json::array();
    for (const auto& run : block.formatRuns()) {
        runs.push_back({
            {"start", run.start},
            {"length", run.length},
            {"styles", styleToNames(run.style)},
            {"size", run.sizePercent}, // taille en % (100 = normal)
        });
    }
    return {
        {"type", blockTypeName(block.type())},
        {"text", utils::u32ToUtf8(block.text())},
        {"runs", runs},
    };
}

// =====================================================================
// EditorApi
// =====================================================================

EditorApi::EditorApi(core::Document& document, core::UndoManager& undo)
    : m_document(document), m_undo(undo) {}

bool EditorApi::registerOn(Bridge& bridge) {
    // Chaque commande capture 'this' : l'objet EditorApi doit donc
    // vivre aussi longtemps que le Bridge (documenté dans le .h).
    bool ok = true;
    ok &= bridge.registerCommand("editor.getDocument",
        [this](const json& p) { return getDocument(p); });
    ok &= bridge.registerCommand("editor.insertText",
        [this](const json& p) { return insertText(p); });
    ok &= bridge.registerCommand("editor.removeText",
        [this](const json& p) { return removeText(p); });
    ok &= bridge.registerCommand("editor.applyStyle",
        [this](const json& p) { return applyStyle(p); });
    ok &= bridge.registerCommand("editor.removeStyle",
        [this](const json& p) { return removeStyle(p); });
    ok &= bridge.registerCommand("editor.setFontSize",
        [this](const json& p) { return setFontSize(p); });
    ok &= bridge.registerCommand("editor.toggleWordStyle",
        [this](const json& p) { return toggleWordStyle(p); });
    ok &= bridge.registerCommand("editor.wordCount",
        [this](const json& p) { return wordCount(p); });
    ok &= bridge.registerCommand("editor.undo",
        [this](const json& p) { return undoCmd(p); });
    ok &= bridge.registerCommand("editor.redo",
        [this](const json& p) { return redoCmd(p); });
    ok &= bridge.registerCommand("editor.historyState",
        [this](const json& p) { return historyState(p); });
    ok &= bridge.registerCommand("editor.breakUndoGroup",
        [this](const json& p) { return breakUndoGroup(p); });
    ok &= bridge.registerCommand("editor.setBlockType",
        [this](const json& p) { return setBlockType(p); });
    ok &= bridge.registerCommand("editor.splitBlock",
        [this](const json& p) { return splitBlock(p); });
    ok &= bridge.registerCommand("editor.mergeBlock",
        [this](const json& p) { return mergeBlock(p); });
    ok &= bridge.registerCommand("editor.moveBlocks",
        [this](const json& p) { return moveBlocks(p); });
    ok &= bridge.registerCommand("editor.find",
        [this](const json& p) { return find(p); });
    ok &= bridge.registerCommand("editor.replaceRange",
        [this](const json& p) { return replaceRange(p); });
    ok &= bridge.registerCommand("editor.replaceAll",
        [this](const json& p) { return replaceAll(p); });
    return ok;
}

json EditorApi::documentJson() {
    json blocks = json::array();
    for (const auto& block : m_document.blocks()) {
        blocks.push_back(serializeBlock(block));
    }
    return {{"blocks", blocks}};
}

// --- Aides de validation ---------------------------------------------

core::TextBlock& EditorApi::requireBlock(const json& payload) {
    const size_t index = requireIndex(payload, "block");
    auto& blocks = m_document.blocks();
    if (index >= blocks.size()) {
        throw std::runtime_error("Bloc inexistant (index hors limites).");
    }
    return blocks[index];
}

core::TextStyle EditorApi::styleFromName(const std::string& name) {
    using S = core::TextStyle;
    if (name == "bold")          return S::Bold;
    if (name == "italic")        return S::Italic;
    if (name == "underline")     return S::Underline;
    if (name == "strikethrough") return S::Strikethrough;
    if (name == "superscript")   return S::Superscript;
    if (name == "subscript")     return S::Subscript;
    throw std::runtime_error("Style inconnu : " + name);
}

// --- Gestionnaires ----------------------------------------------------

json EditorApi::getDocument(const json&) {
    return documentJson();
}

json EditorApi::insertText(const json& payload) {
    auto& block = requireBlock(payload);
    const size_t blockIndex = requireIndex(payload, "block");
    const std::u32string text = utils::utf8ToU32(requireString(payload, "text"));

    // Même bornage que le modèle : l'historique doit raisonner sur la
    // position RÉELLEMENT utilisée, pas sur celle reçue du JS.
    const size_t pos = std::min(requireIndex(payload, "pos"), block.length());

    if (!text.empty()) {
        core::TextBlock before = block; // instantané pour l'annulation
        block.insertText(pos, text);
        m_undo.recordTyping(blockIndex, pos, text, std::move(before), block);
    }

    // On renvoie le bloc mis à jour : le JS re-rend uniquement ce bloc,
    // pas tout le document (important pour la fluidité sur un roman long).
    return {{"block", serializeBlock(block)}};
}

json EditorApi::removeText(const json& payload) {
    auto& block = requireBlock(payload);
    const size_t blockIndex = requireIndex(payload, "block");

    const size_t pos = std::min(requireIndex(payload, "pos"), block.length());
    const size_t count = std::min(requireIndex(payload, "count"), block.length() - pos);

    if (count > 0) {
        core::TextBlock before = block;
        // Le texte supprimé sert au regroupement (mot ou séparateur ?).
        const std::u32string removed = block.text().substr(pos, count);
        block.removeText(pos, count);
        m_undo.recordDeletion(blockIndex, pos, removed, std::move(before), block);
    }
    return {{"block", serializeBlock(block)}};
}

json EditorApi::applyStyle(const json& payload) {
    auto& block = requireBlock(payload);
    const size_t blockIndex = requireIndex(payload, "block");
    const core::TextStyle style = styleFromName(requireString(payload, "style"));

    const size_t pos = std::min(requireIndex(payload, "pos"), block.length());
    const size_t count = std::min(requireIndex(payload, "count"), block.length() - pos);

    if (count > 0) {
        core::TextBlock before = block;
        block.applyStyle(pos, count, style);
        m_undo.recordOther(blockIndex, std::move(before), block);
    }
    return {{"block", serializeBlock(block)}};
}

json EditorApi::removeStyle(const json& payload) {
    auto& block = requireBlock(payload);
    const size_t blockIndex = requireIndex(payload, "block");
    const core::TextStyle style = styleFromName(requireString(payload, "style"));

    const size_t pos = std::min(requireIndex(payload, "pos"), block.length());
    const size_t count = std::min(requireIndex(payload, "count"), block.length() - pos);

    if (count > 0) {
        core::TextBlock before = block;
        block.removeStyle(pos, count, style);
        m_undo.recordOther(blockIndex, std::move(before), block);
    }
    return {{"block", serializeBlock(block)}};
}

json EditorApi::setFontSize(const json& payload) {
    auto& block = requireBlock(payload);
    const size_t blockIndex = requireIndex(payload, "block");

    const size_t pos = std::min(requireIndex(payload, "pos"), block.length());
    const size_t count = std::min(requireIndex(payload, "count"), block.length() - pos);

    // Taille en pourcentage, bornée à [20, 800] pour rester raisonnable
    // (le JS envoie déjà des valeurs valides ; on se protège malgré tout).
    size_t sizePercent = requireIndex(payload, "size");
    if (sizePercent < 20)  sizePercent = 20;
    if (sizePercent > 800) sizePercent = 800;

    if (count > 0) {
        core::TextBlock before = block;
        block.setFontSize(pos, count, static_cast<uint16_t>(sizePercent));
        m_undo.recordOther(blockIndex, std::move(before), block);
    }
    return {{"block", serializeBlock(block)}};
}

json EditorApi::toggleWordStyle(const json& payload) {
    // Ergonomie "par mot" : clic sur Gras sans sélection -> le mot entier
    // sous le curseur bascule. Le modèle reste par caractère ; c'est ici,
    // et seulement ici, que la notion de mot intervient.
    auto& block = requireBlock(payload);
    const size_t blockIndex = requireIndex(payload, "block");
    const size_t pos = requireIndex(payload, "pos");
    const core::TextStyle style = styleFromName(requireString(payload, "style"));

    const auto range = core::WordBoundary::wordAt(block.text(), pos);
    if (range.start == range.end) {
        // Curseur sur un séparateur : rien à basculer, ce n'est pas une
        // erreur (l'utilisateur a juste cliqué au mauvais endroit).
        return {{"block", serializeBlock(block)}, {"changed", false}};
    }

    // Bascule : si le premier caractère du mot a déjà le style, on le
    // retire du mot entier ; sinon on l'applique au mot entier.
    core::TextBlock before = block;
    const bool alreadyStyled = core::hasStyle(block.styleAt(range.start), style);
    const size_t count = range.end - range.start;
    if (alreadyStyled) {
        block.removeStyle(range.start, count, style);
    } else {
        block.applyStyle(range.start, count, style);
    }
    m_undo.recordOther(blockIndex, std::move(before), block);
    return {{"block", serializeBlock(block)}, {"changed", true}};
}

json EditorApi::wordCount(const json&) {
    return {{"total", m_document.wordCount()}};
}

json EditorApi::undoCmd(const json&) {
    const auto info = m_undo.undo(m_document);
    if (!info) {
        return {{"changed", false}}; // rien à annuler : pas une erreur
    }
    if (info->wholeDocument) {
        // La structure a changé (bloc coupé/fusionné) : on renvoie tout
        // le document, le JS le re-rend en entier.
        json result = documentJson();
        result["changed"] = true;
        result["wholeDocument"] = true;
        return result;
    }
    return {{"changed", true},
            {"wholeDocument", false},
            {"blockIndex", info->blockIndex},
            {"block", serializeBlock(m_document.blocks()[info->blockIndex])}};
}

json EditorApi::redoCmd(const json&) {
    const auto info = m_undo.redo(m_document);
    if (!info) {
        return {{"changed", false}};
    }
    if (info->wholeDocument) {
        json result = documentJson();
        result["changed"] = true;
        result["wholeDocument"] = true;
        return result;
    }
    return {{"changed", true},
            {"wholeDocument", false},
            {"blockIndex", info->blockIndex},
            {"block", serializeBlock(m_document.blocks()[info->blockIndex])}};
}

json EditorApi::historyState(const json&) {
    return {{"canUndo", m_undo.canUndo()}, {"canRedo", m_undo.canRedo()}};
}

json EditorApi::breakUndoGroup(const json&) {
    m_undo.breakGroup();
    return json::object();
}

json EditorApi::setBlockType(const json& payload) {
    auto& block = requireBlock(payload);
    const size_t blockIndex = requireIndex(payload, "block");
    const core::BlockType type = blockTypeFromName(requireString(payload, "type"));

    // Le type change, pas la structure : étape par bloc, un seul re-rendu.
    core::TextBlock before = block;
    block.setType(type);
    m_undo.recordOther(blockIndex, std::move(before), block);
    return {{"block", serializeBlock(block)}, {"blockIndex", blockIndex}};
}

json EditorApi::splitBlock(const json& payload) {
    const size_t blockIndex = requireIndex(payload, "block");
    if (blockIndex >= m_document.blocks().size()) {
        throw std::runtime_error("Bloc inexistant (index hors limites).");
    }
    const size_t pos = std::min(requireIndex(payload, "pos"),
                                m_document.blocks()[blockIndex].length());

    // Instantané complet AVANT modification (les indices vont changer).
    std::vector<core::TextBlock> before = m_document.blocks();
    m_document.splitBlock(blockIndex, pos);
    m_undo.recordStructural(std::move(before), m_document.blocks());

    // Le JS placera le curseur au début du nouveau bloc (blockIndex + 1).
    json result = documentJson();
    result["newBlock"] = blockIndex + 1;
    return result;
}

json EditorApi::mergeBlock(const json& payload) {
    const size_t blockIndex = requireIndex(payload, "block");
    if (blockIndex >= m_document.blocks().size()) {
        throw std::runtime_error("Bloc inexistant (index hors limites).");
    }
    if (blockIndex == 0) {
        // Pas de bloc précédent : rien à fusionner, ce n'est pas une
        // erreur (Retour arrière au tout début du document).
        json result = documentJson();
        result["merged"] = false;
        return result;
    }

    // Position de fusion = longueur du bloc précédent AVANT la fusion :
    // c'est là que le curseur doit se poser (jointure des deux blocs).
    const size_t caretPos = m_document.blocks()[blockIndex - 1].length();

    std::vector<core::TextBlock> before = m_document.blocks();
    m_document.mergeWithPrevious(blockIndex);
    m_undo.recordStructural(std::move(before), m_document.blocks());

    json result = documentJson();
    result["merged"] = true;
    result["caretBlock"] = blockIndex - 1;
    result["caretPos"] = caretPos;
    return result;
}

json EditorApi::moveBlocks(const json& payload) {
    const size_t start = requireIndex(payload, "start");
    const size_t count = requireIndex(payload, "count");
    const size_t before = requireIndex(payload, "before");
    if (start >= m_document.blocks().size()) {
        throw std::runtime_error("Bloc inexistant (index hors limites).");
    }

    std::vector<core::TextBlock> snapshot = m_document.blocks();
    const bool moved = m_document.moveBlocks(start, count, before);
    // On n'enregistre une étape d'annulation que si un déplacement a
    // réellement eu lieu (sinon Ctrl+Z n'aurait rien à annuler).
    if (moved) {
        m_undo.recordStructural(std::move(snapshot), m_document.blocks());
    }

    json result = documentJson();
    result["moved"] = moved;
    return result;
}

json EditorApi::find(const json& payload) {
    const std::u32string query = utils::utf8ToU32(requireString(payload, "query"));
    const size_t n = m_document.blocks().size();

    core::SearchOptions opts;
    opts.caseSensitive = optBool(payload, "caseSensitive", false);
    opts.wholeWord = optBool(payload, "wholeWord", false);

    const core::SearchHit hit = core::findNext(
        m_document, query,
        optIndex(payload, "rangeStart", 0),
        optIndex(payload, "rangeEnd", n),
        optIndex(payload, "fromBlock", 0),
        optIndex(payload, "fromPos", 0),
        opts,
        optBool(payload, "backward", false),
        optBool(payload, "wrap", true));

    return {{"found", hit.found}, {"block", hit.block},
            {"start", hit.start}, {"end", hit.end},
            {"index", hit.index}, {"total", hit.total}};
}

json EditorApi::replaceRange(const json& payload) {
    auto& block = requireBlock(payload);
    const size_t blockIndex = requireIndex(payload, "block");
    const size_t pos = std::min(requireIndex(payload, "start"), block.length());
    const size_t count = std::min(requireIndex(payload, "count"), block.length() - pos);
    const std::u32string text = utils::utf8ToU32(requireString(payload, "text"));

    if (count > 0 || !text.empty()) {
        // Remove + insert regroupés en UNE seule étape d'annulation.
        core::TextBlock before = block;
        if (count > 0) block.removeText(pos, count);
        if (!text.empty()) block.insertText(pos, text);
        m_undo.recordOther(blockIndex, std::move(before), block);
    }
    return {{"block", serializeBlock(block)},
            {"blockIndex", blockIndex},
            {"caretPos", pos + text.size()}};
}

json EditorApi::replaceAll(const json& payload) {
    const std::u32string query = utils::utf8ToU32(requireString(payload, "query"));
    const std::u32string replacement = utils::utf8ToU32(requireString(payload, "replacement"));
    const size_t n = m_document.blocks().size();

    core::SearchOptions opts;
    opts.caseSensitive = optBool(payload, "caseSensitive", false);
    opts.wholeWord = optBool(payload, "wholeWord", false);

    std::vector<core::TextBlock> snapshot = m_document.blocks();
    const size_t count = core::replaceAll(
        m_document, query, replacement,
        optIndex(payload, "rangeStart", 0),
        optIndex(payload, "rangeEnd", n),
        opts);
    // Un seul « Ctrl+Z » annule tous les remplacements (étape structurelle).
    if (count > 0) {
        m_undo.recordStructural(std::move(snapshot), m_document.blocks());
    }

    json result = documentJson();
    result["count"] = count;
    return result;
}

} // namespace bridge
