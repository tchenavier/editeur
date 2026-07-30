// FileApi.cpp
// Implémentation des commandes file.*. Voir FileApi.h.

#include "FileApi.h"

#include "MarkdownIO.h"
#include "NativeDialog.h"

#include "PathUtf8.h"
#include "Uuid.h"

#include <filesystem>
#include <stdexcept>

namespace bridge {

// requireString / optBool viennent de Bridge.h : la validation des
// payloads est commune à tous les modules *Api.

namespace {

// Scelle l'identité du projet au premier enregistrement (Phase 5).
//
// POURQUOI ICI : c'est le seul endroit où le manuscrit devient un
// fichier. Attribuer l'identifiant plus tôt (à l'ouverture de
// l'application) en créerait un à chaque démarrage, y compris pour des
// documents jamais enregistrés ; plus tard, on écrirait un fichier sans
// identité, qu'une synchronisation ultérieure ne saurait pas rattacher.
void ensureProjectId(core::Document& document) {
    if (document.metadata().projectId.empty()) {
        document.metadata().projectId = utils::newUuid();
    }
}

} // namespace

FileApi::FileApi(core::Document& document, core::UndoManager& undo)
    : m_document(document), m_undo(undo) {}

bool FileApi::registerOn(Bridge& bridge) {
    bool ok = true;
    ok &= bridge.registerCommand("file.save",
        [this](const json& p) { return save(p); });
    ok &= bridge.registerCommand("file.load",
        [this](const json& p) { return load(p); });
    ok &= bridge.registerCommand("file.serialize",
        [this](const json& p) { return serializeCmd(p); });
    ok &= bridge.registerCommand("file.getMetadata",
        [this](const json& p) { return getMetadata(p); });
    ok &= bridge.registerCommand("file.setMetadata",
        [this](const json& p) { return setMetadata(p); });
    ok &= bridge.registerCommand("file.chooseOpen",
        [this](const json& p) { return chooseOpen(p); });
    ok &= bridge.registerCommand("file.chooseSave",
        [this](const json& p) { return chooseSave(p); });
    ok &= bridge.registerCommand("file.chooseFolder",
        [this](const json& p) { return chooseFolder(p); });
    ok &= bridge.registerCommand("file.backup",
        [this](const json& p) { return backup(p); });
    ok &= bridge.registerCommand("file.setState",
        [this](const json& p) { return setState(p); });
    return ok;
}

json FileApi::save(const json& payload) {
    const std::string path = requireString(payload, "path");
    const bool force = optBool(payload, "force");
    const auto fsPath = utils::pathFromUtf8(path);

    // Modification externe : le fichier a-t-il changé depuis qu'on l'a lu ?
    // Si oui et qu'on ne force pas, on rend la main au JS sans écraser.
    if (!force) {
        const auto it = m_fileTokens.find(path);
        if (it != m_fileTokens.end() && std::filesystem::exists(fsPath) &&
            utils::fileModToken(fsPath) != it->second) {
            return {{"conflict", true}};
        }
    }

    ensureProjectId(m_document);

    std::string error;
    if (!io::saveToFile(m_document, fsPath, &error)) {
        // Le Bridge transformera l'exception en { ok: false, error } :
        // le JS affichera le message tel quel à l'utilisateur.
        throw std::runtime_error(error);
    }
    m_fileTokens[path] = utils::fileModToken(fsPath); // nouvelle référence
    return {{"saved", true}};
}

json FileApi::load(const json& payload) {
    const std::string path = requireString(payload, "path");
    std::string error;
    const auto fsPath = utils::pathFromUtf8(path);
    if (!io::loadFromFile(m_document, fsPath, &error)) {
        throw std::runtime_error(error);
    }
    // L'historique portait sur l'ancien document : le garder permettrait
    // d'"annuler" vers un contenu qui n'existe plus.
    m_undo.clear();
    m_fileTokens[path] = utils::fileModToken(fsPath); // référence pour la détection
    return {{"loaded", true}};
}

json FileApi::serializeCmd(const json&) {
    return {{"markdown", io::serialize(m_document)}};
}

json FileApi::getMetadata(const json&) {
    const auto& meta = m_document.metadata();
    // 'project' est en LECTURE SEULE : il est renvoyé pour information,
    // mais setMetadata ne l'accepte pas. L'identité d'un projet est
    // attribuée à l'enregistrement, pas saisie dans un formulaire — une
    // identité modifiable à la main ne serait plus une identité.
    return {{"title", meta.title},
            {"author", meta.author},
            {"cover", meta.coverPath},
            {"summary", meta.summary},
            {"project", meta.projectId}};
}

json FileApi::setMetadata(const json& payload) {
    auto& meta = m_document.metadata();
    // Seuls les champs présents sont modifiés : le JS peut n'envoyer que
    // ce que l'utilisateur a changé dans le panneau de métadonnées.
    auto assign = [&payload](const char* field, std::string& target) {
        if (!payload.contains(field)) return;
        if (!payload[field].is_string()) {
            throw std::runtime_error(std::string("Champ invalide (chaine attendue) : ") + field);
        }
        target = payload[field].get<std::string>();
    };
    assign("title", meta.title);
    assign("author", meta.author);
    assign("cover", meta.coverPath);
    assign("summary", meta.summary);
    return getMetadata(payload);
}

namespace {

// Traduit le nom de format envoyé par l'interface. Un nom inconnu retombe
// sur le manuscrit : un dialogue au mauvais filtre reste utilisable, alors
// qu'une erreur empêcherait l'utilisateur d'ouvrir son fichier.
FileKind fileKindFromName(const std::string& name) {
    if (name == "epub")  return FileKind::Epub;
    if (name == "pdf")   return FileKind::Pdf;
    if (name == "image") return FileKind::Image;
    return FileKind::Markdown;
}

} // namespace

json FileApi::chooseOpen(const json& payload) {
    // 'kind' et 'title' sont facultatifs : sans eux, on ouvre un manuscrit
    // (comportement d'origine). Avec, la même commande sert à choisir une
    // image de couverture (Phase 4).
    const auto r = openFileDialog(m_ownerWindow,
                                  optString(payload, "title", "Ouvrir un manuscrit"),
                                  fileKindFromName(optString(payload, "kind")));
    if (!r.ok) return {{"cancelled", true}};
    return {{"cancelled", false}, {"path", r.path}};
}

json FileApi::chooseSave(const json& payload) {
    const auto r = saveFileDialog(m_ownerWindow,
                                  optString(payload, "title", "Enregistrer le manuscrit"),
                                  optString(payload, "name", "mon-roman.md"),
                                  fileKindFromName(optString(payload, "kind")));
    if (!r.ok) return {{"cancelled", true}};
    return {{"cancelled", false}, {"path", r.path}};
}

json FileApi::chooseFolder(const json&) {
    const auto r = chooseFolderDialog(m_ownerWindow, "Choisir le dossier des fiches");
    if (!r.ok) return {{"cancelled", true}};
    return {{"cancelled", false}, {"path", r.path}};
}

json FileApi::setState(const json& payload) {
    if (payload.contains("dirty") && payload["dirty"].is_boolean()) {
        m_dirty = payload["dirty"].get<bool>();
    }
    if (payload.contains("path") && payload["path"].is_string()) {
        m_currentPath = payload["path"].get<std::string>();
    }
    return {{"dirty", m_dirty}, {"path", m_currentPath}};
}

bool FileApi::saveCurrent(std::string* errorMessage, bool force, bool* conflict) {
    if (conflict) *conflict = false;
    std::string path = m_currentPath;
    if (path.empty()) {
        // Jamais enregistré : demander où.
        const auto chosen = saveFileDialog(m_ownerWindow, "Enregistrer le manuscrit", "mon-roman.md");
        if (!chosen.ok) {
            if (errorMessage) *errorMessage = "Enregistrement annulé.";
            return false;
        }
        path = chosen.path;
    }
    const auto fsPath = utils::pathFromUtf8(path);

    if (!force) {
        const auto it = m_fileTokens.find(path);
        if (it != m_fileTokens.end() && std::filesystem::exists(fsPath) &&
            utils::fileModToken(fsPath) != it->second) {
            if (conflict) *conflict = true;
            if (errorMessage) *errorMessage = "Le fichier a été modifié en dehors de l'éditeur.";
            return false;
        }
    }
    ensureProjectId(m_document);
    if (!io::saveToFile(m_document, fsPath, errorMessage)) {
        return false;
    }
    m_currentPath = path;
    m_dirty = false;
    m_fileTokens[path] = utils::fileModToken(fsPath);
    return true;
}

json FileApi::backup(const json& payload) {
    const std::string path = requireString(payload, "path");
    // Nombre de sauvegardes à conserver (défaut : 10).
    size_t keep = 10;
    if (payload.contains("keep") && payload["keep"].is_number_unsigned()) {
        keep = payload["keep"].get<size_t>();
    }
    std::string outPath, error;
    if (!io::writeBackup(m_document, utils::pathFromUtf8(path), keep, &outPath, &error)) {
        throw std::runtime_error(error);
    }
    return {{"backup", outPath}};
}

} // namespace bridge
