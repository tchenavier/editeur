// test_sync.cpp
// Tests du moteur de synchronisation (io/SyncPlan).
//
// CE QUI EST VÉRIFIÉ ICI n'est pas « est-ce que ça transfère » — rien ne
// transfère dans ce module — mais « est-ce que ça DÉCIDE juste ». C'est la
// seule couche où une erreur détruit du travail sans bruit : envoyer au
// lieu de recevoir écrase le serveur, recevoir au lieu d'envoyer écrase le
// manuscrit. Chaque cas de figure est donc écrit à la main, y compris ceux
// qu'on ne saurait pas provoquer pour de vrai.

#include "SyncPlan.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Un inventaire écrit à la main : { chemin, empreinte } et rien d'autre,
// puisque c'est tout ce sur quoi le moteur raisonne.
io::ProjectIndex index(std::initializer_list<std::pair<const char*, const char*>> files) {
    io::ProjectIndex result;
    result.projectId = "11111111-2222-4333-8444-555555555555";
    for (const auto& entry : files) {
        io::ProjectFile file;
        file.path = entry.first;
        file.sha256 = entry.second;
        file.size = 100;
        result.files.push_back(file);
    }
    return result;
}

const io::SyncStep* stepFor(const io::SyncPlan& plan, const std::string& path) {
    for (const io::SyncStep& step : plan.steps) {
        if (step.path == path) return &step;
    }
    return nullptr;
}

// L'action décidée pour un chemin, ou "-" s'il n'y a rien à faire.
std::string actionFor(const io::SyncPlan& plan, const std::string& path) {
    const io::SyncStep* step = stepFor(plan, path);
    return step ? io::syncActionName(step->action) : "-";
}

const char* const A = "aaaa";
const char* const B = "bbbb";
const char* const C = "cccc";

} // namespace

