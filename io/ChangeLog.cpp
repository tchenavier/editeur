// ChangeLog.cpp
// Journal des modifications : ajout, lecture tolérante, compactage.
// Voir ChangeLog.h.

#include "ChangeLog.h"

#include "PathUtf8.h"

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>

namespace io {

namespace {

namespace fs = std::filesystem;

// Une ligne : « <horodatage> <action> <empreinte> <taille> <chemin> ».
// Les quatre premiers champs ne contiennent jamais d'espace ; le chemin
// prend tout le reste de la ligne, il peut donc en contenir.
std::string formatEntry(const std::string& timestamp, const Change& change) {
    return timestamp + " " + changeKindName(change.kind) + " " +
           (change.sha256.empty() ? "-" : change.sha256) + " " +
           std::to_string(change.size) + " " + change.path + "\n";
}

} // namespace

std::string utcTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    // Tampon initialisé : en cas d'échec, strftime laisse le contenu
    // indéterminé et on en ferait une chaîne.
    char stamp[32] = {0};
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string(stamp);
}

bool appendChanges(const fs::path& journalPath, const std::vector<Change>& changes,
                   std::string* errorMessage) {
    if (changes.empty()) return true; // rien à écrire n'est pas un échec

    std::error_code ec;
    fs::create_directories(journalPath.parent_path(), ec);

    // On assemble le texte AVANT d'ouvrir le fichier : l'écriture est
    // alors un seul appel, et deux enregistrements rapprochés ne peuvent
    // pas entrelacer leurs lignes.
    const std::string timestamp = utcTimestamp();
    std::string text;
    for (const Change& change : changes) {
        text += formatEntry(timestamp, change);
    }

    std::ofstream out(journalPath, std::ios::binary | std::ios::app);
    if (!out) {
        if (errorMessage) {
            *errorMessage = "Journal inaccessible : " + utils::pathToUtf8(journalPath);
        }
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
        if (errorMessage) {
            *errorMessage = "Échec d'écriture du journal : " + utils::pathToUtf8(journalPath);
        }
        return false;
    }
    return true;
}

std::vector<JournalEntry> parseJournal(const std::string& text) {
    std::vector<JournalEntry> entries;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        const bool complete = (end != std::string::npos);
        if (!complete) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        // Dernière ligne sans retour à la ligne : écriture interrompue.
        // On la laisse de côté plutôt que d'en tirer des valeurs fausses.
        if (!complete) break;

        size_t pos = 0;
        auto nextField = [&line, &pos]() -> std::string {
            const size_t space = line.find(' ', pos);
            if (space == std::string::npos) { pos = line.size() + 1; return std::string(); }
            std::string field = line.substr(pos, space - pos);
            pos = space + 1;
            return field;
        };

        JournalEntry entry;
        entry.timestamp = nextField();
        const std::string action = nextField();
        entry.sha256 = nextField();
        const std::string sizeText = nextField();
        entry.path = (pos <= line.size()) ? line.substr(pos) : std::string();
        if (entry.sha256 == "-") entry.sha256.clear();

        if (entry.timestamp.size() != 20 || entry.path.empty()) continue;
        if (!changeKindFromName(action, entry.kind)) continue;
        bool digitsOnly = !sizeText.empty() && sizeText.size() <= 19;
        for (char c : sizeText) {
            if (c < '0' || c > '9') { digitsOnly = false; break; }
        }
        if (!digitsOnly) continue;
        entry.size = std::strtoull(sizeText.c_str(), nullptr, 10);

        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<JournalEntry> readJournal(const fs::path& journalPath) {
    std::ifstream in(journalPath, std::ios::binary);
    if (!in) return {}; // pas encore de journal : ce n'est pas une erreur
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    return parseJournal(text);
}

size_t compactJournal(const fs::path& journalPath, size_t keep, std::string* errorMessage) {
    const std::vector<JournalEntry> entries = readJournal(journalPath);
    if (entries.size() <= keep) return 0;

    const size_t removed = entries.size() - keep;
    std::string text;
    for (size_t i = removed; i < entries.size(); ++i) {
        Change change;
        change.kind = entries[i].kind;
        change.path = entries[i].path;
        change.sha256 = entries[i].sha256;
        change.size = entries[i].size;
        text += formatEntry(entries[i].timestamp, change);
    }

    // Ici on remet en jeu tout le fichier : écriture atomique obligatoire.
    fs::path tmp = journalPath;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (errorMessage) {
                *errorMessage = "Compactage impossible : " + utils::pathToUtf8(tmp);
            }
            return 0;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            std::error_code ignored;
            fs::remove(tmp, ignored);
            if (errorMessage) *errorMessage = "Échec d'écriture du compactage.";
            return 0;
        }
    }
    std::error_code ec;
    fs::rename(tmp, journalPath, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        if (errorMessage) *errorMessage = "Échec du remplacement du journal.";
        return 0;
    }
    return removed;
}

} // namespace io
