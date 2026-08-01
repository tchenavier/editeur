// ProjectIndex.cpp
// Construction, sérialisation et comparaison des inventaires de projet.
// Voir ProjectIndex.h.

#include "ProjectIndex.h"

#include "PathUtf8.h"
#include "Sha256.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace io {

namespace {

namespace fs = std::filesystem;

// Suffixes des dossiers voisins du manuscrit (conventions du projet).
const char* const kCardsSuffix   = "_fiches";
const char* const kBackupSuffix  = "_backups";
const char* const kSyncSuffix    = "_sync";

// Chemin relatif au dossier racine, avec '/' comme séparateur — le même
// sur toutes les plateformes, pour qu'un inventaire écrit sous Windows
// soit compréhensible ailleurs.
std::string relativeUtf8(const fs::path& file, const fs::path& root) {
    const fs::path relative = file.lexically_relative(root);
    if (relative.empty()) return std::string();
    std::string text = utils::pathToUtf8(relative);
    for (char& c : text) {
        if (c == '\\') c = '/';
    }
    return text;
}

// Un chemin qu'on ne saurait pas réécrire ni relire fidèlement n'entre
// pas dans l'inventaire : retour à la ligne (il casserait le format), et
// remontée « .. » (elle sortirait du projet).
bool isStorablePath(const std::string& path) {
    if (path.empty()) return false;
    if (path.rfind("../", 0) == 0 || path.find("/../") != std::string::npos) return false;
    for (unsigned char c : path) {
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

std::string stemOf(const fs::path& manuscriptPath) {
    return utils::pathToUtf8(manuscriptPath.stem());
}

} // namespace

const ProjectFile* ProjectIndex::find(const std::string& path) const {
    for (const ProjectFile& file : files) {
        if (file.path == path) return &file;
    }
    return nullptr;
}

const char* changeKindName(ChangeKind kind) {
    switch (kind) {
        case ChangeKind::Added:    return "ajout";
        case ChangeKind::Modified: return "modif";
        case ChangeKind::Removed:  return "suppr";
    }
    return "modif";
}

bool changeKindFromName(const std::string& name, ChangeKind& out) {
    if (name == "ajout") { out = ChangeKind::Added;    return true; }
    if (name == "modif") { out = ChangeKind::Modified; return true; }
    if (name == "suppr") { out = ChangeKind::Removed;  return true; }
    return false;
}

fs::path syncFolderFor(const fs::path& manuscriptPath) {
    fs::path folder = manuscriptPath.parent_path() / manuscriptPath.stem();
    folder += kSyncSuffix;
    return folder;
}

fs::path indexPathFor(const fs::path& manuscriptPath) {
    return syncFolderFor(manuscriptPath) / "inventaire.txt";
}

fs::path journalPathFor(const fs::path& manuscriptPath) {
    return syncFolderFor(manuscriptPath) / "journal.txt";
}

fs::path basePathFor(const fs::path& manuscriptPath) {
    return syncFolderFor(manuscriptPath) / "derniere_synchro.txt";
}

ProjectIndex buildIndex(const fs::path& manuscriptPath,
                        const std::string& projectId,
                        size_t* skipped) {
    ProjectIndex index;
    index.projectId = projectId;
    if (skipped) *skipped = 0;
    if (manuscriptPath.empty()) return index;

    const fs::path root = manuscriptPath.parent_path();
    const std::string stem = stemOf(manuscriptPath);
    const std::string excludedBackups = stem + kBackupSuffix;
    const std::string excludedSync    = stem + kSyncSuffix;

    auto addFile = [&](const fs::path& file) {
        std::error_code ec;
        const auto size = fs::file_size(file, ec);
        if (ec) { if (skipped) ++*skipped; return; }

        const std::string relative = relativeUtf8(file, root);
        if (!isStorablePath(relative)) { if (skipped) ++*skipped; return; }

        bool ok = false;
        const std::string hash = utils::sha256File(file, &ok);
        if (!ok) { if (skipped) ++*skipped; return; } // illisible : ignoré, pas fatal

        ProjectFile entry;
        entry.path = relative;
        entry.size = static_cast<uint64_t>(size);
        entry.modToken = utils::fileModToken(file);
        entry.sha256 = hash;
        index.files.push_back(std::move(entry));
    };

    // 1. Le manuscrit lui-même.
    std::error_code ec;
    if (fs::exists(manuscriptPath, ec)) addFile(manuscriptPath);

    // 2. Tout ce qui se trouve dans le dossier du projet, sauf les
    //    dossiers techniques (sauvegardes horodatées et synchronisation).
    if (!root.empty() && fs::is_directory(root, ec)) {
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            const fs::path& current = it->path();
            const std::string name = utils::pathToUtf8(current.filename());

            if (it->is_directory(ec)) {
                // Dossiers écartés : ils se reconstituent, ou décrivent
                // l'inventaire lui-même.
                if (name == excludedBackups || name == excludedSync ||
                    (!name.empty() && name[0] == '.')) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!name.empty() && name[0] == '.') continue; // fichier masqué
            if (current == manuscriptPath) continue;       // déjà ajouté
            addFile(current);
        }
    }

    std::sort(index.files.begin(), index.files.end(),
              [](const ProjectFile& a, const ProjectFile& b) { return a.path < b.path; });
    return index;
}

// ---------------------------------------------------------------------
// Sérialisation
// ---------------------------------------------------------------------

std::string serializeIndex(const ProjectIndex& index) {
    std::string out = "# inventaire de projet 1\n";
    if (!index.projectId.empty()) out += "projet " + index.projectId + "\n";
    for (const ProjectFile& file : index.files) {
        out += "f " + file.sha256 + " " + std::to_string(file.size) + " " +
               (file.modToken.empty() ? "-" : file.modToken) + " " + file.path + "\n";
    }
    return out;
}

ProjectIndex parseIndex(const std::string& text) {
    ProjectIndex index;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        start = end + 1;
        if (line.empty() || line[0] == '#') continue;

        if (line.rfind("projet ", 0) == 0) {
            index.projectId = line.substr(7);
            continue;
        }
        if (line.rfind("f ", 0) != 0) continue; // ligne inconnue : ignorée

        // « f <empreinte> <taille> <date> <chemin> » : les quatre premiers
        // champs sont sans espace, le chemin prend tout le reste.
        size_t pos = 2;
        auto nextField = [&line, &pos]() -> std::string {
            const size_t space = line.find(' ', pos);
            if (space == std::string::npos) { pos = line.size(); return std::string(); }
            std::string field = line.substr(pos, space - pos);
            pos = space + 1;
            return field;
        };
        ProjectFile file;
        file.sha256 = nextField();
        const std::string sizeText = nextField();
        file.modToken = nextField();
        file.path = (pos <= line.size()) ? line.substr(pos) : std::string();
        if (file.modToken == "-") file.modToken.clear();

        if (file.sha256.size() != 64 || sizeText.empty() || !isStorablePath(file.path)) {
            continue; // ligne abîmée : ignorée, le reste de l'inventaire reste bon
        }
        // Taille : chiffres uniquement, et bornée pour ne pas déborder.
        bool digitsOnly = true;
        for (char c : sizeText) {
            if (c < '0' || c > '9') { digitsOnly = false; break; }
        }
        if (!digitsOnly || sizeText.size() > 19) continue;
        file.size = std::strtoull(sizeText.c_str(), nullptr, 10);

        index.files.push_back(std::move(file));
    }
    std::sort(index.files.begin(), index.files.end(),
              [](const ProjectFile& a, const ProjectFile& b) { return a.path < b.path; });
    return index;
}

