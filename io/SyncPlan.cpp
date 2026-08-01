// SyncPlan.cpp
// Le moteur de synchronisation : décider quoi faire. Voir SyncPlan.h.

#include "SyncPlan.h"

#include <algorithm>
#include <set>

namespace io {

namespace {

// L'empreinte d'un chemin dans un inventaire, ou une chaîne vide s'il n'y
// figure pas. « Absent » et « vide » se confondent volontairement : un
// fichier sans empreinte n'est pas comparable, donc autant le traiter
// comme absent que de bâtir une décision sur rien.
std::string shaOf(const ProjectIndex& index, const std::string& path) {
    const ProjectFile* file = index.find(path);
    return file ? file->sha256 : std::string();
}

uint64_t sizeOf(const ProjectIndex& index, const std::string& path) {
    const ProjectFile* file = index.find(path);
    return file ? file->size : 0;
}

std::set<std::string> allPaths(const ProjectIndex& a, const ProjectIndex& b,
                               const ProjectIndex& c) {
    std::set<std::string> paths;
    for (const ProjectFile& file : a.files) paths.insert(file.path);
    for (const ProjectFile& file : b.files) paths.insert(file.path);
    for (const ProjectFile& file : c.files) paths.insert(file.path);
    return paths;
}

} // namespace

const char* syncActionName(SyncAction action) {
    switch (action) {
        case SyncAction::Upload:       return "envoi";
        case SyncAction::DeleteLocal:  return "suppression locale";
        case SyncAction::DeleteRemote: return "suppression distante";
        case SyncAction::Conflict:     return "conflit";
        case SyncAction::Blocked:      return "bloqué (lecture seule)";
        case SyncAction::Download:     break;
    }
    return "réception";
}

size_t SyncPlan::count(SyncAction action) const {
    size_t total = 0;
    for (const SyncStep& step : steps) {
        if (step.action == action) ++total;
    }
    return total;
}

uint64_t SyncPlan::bytesToTransfer() const {
    uint64_t total = 0;
    for (const SyncStep& step : steps) {
        if (step.action == SyncAction::Download || step.action == SyncAction::Upload) {
            total += step.size;
        }
    }
    return total;
}

SyncPlan planSync(const ProjectIndex& base, const ProjectIndex& local,
                  const ProjectIndex& remote, SyncMode mode) {
    SyncPlan plan;

    // std::set : les chemins arrivent triés, donc deux calculs sur les
    // mêmes données donnent exactement le même plan. Un plan reproductible
    // se teste ; un plan dont l'ordre dépend du hasard, non.
    for (const std::string& path : allPaths(base, local, remote)) {
        const std::string baseSha = shaOf(base, path);
        const std::string localSha = shaOf(local, path);
        const std::string remoteSha = shaOf(remote, path);

        // Les deux côtés d'accord : rien à faire. Cela couvre aussi le cas
        // où le fichier a disparu des deux côtés.
        if (localSha == remoteSha) continue;

        const bool localChanged = (localSha != baseSha);
        const bool remoteChanged = (remoteSha != baseSha);

        SyncStep step;
        step.path = path;
        step.baseSha = baseSha;
        step.localSha = localSha;
        step.remoteSha = remoteSha;

        if (!localChanged && remoteChanged) {
            // Seul le serveur a bougé.
            step.action = remoteSha.empty() ? SyncAction::DeleteLocal : SyncAction::Download;
            step.size = sizeOf(remote, path);
        } else if (localChanged && !remoteChanged) {
            // Seul le poste a bougé.
            step.action = localSha.empty() ? SyncAction::DeleteRemote : SyncAction::Upload;
            step.size = sizeOf(local, path);
        } else {
            // Les deux ont bougé, et différemment — sans quoi les
            // empreintes seraient égales et on serait sorti plus haut.
            // Aucune règle automatique ne peut trancher sans risquer de
            // détruire du travail : c'est à l'auteur de voir.
            step.action = SyncAction::Conflict;
            step.size = 0;
        }

        // En lecture seule, ce qui devait partir ne part pas — mais on le
        // DIT. Un envoi silencieusement abandonné laisserait l'auteur
        // croire que son texte est sur le serveur.
        if (mode == SyncMode::ReadOnly &&
            (step.action == SyncAction::Upload || step.action == SyncAction::DeleteRemote)) {
            step.action = SyncAction::Blocked;
        }

        plan.steps.push_back(step);
    }
    return plan;
}

ProjectIndex advanceBase(const ProjectIndex& base, const ProjectIndex& local,
                         const ProjectIndex& remote, const SyncPlan& plan,
                         const std::vector<std::string>& done) {
    const std::set<std::string> succeeded(done.begin(), done.end());

    ProjectIndex result;
    result.projectId = local.projectId.empty() ? base.projectId : local.projectId;

    for (const std::string& path : allPaths(base, local, remote)) {
        const ProjectFile* baseFile = base.find(path);
        const ProjectFile* localFile = local.find(path);
        const ProjectFile* remoteFile = remote.find(path);

        // 1. Disparu des deux côtés : il sort de la base. La garder
        //    ferait réapparaître le fichier au prochain calcul.
        if (!localFile && !remoteFile) continue;

        // 2. Les deux côtés d'accord : c'est l'état commun, même si aucune
        //    action ne l'a produit. L'inscrire est ce qui permettra plus
        //    tard de reconnaître une SUPPRESSION distante au lieu de la
        //    prendre pour un ajout local.
        if (localFile && remoteFile && localFile->sha256 == remoteFile->sha256) {
            result.files.push_back(*remoteFile);
            continue;
        }

        // 3. Une action a abouti sur ce chemin : la base prend son effet.
        if (succeeded.count(path) != 0) {
            const SyncStep* step = nullptr;
            for (const SyncStep& candidate : plan.steps) {
                if (candidate.path == path) { step = &candidate; break; }
            }
            if (step) {
                switch (step->action) {
                    case SyncAction::Download:
                        if (remoteFile) result.files.push_back(*remoteFile);
                        continue;
                    case SyncAction::Upload:
                        if (localFile) result.files.push_back(*localFile);
                        continue;
                    case SyncAction::DeleteLocal:
                    case SyncAction::DeleteRemote:
                        continue; // le fichier n'existe plus : rien à inscrire
                    case SyncAction::Conflict:
                    case SyncAction::Blocked:
                        break; // rien n'a pu aboutir : on garde l'ancien état
                }
            }
        }

        // 4. Rien n'a abouti : la base ne bouge pas. C'est ce qui rend une
        //    synchronisation interrompue reprenable — le plan suivant
        //    retrouvera exactement le même travail à faire.
        if (baseFile) result.files.push_back(*baseFile);
    }

    std::sort(result.files.begin(), result.files.end(),
              [](const ProjectFile& a, const ProjectFile& b) { return a.path < b.path; });
    return result;
}

} // namespace io
