// DiskFileStore.cpp
// Le dossier du projet, et la certitude de ne pas en sortir.
// Voir DiskFileStore.h.

#include "DiskFileStore.h"

#include "PathUtf8.h"

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace net {

namespace {

// Une taille au-delà de laquelle on refuse de charger un fichier en
// mémoire pour l'envoyer. C'est la même borne que celle du protocole :
// au-delà, le serveur refuserait de toute façon.
constexpr std::uintmax_t kMaxLecture = 64ull * 1024ull * 1024ull;

} // namespace

DiskFileStore::DiskFileStore(fs::path root) : m_root(std::move(root)) {}

fs::path DiskFileStore::resolve(const std::string& path) const {
    // Barrière 1 : la même validation que celle appliquée à l'envoi.
    if (m_root.empty() || !SyncClient::isSafePath(path)) return {};

    // lexically_normal réduit les « . » et « .. » sans toucher au disque :
    // on n'a pas besoin que le fichier existe pour vérifier où il irait.
    const fs::path racine = fs::absolute(m_root).lexically_normal();
    const fs::path complet = (racine / utils::pathFromUtf8(path)).lexically_normal();

    // Barrière 2 : le résultat reste-t-il sous la racine ? On compare
    // composant par composant plutôt que par préfixe de chaîne — un
    // dossier voisin nommé « projet-bis » ne doit pas passer pour un
    // sous-dossier de « projet ».
    auto itRacine = racine.begin();
    auto itComplet = complet.begin();
    for (; itRacine != racine.end(); ++itRacine, ++itComplet) {
        if (itComplet == complet.end()) return {};
        if (*itComplet != *itRacine) return {};
    }
    if (itComplet == complet.end()) return {}; // c'est la racine elle-même
    return complet;
}

bool DiskFileStore::read(const std::string& path, std::string& content,
                         std::string* error) {
    content.clear();
    const fs::path file = resolve(path);
    if (file.empty()) {
        if (error) *error = "Chemin refusé : " + path;
        return false;
    }

    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) {
        if (error) *error = "Fichier introuvable : " + path;
        return false;
    }
    if (size > kMaxLecture) {
        if (error) *error = "Fichier trop volumineux pour être envoyé : " + path;
        return false;
    }

    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (error) *error = "Lecture impossible : " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    content = buffer.str();
    return true;
}

bool DiskFileStore::write(const std::string& path, const std::string& content,
                          std::string* error) {
    const fs::path file = resolve(path);
    if (file.empty()) {
        if (error) *error = "Chemin refusé : " + path;
        return false;
    }

    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);

    // Écriture atomique : on écrit à côté, puis on renomme. Une coupure
    // ne peut donc pas laisser un chapitre à moitié reçu à la place du
    // chapitre entier qu'il remplace.
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "Écriture impossible : " + path;
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            fs::remove(tmp, ec);
            if (error) *error = "Échec d'écriture : " + path;
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(tmp, ec);
        if (error) *error = "Renommage impossible : " + path;
        return false;
    }
    return true;
}

bool DiskFileStore::remove(const std::string& path, std::string* error) {
    const fs::path file = resolve(path);
    if (file.empty()) {
        if (error) *error = "Chemin refusé : " + path;
        return false;
    }
    std::error_code ec;
    fs::remove(file, ec);
    // Un fichier déjà absent n'est pas une erreur : le résultat voulu est
    // atteint. On ne fait échouer que si le système refuse vraiment.
    if (ec && fs::exists(file)) {
        if (error) *error = "Suppression impossible : " + path;
        return false;
    }
    return true;
}

} // namespace net