bool saveIndex(const ProjectIndex& index, const fs::path& file, std::string* errorMessage) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);

    // Écriture atomique, comme toute écriture du projet : un inventaire à
    // moitié écrit vaudrait moins que pas d'inventaire du tout.
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (errorMessage) *errorMessage = "Impossible d'écrire : " + utils::pathToUtf8(tmp);
            return false;
        }
        const std::string text = serializeIndex(index);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            std::error_code ignored;
            fs::remove(tmp, ignored);
            if (errorMessage) *errorMessage = "Échec d'écriture : " + utils::pathToUtf8(tmp);
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        if (errorMessage) *errorMessage = "Impossible de renommer vers : " + utils::pathToUtf8(file);
        return false;
    }
    return true;
}

bool loadIndex(ProjectIndex& index, const fs::path& file, std::string* errorMessage) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (errorMessage) *errorMessage = "Inventaire introuvable : " + utils::pathToUtf8(file);
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    index = parseIndex(text);
    return true;
}

// ---------------------------------------------------------------------
// Comparaison
// ---------------------------------------------------------------------

std::vector<Change> diffIndex(const ProjectIndex& previous, const ProjectIndex& current) {
    std::vector<Change> changes;

    // Les deux inventaires sont triés par chemin : un parcours simultané
    // suffit, sans recherche imbriquée (donc sans temps quadratique sur
    // un projet à plusieurs milliers de fiches).
    size_t i = 0, j = 0;
    while (i < previous.files.size() || j < current.files.size()) {
        const bool hasOld = i < previous.files.size();
        const bool hasNew = j < current.files.size();

        if (hasOld && (!hasNew || previous.files[i].path < current.files[j].path)) {
            Change change;
            change.kind = ChangeKind::Removed;
            change.path = previous.files[i].path;
            changes.push_back(std::move(change));
            ++i;
        } else if (hasNew && (!hasOld || current.files[j].path < previous.files[i].path)) {
            Change change;
            change.kind = ChangeKind::Added;
            change.path = current.files[j].path;
            change.sha256 = current.files[j].sha256;
            change.size = current.files[j].size;
            changes.push_back(std::move(change));
            ++j;
        } else {
            // Même chemin des deux côtés : c'est l'EMPREINTE qui tranche,
            // pas la date. Réenregistrer un fichier sans le modifier
            // change sa date mais pas son contenu — ce n'est pas une
            // modification à propager.
            if (previous.files[i].sha256 != current.files[j].sha256) {
                Change change;
                change.kind = ChangeKind::Modified;
                change.path = current.files[j].path;
                change.sha256 = current.files[j].sha256;
                change.size = current.files[j].size;
                changes.push_back(std::move(change));
            }
            ++i;
            ++j;
        }
    }
    return changes;
}

} // namespace io
