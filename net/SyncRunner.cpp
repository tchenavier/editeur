// SyncRunner.cpp
// Exécution d'un plan de synchronisation. Voir SyncRunner.h.

#include "SyncRunner.h"

namespace net {

namespace {

// Une panne : rien n'a été dit, ou le serveur est hors d'état. Réessayer
// le fichier suivant donnerait exactement le même résultat.
bool isBreakdown(const Result& result) {
    return result.outcome == Outcome::Unreachable ||
           result.outcome == Outcome::ServerError ||
           result.outcome == Outcome::Busy;
}

// Une session expirée est une panne d'un genre particulier : inutile de
// continuer, mais l'auteur doit savoir qu'il s'agit de se reconnecter et
// non d'attendre.
bool isSessionLost(const Result& result) {
    return result.outcome == Outcome::Unauthorized;
}

} // namespace

SyncReport runSync(SyncClient& client, const std::string& projectId,
                   const io::SyncPlan& plan, FileStore& files) {
    SyncReport report;

    for (const io::SyncStep& step : plan.steps) {
        auto note = [&report, &step](const std::string& message, bool conflict) {
            report.problems.push_back({step.path, message, conflict});
        };

        // Les deux cas décidés d'avance : rien à demander au serveur.
        if (step.action == io::SyncAction::Conflict) {
            ++report.conflicts;
            note("Modifié des deux côtés : à vous de choisir la version à garder.", true);
            continue;
        }
        if (step.action == io::SyncAction::Blocked) {
            ++report.blocked;
            note("Ce projet est partagé en lecture seule : la modification reste ici.", false);
            continue;
        }

        Result result{Outcome::Ok, {}};

        switch (step.action) {
            case io::SyncAction::Download: {
                std::string content, sha;
                result = client.readFile(projectId, step.path, content, sha);
                if (result.ok()) {
                    // Le contenu a déjà été vérifié contre son empreinte par
                    // le client : ce qu'on écrit est bien ce que le serveur
                    // a annoncé.
                    std::string error;
                    if (!files.write(step.path, content, &error)) {
                        ++report.failed;
                        note(error.empty() ? "Écriture impossible." : error, false);
                        continue;
                    }
                    ++report.downloaded;
                }
                break;
            }
            case io::SyncAction::Upload: {
                std::string content;
                std::string error;
                if (!files.read(step.path, content, &error)) {
                    // Le fichier a disparu entre le calcul du plan et son
                    // exécution. Ce n'est pas grave : le prochain plan en
                    // tiendra compte, puisqu'il se recalcule.
                    ++report.failed;
                    note(error.empty() ? "Fichier introuvable au moment de l'envoi." : error,
                         false);
                    continue;
                }
                std::string newSha;
                result = client.writeFile(projectId, step.path, content,
                                          step.remoteSha, newSha);
                if (result.ok()) ++report.uploaded;
                break;
            }
            case io::SyncAction::DeleteLocal: {
                std::string error;
                if (!files.remove(step.path, &error)) {
                    ++report.failed;
                    note(error.empty() ? "Suppression impossible." : error, false);
                    continue;
                }
                ++report.deletedLocal;
                break;
            }
            case io::SyncAction::DeleteRemote: {
                result = client.removeFile(projectId, step.path, step.remoteSha);
                if (result.ok()) ++report.deletedRemote;
                break;
            }
            case io::SyncAction::Conflict:
            case io::SyncAction::Blocked:
                break; // traités plus haut
        }

        if (result.ok()) {
            report.done.push_back(step.path);
            continue;
        }

        // RÈGLE 1 : une panne arrête tout. Ce qui reste sera retrouvé par
        // le prochain plan, à l'identique.
        if (isBreakdown(result) || isSessionLost(result)) {
            report.interrupted = true;
            report.interruption = result.message;
            note(result.message, false);
            break;
        }

        // RÈGLE 2 : un refus ne concerne que ce fichier.
        if (result.outcome == Outcome::Conflict) {
            // Quelqu'un est passé entre le calcul du plan et son
            // exécution. C'est exactement le cas que « If-Match » sert à
            // rendre visible plutôt que d'écraser en silence.
            ++report.conflicts;
            note("Le fichier a changé sur le serveur pendant la synchronisation.", true);
        } else if (result.outcome == Outcome::Forbidden) {
            ++report.blocked;
            note(result.message, false);
        } else {
            ++report.failed;
            note(result.message, false);
        }
    }

    return report;
}

} // namespace net