int main() {
    std::cout << std::unitbuf;

    // --- Test 1 : rien n'a bougé ---
    {
        const auto base = index({{"roman.md", A}, {"fiches/elise.md", B}});
        const auto plan = io::planSync(base, base, base, io::SyncMode::TwoWay);
        assert(plan.empty());
        assert(!plan.hasConflicts());
        assert(plan.bytesToTransfer() == 0);
        std::cout << "Test 1 OK (rien a faire)\n";
    }

    // --- Test 2 : un seul côté a bougé ---
    {
        const auto base = index({{"roman.md", A}});

        // Modifié ici : il part.
        auto plan = io::planSync(base, index({{"roman.md", B}}), base, io::SyncMode::TwoWay);
        assert(plan.steps.size() == 1);
        assert(actionFor(plan, "roman.md") == std::string("envoi"));
        // L'empreinte distante est ce qu'on passera en « If-Match » :
        // c'est elle qui transforme un écrasement en conflit visible.
        assert(stepFor(plan, "roman.md")->remoteSha == A);

        // Modifié là-bas : on le reçoit.
        plan = io::planSync(base, base, index({{"roman.md", C}}), io::SyncMode::TwoWay);
        assert(actionFor(plan, "roman.md") == std::string("réception"));
        std::cout << "Test 2 OK (un seul cote a bouge)\n";
    }

    // --- Test 3 : LA question que deux inventaires ne peuvent pas trancher ---
    {
        // Même situation observable des deux côtés — un fichier ici, pas
        // là-bas — et deux réponses OPPOSÉES selon la base. C'est la
        // raison d'être du troisième inventaire.
        const auto avecFichier = index({{"roman.md", A}, {"chapitre2.md", B}});
        const auto sansFichier = index({{"roman.md", A}});

        // La base ne le connaît pas : il vient d'être créé ici. On l'envoie.
        auto plan = io::planSync(sansFichier, avecFichier, sansFichier, io::SyncMode::TwoWay);
        assert(actionFor(plan, "chapitre2.md") == std::string("envoi"));

        // La base le connaît : il a été supprimé sur le serveur. On l'efface.
        plan = io::planSync(avecFichier, avecFichier, sansFichier, io::SyncMode::TwoWay);
        assert(actionFor(plan, "chapitre2.md") == std::string("suppression locale"));

        // Et symétriquement, côté distant.
        plan = io::planSync(sansFichier, sansFichier, avecFichier, io::SyncMode::TwoWay);
        assert(actionFor(plan, "chapitre2.md") == std::string("réception"));
        plan = io::planSync(avecFichier, sansFichier, avecFichier, io::SyncMode::TwoWay);
        assert(actionFor(plan, "chapitre2.md") == std::string("suppression distante"));
        std::cout << "Test 3 OK (la base tranche entre ajout et suppression)\n";
    }

    // --- Test 4 : conflits ---
    {
        const auto base = index({{"roman.md", A}});

        // Modifié des deux côtés, différemment.
        auto plan = io::planSync(base, index({{"roman.md", B}}),
                                 index({{"roman.md", C}}), io::SyncMode::TwoWay);
        assert(plan.hasConflicts());
        assert(actionFor(plan, "roman.md") == std::string("conflit"));

        // Modifié des deux côtés, mais À L'IDENTIQUE : ce n'est pas un
        // conflit, c'est un accord. Rien à faire.
        plan = io::planSync(base, index({{"roman.md", B}}),
                            index({{"roman.md", B}}), io::SyncMode::TwoWay);
        assert(plan.empty());

        // Supprimé ici, modifié là-bas : personne ne peut trancher à la
        // place de l'auteur.
        plan = io::planSync(base, index({}), index({{"roman.md", C}}), io::SyncMode::TwoWay);
        assert(actionFor(plan, "roman.md") == std::string("conflit"));

        // Première synchronisation (aucune base), contenus différents des
        // deux côtés : conflit. Une date de fichier ne prouverait rien.
        plan = io::planSync(io::ProjectIndex{}, index({{"roman.md", A}}),
                            index({{"roman.md", B}}), io::SyncMode::TwoWay);
        assert(actionFor(plan, "roman.md") == std::string("conflit"));
        std::cout << "Test 4 OK (conflits, et ce qui n'en est pas un)\n";
    }

    // --- Test 5 : lecture seule ---
    {
        const auto base = index({{"roman.md", A}, {"notes.md", A}});

        const auto plan = io::planSync(base,
                                       index({{"roman.md", B}, {"notes.md", A}}),
                                       index({{"roman.md", A}, {"notes.md", C}}),
                                       io::SyncMode::ReadOnly);
        // Ce qui vient du serveur passe.
        assert(actionFor(plan, "notes.md") == std::string("réception"));
        // Ce qui devait partir ne part pas — mais est ANNONCÉ, pas
        // silencieusement abandonné.
        assert(actionFor(plan, "roman.md") == std::string("bloqué (lecture seule)"));
        assert(plan.count(io::SyncAction::Upload) == 0);
        assert(plan.count(io::SyncAction::Blocked) == 1);

        // Un conflit reste un conflit en lecture seule : on n'écrase
        // surtout pas le travail local par ce qui vient du serveur.
        const auto conflit = io::planSync(base, index({{"roman.md", B}}),
                                          index({{"roman.md", C}}), io::SyncMode::ReadOnly);
        assert(actionFor(conflit, "roman.md") == std::string("conflit"));
        std::cout << "Test 5 OK (lecture seule : recoit, n'envoie pas, le dit)\n";
    }

    // --- Test 6 : le plan est reproductible et chiffré ---
    {
        const auto base = index({{"a.md", A}});
        const auto local = index({{"a.md", B}, {"b.md", A}, {"c.md", A}});
        const auto remote = index({{"a.md", A}, {"d.md", C}});

        const auto plan = io::planSync(base, local, remote, io::SyncMode::TwoWay);
        // Trié par chemin : deux calculs donnent le même plan, donc un
        // affichage stable et des tests possibles.
        for (size_t i = 1; i < plan.steps.size(); ++i) {
            assert(plan.steps[i - 1].path < plan.steps[i].path);
        }
        assert(plan.count(io::SyncAction::Upload) == 3);   // a.md modifié, b.md et c.md neufs
        assert(plan.count(io::SyncAction::Download) == 1); // d.md
        assert(plan.bytesToTransfer() == 4 * 100);
        std::cout << "Test 6 OK (plan trie, compte et volume)\n";
    }

    // --- Test 7 : reprise après coupure ---
    {
        const auto base = index({{"a.md", A}, {"b.md", A}});
        const auto local = index({{"a.md", B}, {"b.md", B}});
        const auto remote = index({{"a.md", A}, {"b.md", A}});

        const auto plan = io::planSync(base, local, remote, io::SyncMode::TwoWay);
        assert(plan.count(io::SyncAction::Upload) == 2);

        // Le premier envoi aboutit, la connexion tombe avant le second.
        const auto nouvelleBase = io::advanceBase(base, local, remote, plan, {"a.md"});
        assert(nouvelleBase.find("a.md") != nullptr);
        assert(nouvelleBase.find("a.md")->sha256 == B); // acquis
        assert(nouvelleBase.find("b.md")->sha256 == A); // pas encore

        // Au tour suivant, le serveur a bien reçu a.md. Il ne reste que b.md.
        const auto apres = io::planSync(nouvelleBase, local,
                                        index({{"a.md", B}, {"b.md", A}}),
                                        io::SyncMode::TwoWay);
        assert(apres.steps.size() == 1);
        assert(actionFor(apres, "b.md") == std::string("envoi"));
        std::cout << "Test 7 OK (reprise exacte apres coupure)\n";
    }

    // --- Test 8 : la base enregistre aussi ce sur quoi tout le monde est d'accord ---
    {
        // Première synchronisation, fichier identique des deux côtés :
        // aucune action, mais il DOIT entrer dans la base. Sans cela, une
        // suppression distante ultérieure passerait pour un ajout local et
        // le fichier serait renvoyé — ressuscité contre la volonté de
        // l'auteur.
        const auto local = index({{"roman.md", A}});
        const auto plan = io::planSync(io::ProjectIndex{}, local, local, io::SyncMode::TwoWay);
        assert(plan.empty());

        const auto nouvelleBase = io::advanceBase(io::ProjectIndex{}, local, local, plan, {});
        assert(nouvelleBase.find("roman.md") != nullptr);

        const auto ensuite = io::planSync(nouvelleBase, local, index({}), io::SyncMode::TwoWay);
        assert(actionFor(ensuite, "roman.md") == std::string("suppression locale"));

        // Et un fichier disparu des deux côtés sort de la base.
        const auto videe = io::advanceBase(nouvelleBase, index({}), index({}),
                                           io::SyncPlan{}, {});
        assert(videe.files.empty());
        std::cout << "Test 8 OK (la base retient les accords, oublie les disparus)\n";
    }

    // --- Test 9 : cas dégénérés ---
    {
        // Tout vide : aucun plan, aucun plantage.
        assert(io::planSync(io::ProjectIndex{}, io::ProjectIndex{}, io::ProjectIndex{},
                            io::SyncMode::TwoWay).empty());

        // Projet entier neuf face à un serveur vide.
        const auto local = index({{"a.md", A}, {"b.md", B}});
        auto plan = io::planSync(io::ProjectIndex{}, local, io::ProjectIndex{},
                                 io::SyncMode::TwoWay);
        assert(plan.count(io::SyncAction::Upload) == 2);

        // Le même, en lecture seule : rien ne part, tout est annoncé.
        plan = io::planSync(io::ProjectIndex{}, local, io::ProjectIndex{},
                            io::SyncMode::ReadOnly);
        assert(plan.count(io::SyncAction::Blocked) == 2);

        // Une empreinte vide dans l'inventaire (fichier illisible au
        // moment de l'inventaire) équivaut à une absence : on ne bâtit pas
        // une décision sur rien.
        io::ProjectIndex abime;
        io::ProjectFile sansEmpreinte;
        sansEmpreinte.path = "a.md";
        abime.files.push_back(sansEmpreinte);
        plan = io::planSync(io::ProjectIndex{}, abime, io::ProjectIndex{},
                            io::SyncMode::TwoWay);
        assert(plan.empty());
        std::cout << "Test 9 OK (cas degeneres)\n";
    }

    std::cout << "Tous les tests de synchronisation sont passes.\n";
    return 0;
}
