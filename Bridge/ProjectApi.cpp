// ProjectApi.cpp
// Implémentation des commandes project.*. Voir ProjectApi.h.

#include "ProjectApi.h"

#include "ChangeLog.h"
#include "PathUtf8.h"
#include "ProjectIndex.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace bridge {

namespace {

json changeToJson(const io::Change& change) {
    return {{"kind", io::changeKindName(change.kind)},
            {"path", change.path},
            {"size", change.size}};
}

// Le manuscrit doit être enregistré pour avoir un inventaire : sans
// fichier, il n'y a pas de dossier de projet où le ranger.
std::filesystem::path requireManuscript(const json& payload) {
    const std::string path = requireString(payload, "path");
    if (path.empty()) {
        throw std::runtime_error("Le manuscrit doit être enregistré avant tout suivi.");
    }
    return utils::pathFromUtf8(path);
}

} // namespace

ProjectApi::ProjectApi(core::Document& document) : m_document(document) {}

bool ProjectApi::registerOn(Bridge& bridge) {
    bool ok = true;
    ok &= bridge.registerCommand("project.state",
        [this](const json& p) { return state(p); });
    ok &= bridge.registerCommand("project.snapshot",
        [this](const json& p) { return snapshot(p); });
    ok &= bridge.registerCommand("project.history",
        [this](const json& p) { return history(p); });
    return ok;
}

json ProjectApi::state(const json& payload) {
    const std::filesystem::path manuscript = requireManuscript(payload);
    const std::string projectId = m_document.metadata().projectId;

    io::ProjectIndex stored;
    const bool hasIndex = io::loadIndex(stored, io::indexPathFor(manuscript));

    size_t skipped = 0;
    const io::ProjectIndex current = io::buildIndex(manuscript, projectId, &skipped);
    const std::vector<io::Change> pending = io::diffIndex(stored, current);

    json changes = json::array();
    for (const io::Change& change : pending) changes.push_back(changeToJson(change));

    return {{"projectId", projectId},
            {"tracked", hasIndex},
            {"files", current.files.size()},
            {"skipped", skipped},
            {"pending", changes}};
}

json ProjectApi::snapshot(const json& payload) {
    const std::filesystem::path manuscript = requireManuscript(payload);
    const std::string projectId = m_document.metadata().projectId;

    io::ProjectIndex stored;
    io::loadIndex(stored, io::indexPathFor(manuscript)); // absent = inventaire vide

    size_t skipped = 0;
    const io::ProjectIndex current = io::buildIndex(manuscript, projectId, &skipped);
    const std::vector<io::Change> changes = io::diffIndex(stored, current);

    std::string error;
    if (!changes.empty()) {
        if (!io::appendChanges(io::journalPathFor(manuscript), changes, &error)) {
            throw std::runtime_error(error);
        }
    }
    if (!io::saveIndex(current, io::indexPathFor(manuscript), &error)) {
        throw std::runtime_error(error);
    }
    // Le journal ne doit pas grossir sans fin dans le dossier de l'auteur.
    const size_t dropped =
        io::compactJournal(io::journalPathFor(manuscript), io::kJournalMaxEntries);

    json recorded = json::array();
    for (const io::Change& change : changes) recorded.push_back(changeToJson(change));

    return {{"projectId", projectId},
            {"files", current.files.size()},
            {"skipped", skipped},
            {"recorded", recorded},
            {"compacted", dropped}};
}

json ProjectApi::history(const json& payload) {
    const std::filesystem::path manuscript = requireManuscript(payload);
    const size_t limit = optIndex(payload, "limit", 50);

    std::vector<io::JournalEntry> entries = io::readJournal(io::journalPathFor(manuscript));
    // De la plus récente à la plus ancienne : c'est l'ordre d'affichage
    // attendu, et il évite au JS d'avoir à retourner la liste.
    std::reverse(entries.begin(), entries.end());
    if (entries.size() > limit) entries.resize(limit);

    json list = json::array();
    for (const io::JournalEntry& entry : entries) {
        list.push_back({{"at", entry.timestamp},
                        {"kind", io::changeKindName(entry.kind)},
                        {"path", entry.path},
                        {"size", entry.size}});
    }
    return {{"entries", list}, {"total", list.size()}};
}

} // namespace bridge
