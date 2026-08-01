// test_runner.cpp
// Tests de l'exécution d'un plan de synchronisation (net/SyncRunner).
//
// CE QUI SE JOUE ICI : c'est la seule couche du client qui écrase des
// fichiers. Une erreur y détruit du travail sans bruit. Les tests portent
// donc moins sur le cas où tout se passe bien que sur tous ceux où quelque
// chose se passe mal — panne au milieu, conflit surgi entre le calcul et
// l'exécution, fichier disparu, écriture refusée.
//
// Ni disque ni réseau : le transport et le stockage sont simulés, ce qui
// permet d'écrire en une ligne des situations qu'on ne saurait pas
// provoquer autrement.

#include "DiskFileStore.h"
#include "FakeTransport.h"
#include "Sha256.h"
#include "SyncRunner.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

namespace {

const char* const kProjet = "11111111-2222-4333-8444-555555555555";

std::string sha(const std::string& content) {
    utils::Sha256 hash;
    hash.update(content);
    return hash.finalHex();
}

// Un disque en mémoire, qui sait aussi refuser.
class FauxDisque : public net::FileStore {
public:
    std::map<std::string, std::string> fichiers;
    std::string refuseEcriture; // chemin dont l'écriture échouera
    std::string refuseLecture;

    bool read(const std::string& path, std::string& content, std::string* error) override {
        if (path == refuseLecture) {
            if (error) *error = "lecture refusée (simulé)";
            return false;
        }
        const auto it = fichiers.find(path);
        if (it == fichiers.end()) {
            if (error) *error = "fichier absent (simulé)";
            return false;
        }
        content = it->second;
        return true;
    }

    bool write(const std::string& path, const std::string& content,
               std::string* error) override {
        if (path == refuseEcriture) {
            if (error) *error = "disque plein (simulé)";
            return false;
        }
        fichiers[path] = content;
        return true;
    }

    bool remove(const std::string& path, std::string* error) override {
        (void)error;
        fichiers.erase(path);
        return true;
    }
};

io::SyncStep etape(io::SyncAction action, const std::string& path,
                   const std::string& localSha = {}, const std::string& remoteSha = {}) {
    io::SyncStep step;
    step.action = action;
    step.path = path;
    step.localSha = localSha;
    step.remoteSha = remoteSha;
    return step;
}

} // namespace

