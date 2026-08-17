// test_project.cpp
// Tests de l'inventaire de projet (io/ProjectIndex) et du journal des
// modifications (io/ChangeLog) — jalon A de la Phase 5.
//
// Deux exigences dominent ces tests :
//   - c'est l'EMPREINTE qui décide qu'un fichier a changé, jamais la date
//     seule (réenregistrer un fichier identique ne doit rien déclencher) ;
//   - un fichier technique abîmé ne doit jamais empêcher d'ouvrir le
//     projet : les analyseurs ignorent les lignes fausses et gardent le
//     reste.

#include "ChangeLog.h"
#include "Pairing.h"
#include "ProjectIndex.h"
#include "Sha256.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

// Écrit un fichier (en créant les dossiers) et referme aussitôt : sous
// Windows, un flux resté ouvert empêcherait la suppression du dossier.
void writeFile(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

const io::ProjectFile* fileAt(const io::ProjectIndex& index, const std::string& path) {
    return index.find(path);
}

bool hasChange(const std::vector<io::Change>& changes, io::ChangeKind kind,
               const std::string& path) {
    for (const io::Change& change : changes) {
        if (change.kind == kind && change.path == path) return true;
    }
    return false;
}

} // namespace

int main() {
    std::cout << std::unitbuf;

    const fs::path racine = fs::temp_directory_path() / "test_editeur_projet";
    // Dossier de brouillon SÉPARÉ, hors du projet : y écrire des fichiers
    // d'essai reviendrait sinon à les ajouter au projet, et l'inventaire
    // les compterait — à juste titre.
    const fs::path brouillon = fs::temp_directory_path() / "test_editeur_brouillon";
    std::error_code ec;
    fs::remove_all(racine, ec);
    fs::remove_all(brouillon, ec);
    fs::create_directories(brouillon, ec);
    const fs::path manuscrit = racine / "mon-roman.md";

    // --- Test 1 : périmètre de l'inventaire ---
    {
        writeFile(manuscrit, "---\ntitle: \"Roman\"\n---\n\n# Chapitre\n\nTexte.\n");
        writeFile(racine / "mon-roman_fiches" / "personnage" / "elise.md",
                  "---\nid: elise\ntitre: \"Élise\"\n---\n\nNotes.\n");
        writeFile(racine / "images" / "couverture.png", "\x89PNG\r\n\x1a\n fausse image");
        // Doivent être EXCLUS : sauvegardes horodatées et dossier technique.
        writeFile(racine / "mon-roman_backups" / "mon-roman_2026-07-26_10-00-00.md", "ancien");
        writeFile(racine / "mon-roman_sync" / "inventaire.txt", "# ancien inventaire");
        writeFile(racine / ".cache" / "truc", "masque");

        size_t ignores = 0;
        const io::ProjectIndex index = io::buildIndex(manuscrit, "id-de-test", &ignores);

        assert(index.projectId == "id-de-test");
        assert(fileAt(index, "mon-roman.md") != nullptr);
        assert(fileAt(index, "mon-roman_fiches/personnage/elise.md") != nullptr);
        assert(fileAt(index, "images/couverture.png") != nullptr);
        assert(index.files.size() == 3);
        // Les dossiers techniques et masqués ne sont pas inventoriés.
        assert(fileAt(index, "mon-roman_backups/mon-roman_2026-07-26_10-00-00.md") == nullptr);
        assert(fileAt(index, "mon-roman_sync/inventaire.txt") == nullptr);

        // Séparateur '/' partout, et empreinte égale à celle du contenu.
        for (const io::ProjectFile& file : index.files) {
            assert(file.path.find('\\') == std::string::npos);
            assert(file.sha256.size() == 64);
            assert(!file.modToken.empty());
        }
        const io::ProjectFile* md = fileAt(index, "mon-roman.md");
        assert(md->sha256 == utils::sha256Hex(readFile(manuscrit)));
        assert(md->size == readFile(manuscrit).size());

        // Triés par chemin : la comparaison de deux inventaires en dépend.
        for (size_t i = 1; i < index.files.size(); ++i) {
            assert(index.files[i - 1].path < index.files[i].path);
        }
        std::cout << "Test 1 OK (perimetre et contenu de l'inventaire)\n";
    }

    // --- Test 2 : aller-retour de sérialisation, lecture tolérante ---
    {
        io::ProjectIndex index;
        index.projectId = "550e8400-e29b-41d4-a716-446655440000";
        index.files.push_back({"mon-roman.md", 1234, "132000000", std::string(64, 'a')});
        // Un chemin AVEC ESPACES doit survivre à l'aller-retour : c'est
        // pour cela qu'il est écrit en dernier sur la ligne.
        index.files.push_back({"images/ma couverture finale.png", 9, "132000001",
                               std::string(64, 'b')});

        const io::ProjectIndex relu = io::parseIndex(io::serializeIndex(index));
        assert(relu.projectId == index.projectId);
        assert(relu.files.size() == 2);
        assert(fileAt(relu, "images/ma couverture finale.png") != nullptr);
        assert(fileAt(relu, "images/ma couverture finale.png")->size == 9);
        assert(fileAt(relu, "mon-roman.md")->modToken == "132000000");

        // Lecture tolérante : chaque ligne fausse est ignorée, les bonnes
        // restent. Un inventaire abîmé ne doit jamais bloquer un projet.
        const std::string abime =
            "# inventaire de projet 1\n"
            "projet 550e8400-e29b-41d4-a716-446655440000\n"
            "f " + std::string(64, 'c') + " 10 132000002 bon/fichier.md\n"
            "f trop-court 10 132000003 mauvaise/empreinte.md\n"
            "f " + std::string(64, 'd') + " pasunnombre 132000004 mauvaise/taille.md\n"
            "f " + std::string(64, 'e') + " 10 132000005 ../evasion.md\n"
            "ligne totalement inconnue\n"
            "f " + std::string(64, 'f') + " 20 132000006 autre/bon.md\n";
        const io::ProjectIndex tolere = io::parseIndex(abime);
        assert(tolere.files.size() == 2);
        assert(fileAt(tolere, "bon/fichier.md") != nullptr);
        assert(fileAt(tolere, "autre/bon.md") != nullptr);
        assert(fileAt(tolere, "../evasion.md") == nullptr); // remontée refusée
        std::cout << "Test 2 OK (serialisation, lecture tolerante)\n";
    }

    // --- Test 3 : comparaison de deux inventaires ---
    {
        io::ProjectIndex avant;
        avant.files.push_back({"a.md", 10, "100", std::string(64, '1')});
        avant.files.push_back({"b.md", 20, "200", std::string(64, '2')});
        avant.files.push_back({"c.md", 30, "300", std::string(64, '3')});

        io::ProjectIndex apres;
        // a.md : contenu modifié (empreinte différente).
        apres.files.push_back({"a.md", 11, "999", std::string(64, '9')});
        // b.md : RÉENREGISTRÉ À L'IDENTIQUE — date changée, contenu non.
        apres.files.push_back({"b.md", 20, "555", std::string(64, '2')});
        // c.md : supprimé. d.md : ajouté.
        apres.files.push_back({"d.md", 40, "400", std::string(64, '4')});

        const std::vector<io::Change> changements = io::diffIndex(avant, apres);
        assert(hasChange(changements, io::ChangeKind::Modified, "a.md"));
        assert(hasChange(changements, io::ChangeKind::Removed, "c.md"));
        assert(hasChange(changements, io::ChangeKind::Added, "d.md"));
        // LE point du test : une date qui change sans que le contenu change
        // n'est PAS une modification.
        assert(!hasChange(changements, io::ChangeKind::Modified, "b.md"));
        assert(changements.size() == 3);

        // Deux inventaires identiques ne produisent aucun changement.
        assert(io::diffIndex(avant, avant).empty());
        // Tout ajouter / tout supprimer.
        assert(io::diffIndex(io::ProjectIndex{}, apres).size() == 3);
        assert(io::diffIndex(apres, io::ProjectIndex{}).size() == 3);
        std::cout << "Test 3 OK (comparaison : l'empreinte tranche, pas la date)\n";
    }

    // --- Test 4 : inventaire sur disque ---
    {
        const io::ProjectIndex index = io::buildIndex(manuscrit, "id-de-test");
        const fs::path fichier = io::indexPathFor(manuscrit);
        // Emplacement : un dossier voisin, nommé d'après le manuscrit.
        assert(fichier.parent_path() == io::syncFolderFor(manuscrit));
        assert(io::syncFolderFor(manuscrit).filename() == "mon-roman_sync");

        std::string erreur;
        assert(io::saveIndex(index, fichier, &erreur));
        assert(erreur.empty());
        assert(fs::exists(fichier));
        fs::path tmp = fichier;
        tmp += ".tmp";
        assert(!fs::exists(tmp)); // aucun temporaire laissé derrière

        io::ProjectIndex relu;
        assert(io::loadIndex(relu, fichier, &erreur));
        assert(relu.projectId == index.projectId);
        assert(relu.files.size() == index.files.size());
        assert(io::diffIndex(index, relu).empty()); // strictement identique

        // Fichier absent : échec propre, message renseigné.
        io::ProjectIndex vide;
        assert(!io::loadIndex(vide, brouillon / "absent_zzz.txt", &erreur));
        assert(!erreur.empty());
        std::cout << "Test 4 OK (inventaire ecrit et relu sur disque)\n";
    }

    // --- Test 5 : journal des modifications ---
    {
        const fs::path journal = io::journalPathFor(manuscrit);
        fs::remove(journal, ec);

        std::vector<io::Change> lot1;
        lot1.push_back({io::ChangeKind::Added, "mon-roman.md", std::string(64, '1'), 100});
        lot1.push_back({io::ChangeKind::Added, "images/ma couverture.png",
                        std::string(64, '2'), 200});
        std::string erreur;
        assert(io::appendChanges(journal, lot1, &erreur));

        std::vector<io::Change> lot2;
        lot2.push_back({io::ChangeKind::Modified, "mon-roman.md", std::string(64, '3'), 150});
        lot2.push_back({io::ChangeKind::Removed, "images/ma couverture.png", "", 0});
        assert(io::appendChanges(journal, lot2, &erreur));

        const std::vector<io::JournalEntry> entrees = io::readJournal(journal);
        assert(entrees.size() == 4);
        assert(entrees[0].kind == io::ChangeKind::Added);
        assert(entrees[0].path == "mon-roman.md");
        assert(entrees[1].path == "images/ma couverture.png"); // espace conservé
        assert(entrees[2].kind == io::ChangeKind::Modified);
        assert(entrees[3].kind == io::ChangeKind::Removed);
        assert(entrees[3].sha256.empty()); // rien après une suppression
        // Horodatage : « AAAA-MM-JJTHH:MM:SSZ ».
        assert(entrees[0].timestamp.size() == 20);
        assert(entrees[0].timestamp[4] == '-' && entrees[0].timestamp[10] == 'T');
        assert(entrees[0].timestamp.back() == 'Z');

        // Une liste vide n'écrit rien et n'est pas un échec.
        assert(io::appendChanges(journal, {}, &erreur));
        assert(io::readJournal(journal).size() == 4);
        std::cout << "Test 5 OK (journal : ajout et relecture)\n";
    }

    // --- Test 6 : journal abîmé (coupure d'écriture, bricolage) ---
    {
        const fs::path journal = brouillon / "journal_abime.txt";
        writeFile(journal,
                  "2026-07-26T10:00:00Z ajout " + std::string(64, 'a') + " 10 bon1.md\n"
                  "ligne sans structure\n"
                  "2026-07-26T10:00:01Z action_inconnue " + std::string(64, 'b') + " 10 x.md\n"
                  "pasunedate ajout " + std::string(64, 'c') + " 10 y.md\n"
                  "2026-07-26T10:00:02Z modif " + std::string(64, 'd') + " 20 bon2.md\n"
                  // Dernière ligne TRONQUÉE (coupure pendant l'écriture) :
                  // elle n'a pas de retour à la ligne final.
                  "2026-07-26T10:00:03Z ajout " + std::string(64, 'e') + " 30 tronq");

        const std::vector<io::JournalEntry> entrees = io::readJournal(journal);
        assert(entrees.size() == 2);
        assert(entrees[0].path == "bon1.md");
        assert(entrees[1].path == "bon2.md");

        // Journal inexistant : liste vide, pas d'erreur.
        assert(io::readJournal(brouillon / "jamais_ecrit.txt").empty());
        std::cout << "Test 6 OK (journal abime : lignes fausses ignorees)\n";
    }

    // --- Test 7 : compactage ---
    {
        const fs::path journal = brouillon / "journal_long.txt";
        fs::remove(journal, ec);
        for (int i = 0; i < 50; ++i) {
            std::vector<io::Change> lot;
            lot.push_back({io::ChangeKind::Modified, "fichier" + std::to_string(i) + ".md",
                           std::string(64, 'a'), static_cast<uint64_t>(i)});
            assert(io::appendChanges(journal, lot));
        }
        assert(io::readJournal(journal).size() == 50);

        std::string erreur;
        assert(io::compactJournal(journal, 10, &erreur) == 40);
        const std::vector<io::JournalEntry> restantes = io::readJournal(journal);
        assert(restantes.size() == 10);
        // Ce sont les DERNIÈRES qui restent (les plus récentes).
        assert(restantes.front().path == "fichier40.md");
        assert(restantes.back().path == "fichier49.md");
        fs::path tmp = journal;
        tmp += ".tmp";
        assert(!fs::exists(tmp));

        // Journal déjà assez court : rien à faire.
        assert(io::compactJournal(journal, 100, &erreur) == 0);
        assert(io::readJournal(journal).size() == 10);
        std::cout << "Test 7 OK (compactage du journal)\n";
    }

    // --- Test 8 : cycle complet, tel qu'il tournera à chaque sauvegarde ---
    {
        const fs::path fichierIndex = io::indexPathFor(manuscrit);
        const fs::path journal = io::journalPathFor(manuscrit);
        fs::remove(journal, ec);

        io::ProjectIndex precedent;
        io::loadIndex(precedent, fichierIndex);

        // L'auteur modifie son manuscrit et ajoute une fiche.
        writeFile(manuscrit, "---\ntitle: \"Roman\"\n---\n\n# Chapitre\n\nTexte allongé.\n");
        writeFile(racine / "mon-roman_fiches" / "lieu" / "foret.md",
                  "---\nid: foret\ntitre: \"La Forêt\"\n---\n");

        const io::ProjectIndex courant = io::buildIndex(manuscrit, precedent.projectId);
        const std::vector<io::Change> changements = io::diffIndex(precedent, courant);
        assert(hasChange(changements, io::ChangeKind::Modified, "mon-roman.md"));
        assert(hasChange(changements, io::ChangeKind::Added,
                         "mon-roman_fiches/lieu/foret.md"));
        assert(changements.size() == 2);

        assert(io::appendChanges(journal, changements));
        assert(io::saveIndex(courant, fichierIndex));

        // Deuxième passage SANS rien modifier : aucun changement, aucune
        // ligne ajoutée au journal. C'est la garantie qu'une sauvegarde
        // sans modification ne fabrique pas de faux mouvement.
        io::ProjectIndex relu;
        assert(io::loadIndex(relu, fichierIndex));
        const io::ProjectIndex encore = io::buildIndex(manuscrit, relu.projectId);
        assert(io::diffIndex(relu, encore).empty());
        assert(io::readJournal(journal).size() == 2);
        std::cout << "Test 8 OK (cycle complet inventaire + journal)\n";
    }

    // --- Test 9 : appairage du projet (jalon C) ---
    {
        // Nom distinct du 'manuscrit' des tests précédents (portée fonction) :
        // une variable de même nom dans ce bloc le masquerait (avertissement
        // C4456), ce que le projet bannit partout ailleurs.
        const fs::path manuscritAppairage = racine / "appairage" / "roman.md";
        fs::create_directories(manuscritAppairage.parent_path(), ec);
        {
            std::ofstream out(manuscritAppairage, std::ios::binary);
            out << "---\ntitle: \"Roman\"\n---\n\nTexte.\n";
        }

        // Jamais répondu : la question peut être posée.
        io::Pairing initial = io::loadPairing(manuscritAppairage);
        assert(initial.state == io::PairingState::None);
        assert(initial.canPropose());
        assert(!initial.isPaired());
        assert(!initial.autoSync); // la synchro automatique est désactivée par défaut

        // Appairage, puis relecture depuis le disque.
        io::Pairing appaire;
        appaire.state = io::PairingState::Paired;
        appaire.mode = io::PairingMode::ReadOnly;
        appaire.server = "https://exemple.fr";
        std::string err;
        assert(io::savePairing(appaire, manuscritAppairage, &err));

        const io::Pairing relu = io::loadPairing(manuscritAppairage);
        assert(relu.isPaired());
        assert(relu.server == "https://exemple.fr");
        assert(relu.mode == io::PairingMode::ReadOnly);
        assert(!relu.canPropose());

        // « Jamais » se retient : c'est ce qui empêche de reposer la
        // question à chaque lancement jusqu'à obtenir un « oui » de
        // lassitude.
        io::Pairing jamais;
        jamais.state = io::PairingState::Never;
        assert(io::savePairing(jamais, manuscritAppairage, &err));
        assert(io::loadPairing(manuscritAppairage).state == io::PairingState::Never);
        assert(!io::loadPairing(manuscritAppairage).canPropose());

        // Lecture tolérante : fichier abîmé, absent, ou incohérent.
        assert(io::parsePairing("").state == io::PairingState::None);
        assert(io::parsePairing("\x01\x02 n'importe quoi\n").state == io::PairingState::None);
        // Appairé sans serveur : la promesse est intenable, on revient à
        // « aucun » plutôt que d'annoncer une synchronisation impossible.
        assert(io::parsePairing("etat appaire\n").state == io::PairingState::None);
        assert(io::loadPairing(racine / "nulle_part" / "absent.md").state ==
               io::PairingState::None);

        // Aucun secret n'est écrit dans ce fichier : il doit pouvoir être
        // lu par n'importe qui sans conséquence.
        const std::string contenu = io::serializePairing(relu);
        assert(contenu.find("token") == std::string::npos);
        assert(contenu.find("secret") == std::string::npos);
        assert(contenu.find("Bearer") == std::string::npos);

        // Un manuscrit jamais enregistré ne peut pas être appairé : il n'y
        // a nulle part où ranger la décision.
        assert(!io::savePairing(appaire, fs::path(), &err));
        assert(!err.empty());

        std::cout << "Test 9 OK (appairage : trois etats, lecture tolerante)\n";
    }

    fs::remove_all(racine, ec);
    std::cout << "Tous les tests projet sont passes.\n";
    return 0;
}
