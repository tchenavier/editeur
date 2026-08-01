// Pairing.cpp
// La réponse de l'auteur à « voulez-vous synchroniser ce manuscrit ? ».
// Voir Pairing.h.

#include "Pairing.h"

#include "PathUtf8.h"
#include "ProjectIndex.h" // syncFolderFor : un seul endroit décide où vivent ces fichiers

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace io {

namespace {

// Le format est « clé valeur », une paire par ligne : le même que la
// poignée de main du protocole, et pour la même raison — il se lit à
// l'œil nu et s'analyse en dix lignes.
std::string valueOf(const std::string& text, const std::string& key) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const size_t space = line.find(' ');
        if (space == key.size() && line.compare(0, space, key) == 0) {
            return line.substr(space + 1);
        }
        start = end + 1;
    }
    return {};
}

// Une valeur qui contiendrait une fin de ligne casserait le format en
// deux : on la refuse à l'écriture plutôt que d'écrire un fichier qui se
// relirait de travers.
std::string oneLine(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        if (c == '\n' || c == '\r') continue;
        out += c;
    }
    return out;
}

} // namespace

const char* pairingStateName(PairingState state) {
    switch (state) {
        case PairingState::Paired: return "appaire";
        case PairingState::Never:  return "jamais";
        case PairingState::None:   break;
    }
    return "aucun";
}

const char* pairingModeName(PairingMode mode) {
    return mode == PairingMode::TwoWay ? "deux-sens" : "lecture-seule";
}

fs::path pairingPathFor(const fs::path& manuscriptPath) {
    return syncFolderFor(manuscriptPath) / "appairage.txt";
}

std::string serializePairing(const Pairing& pairing) {
    std::ostringstream out;
    out << "# Appairage de ce projet. Fichier lisible et modifiable a la main.\n";
    out << "# Aucune information confidentielle : rien qui serve a s'authentifier.\n";
    out << "etat " << pairingStateName(pairing.state) << "\n";
    out << "mode " << pairingModeName(pairing.mode) << "\n";
    out << "auto " << (pairing.autoSync ? "oui" : "non") << "\n";
    if (!pairing.server.empty()) out << "serveur " << oneLine(pairing.server) << "\n";
    if (!pairing.pairedAt.empty()) out << "date " << oneLine(pairing.pairedAt) << "\n";
    return out.str();
}

Pairing parsePairing(const std::string& text) {
    Pairing pairing;

    const std::string state = valueOf(text, "etat");
    if (state == "appaire") pairing.state = PairingState::Paired;
    else if (state == "jamais") pairing.state = PairingState::Never;
    else pairing.state = PairingState::None;

    pairing.mode = (valueOf(text, "mode") == "deux-sens") ? PairingMode::TwoWay
                                                          : PairingMode::ReadOnly;
    pairing.autoSync = (valueOf(text, "auto") == "oui");
    pairing.server = valueOf(text, "serveur");
    pairing.pairedAt = valueOf(text, "date");

    // Un appairage sans serveur ne veut rien dire : on le ramène à
    // « aucun » plutôt que de laisser un état qui promettrait une
    // synchronisation impossible.
    if (pairing.state == PairingState::Paired && pairing.server.empty()) {
        pairing.state = PairingState::None;
    }
    return pairing;
}

Pairing loadPairing(const fs::path& manuscriptPath) {
    if (manuscriptPath.empty()) return {};
    const fs::path file = pairingPathFor(manuscriptPath);

    std::ifstream in(file, std::ios::binary);
    if (!in) return {}; // absent : la question n'a jamais été posée

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parsePairing(buffer.str());
}

bool savePairing(const Pairing& pairing, const fs::path& manuscriptPath,
                 std::string* errorMessage) {
    if (manuscriptPath.empty()) {
        if (errorMessage) *errorMessage = "Le manuscrit doit être enregistré avant d'être appairé.";
        return false;
    }
    const fs::path file = pairingPathFor(manuscriptPath);

    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);

    // Écriture atomique, comme toute écriture du projet.
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (errorMessage) *errorMessage = "Impossible d'écrire : " + utils::pathToUtf8(tmp);
            return false;
        }
        const std::string text = serializePairing(pairing);
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

} // namespace io