int main() {
    std::cout << std::unitbuf;

    // --- Test 1 : le cas nominal, dans les deux sens ---
    {
        const std::string recu = "# Chapitre premier\n\nIl faisait nuit.\n";
        const std::string envoye = "Elle sortit.\n";

        net::FakeTransport transport;
        transport.replyBytes(200, recu, sha(recu));            // réception
        transport.replyBytes(200, "", sha(envoye));            // envoi
        transport.reply(204, "", "");                          // suppression distante

        net::SyncClient client(transport, "jeton");

        FauxDisque disque;
        disque.fichiers["notes.md"] = envoye;
        disque.fichiers["vieux.md"] = "à supprimer ici";

        io::SyncPlan plan;
        plan.steps.push_back(etape(io::SyncAction::Download, "roman.md"));
        plan.steps.push_back(etape(io::SyncAction::Upload, "notes.md", sha(envoye), sha("ancien")));
        plan.steps.push_back(etape(io::SyncAction::DeleteRemote, "parti.md", "", sha("parti")));

        const net::SyncReport rapport = net::runSync(client, kProjet, plan, disque);
        assert(rapport.downloaded == 1);
        assert(rapport.uploaded == 1);
        assert(rapport.deletedRemote == 1);
        assert(rapport.clean());
        assert(rapport.done.size() == 3);
        // Le fichier reçu est bien sur le disque, tel quel.
        assert(disque.fichiers["roman.md"] == recu);
        std::cout << "Test 1 OK (reception, envoi, suppression)\n";
    }

    // --- Test 2 : suppression locale ---
    {
        net::FakeTransport transport;
        net::SyncClient client(transport);
        FauxDisque disque;
        disque.fichiers["retire.md"] = "contenu";

        io::SyncPlan plan;
        plan.steps.push_back(etape(io::SyncAction::DeleteLocal, "retire.md"));

        const net::SyncReport rapport = net::runSync(client, kProjet, plan, disque);
        assert(rapport.deletedLocal == 1);
        assert(disque.fichiers.count("retire.md") == 0);
        // Aucune requête : effacer chez soi ne regarde pas le serveur.
        assert(transport.sent.empty());
        std::cout << "Test 2 OK (suppression locale, sans requete)\n";
    }

    // --- Test 3 : conflits et lecture seule, décidés sans rien demander ---
    {
        net::FakeTransport transport;
        net::SyncClient client(transport);
        FauxDisque disque;

        io::SyncPlan plan;
        plan.steps.push_back(etape(io::SyncAction::Conflict, "roman.md"));
        plan.steps.push_back(etape(io::SyncAction::Blocked, "notes.md"));

        const net::SyncReport rapport = net::runSync(client, kProjet, plan, disque);
        assert(rapport.conflicts == 1);
        assert(rapport.blocked == 1);
        assert(rapport.done.empty());
        assert(!rapport.clean());
        assert(rapport.problems.size() == 2);
        assert(rapport.problems[0].conflict);
        assert(!rapport.problems[1].conflict);
        assert(transport.sent.empty()); // rien n'a été demandé au serveur
        std::cout << "Test 3 OK (conflit et lecture seule, sans requete)\n";
    }

    // --- Test 4 : UNE PANNE ARRÊTE TOUT ---
    {
        const std::string un = "premier";
        net::FakeTransport transport;
        transport.replyBytes(200, un, sha(un)); // le premier passe
        transport.breakDown("câble débranché (simulé)"); // puis la panne

        net::SyncClient client(transport);
        FauxDisque disque;

        io::SyncPlan plan;
        plan.steps.push_back(etape(io::SyncAction::Download, "a.md"));
        plan.steps.push_back(etape(io::SyncAction::Download, "b.md"));
        plan.steps.push_back(etape(io::SyncAction::Download, "c.md"));

        const net::SyncReport rapport = net::runSync(client, kProjet, plan, disque);
        assert(rapport.downloaded == 1);
        assert(rapport.interrupted);
        assert(rapport.interruption.find("câble") != std::string::npos);
        // On s'arrête à la panne : le troisième fichier n'est même pas tenté.
        assert(transport.sent.size() == 2);
        // Et ce qui est acquis l'est vraiment : la base pourra avancer
        // dessus, et le prochain plan ne redemandera que le reste.
        assert(rapport.done.size() == 1);
        assert(rapport.done[0] == "a.md");
        std::cout << "Test 4 OK (une panne arrete tout, l'acquis reste acquis)\n";
    }

    // --- Test 5 : UN REFUS N'ARRÊTE RIEN ---
    {
        const std::string bon = "contenu correct";
        net::FakeTransport transport;
        transport.reply(409, "Le fichier a changé sur le serveur."); // conflit sur a.md
        transport.reply(413, "Fichier trop volumineux.");            // refus sur b.md
        transport.replyBytes(200, "", sha(bon));                     // c.md passe

        net::SyncClient client(transport);
        FauxDisque disque;
        disque.fichiers["a.md"] = bon;
        disque.fichiers["b.md"] = bon;
        disque.fichiers["c.md"] = bon;

        io::SyncPlan plan;
        plan.steps.push_back(etape(io::SyncAction::Upload, "a.md", sha(bon), sha("vieux")));
        plan.steps.push_back(etape(io::SyncAction::Upload, "b.md", sha(bon), sha("vieux")));
        plan.steps.push_back(etape(io::SyncAction::Upload, "c.md", sha(bon), sha("vieux")));

        const net::SyncReport rapport = net::runSync(client, kProjet, plan, disque);
        assert(!rapport.interrupted);       // on est allé au bout
        assert(rapport.uploaded == 1);      // seul c.md
        assert(rapport.conflicts == 1);     // a.md
        assert(rapport.failed == 1);        // b.md
        assert(rapport.done.size() == 1 && rapport.done[0] == "c.md");
        assert(transport.sent.size() == 3); // les trois ont bien été tentés
        std::cout << "Test 5 OK (un refus isole n'arrete pas le reste)\n";
    }

    // --- Test 6 : la session expirée arrête, mais le dit autrement ---
    {
        net::FakeTransport transport;
        transport.reply(401, "Session expirée.");

        net::SyncClient client(transport, "vieux-jeton");
        FauxDisque disque;

        io::SyncPlan plan;
        plan.steps.push_back(etape(io::SyncAction::Download, "a.md"));
        plan.steps.push_back(etape(io::SyncAction::Download, "b.md"));

        const net::SyncReport rapport = net::runSync(client, kProjet, plan, disque);
        assert(rapport.interrupted);
        assert(rapport.done.empty());
        assert(transport.sent.size() == 1); // on n'insiste pas avec un jeton mort
        std::cout << "Test 6 OK (session expiree : on s'arrete)\n";
    }

    // --- Test 7 : ce qui rate côté disque ---
    {
        const std::string recu = "texte reçu";
        net::FakeTransport transport;
        transport.replyBytes(200, recu, sha(recu));

        net::SyncClient client(transport);
        FauxDisque disque;
        disque.refuseEcriture = "roman.md";

        io::SyncPlan plan;
        plan.steps.push_back(etape(io::SyncAction::Download, "roman.md"));

        net::SyncReport rapport = net::runSync(client, kProjet, plan, disque);
        assert(rapport.failed == 1);
        assert(rapport.downloaded == 0);
        // RIEN n'est déclaré abouti : la base ne doit surtout pas
        // enregistrer une réception qui n'a pas atteint le disque, sinon le
        // fichier ne serait jamais re-téléchargé.
        assert(rapport.done.empty());
        assert(rapport.problems[0].message.find("disque plein") != std::string::npos);

        // Et un fichier envolé entre le plan et l'envoi.
        net::FakeTransport t2;
        net::SyncClient c2(t2);
        FauxDisque d2;
        io::SyncPlan p2;
        p2.steps.push_back(etape(io::SyncAction::Upload, "disparu.md", "abc", ""));
        rapport = net::runSync(c2, kProjet, p2, d2);
        assert(rapport.failed == 1);
        assert(t2.sent.empty()); // on n'envoie pas un fichier qu'on n'a pas lu
        std::cout << "Test 7 OK (echecs disque : rien n'est declare abouti)\n";
    }

    // --- Test 8 : plan vide, et cohérence des compteurs ---
    {
        net::FakeTransport transport;
        net::SyncClient client(transport);
        FauxDisque disque;

        const net::SyncReport rapport = net::runSync(client, kProjet, io::SyncPlan{}, disque);
        assert(rapport.clean());
        assert(rapport.changedFiles() == 0);
        assert(rapport.done.empty());
        assert(rapport.problems.empty());
        assert(transport.sent.empty());
        std::cout << "Test 8 OK (plan vide)\n";
    }

    // --- Test 9 : LE DOSSIER DU PROJET EST UNE PRISON ---
    //
    // Les chemins traités ici viennent d'un inventaire DISTANT. Un serveur
    // fautif ou hostile ne doit pas pouvoir faire écrire l'éditeur ailleurs
    // que dans le dossier du manuscrit — c'est le seul endroit du client
    // où une erreur de chemin toucherait des fichiers étrangers.
    {
        namespace fs = std::filesystem;
        const fs::path racine = fs::temp_directory_path() / "test_runner_disque";
        std::error_code ec;
        fs::remove_all(racine, ec);
        fs::create_directories(racine / "fiches", ec);

        net::DiskFileStore disque(racine);

        // Ce qui est légitime passe.
        assert(!disque.resolve("roman.md").empty());
        assert(!disque.resolve("fiches/elise.md").empty());
        assert(!disque.resolve("images/couverture.png").empty());

        // Tout ce qui sortirait du dossier est refusé, quelle que soit la
        // façon de s'y prendre.
        const char* const hostiles[] = {
            "", "/etc/passwd", "../voisin.md", "a/../../voisin.md",
            "dossier\\a.md", "C:/windows/x.md", "a.md:flux", "a//b.md",
        };
        for (const char* chemin : hostiles) {
            assert(disque.resolve(chemin).empty());
        }

        // Et le refus va jusqu'au bout : rien n'est écrit.
        std::string erreur;
        assert(!disque.write("../evade.md", "contenu", &erreur));
        assert(!erreur.empty());
        assert(!fs::exists(racine.parent_path() / "evade.md"));

        // Aller-retour réel, avec écriture atomique.
        const std::string contenu = "# Chapitre\n\nTexte accentué : été.\n";
        assert(disque.write("fiches/elise.md", contenu, &erreur));
        std::string relu;
        assert(disque.read("fiches/elise.md", relu, &erreur));
        assert(relu == contenu);
        // Aucun fichier temporaire ne subsiste après une écriture réussie.
        assert(!fs::exists(racine / "fiches" / "elise.md.tmp"));

        // Suppression, puis suppression d'un fichier déjà absent : le
        // résultat voulu est atteint dans les deux cas.
        assert(disque.remove("fiches/elise.md", &erreur));
        assert(!fs::exists(racine / "fiches" / "elise.md"));
        assert(disque.remove("fiches/elise.md", &erreur));

        // Lire ce qui n'existe pas échoue proprement.
        assert(!disque.read("absent.md", relu, &erreur));
        assert(relu.empty());

        fs::remove_all(racine, ec);
        std::cout << "Test 9 OK (aucun chemin ne sort du dossier du projet)\n";
    }

    std::cout << "Tous les tests d'execution sont passes.\n";
    return 0;
}
