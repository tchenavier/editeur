// test_roadmap.cpp
// Tests de la feuille de route (io/Roadmap).
//
// DEUX CHOSES SE JOUENT ICI.
//
// Le PLAN DE L'AUTEUR d'abord : c'est un texte qu'il a écrit et qui
// n'existe nulle part ailleurs. Un fichier abîmé ne doit jamais faire
// perdre le reste — perdre son plan parce qu'une ligne est fausse serait
// le pire résultat possible.
//
// Les ÉCHÉANCES ensuite. On ne peut pas les éprouver « en attendant » :
// l'instant courant est donc passé en argument, ce qui permet de se placer
// une minute avant une date limite, ou le lendemain, sans horloge.

#include "Roadmap.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

// Un instant repère, calculé par la fonction du module elle-même : si son
// calendrier était faux, les tests d'échéance ne voudraient rien dire —
// c'est pourquoi le test 4 le vérifie d'abord contre des dates connues.
int64_t instant(const char* texte) {
    int64_t secondes = 0;
    bool avecHeure = false;
    const bool ok = io::parseDeadline(texte, secondes, avecHeure);
    assert(ok);
    (void)ok;
    return secondes;
}

io::RoadmapPart partie(const char* id, const char* titre) {
    io::RoadmapPart p;
    p.id = id;
    p.title = titre;
    return p;
}

} // namespace

int main() {
    std::cout << std::unitbuf;

    // --- Test 1 : aller-retour complet ---
    {
        io::Roadmap plan;

        io::RoadmapPart acte = partie("p-001", "Acte I : Le Destin de Cendres");
        acte.summary = "Regina boit l'élixir.\n\nBlanche assiste, sans comprendre.";
        acte.state = io::PartState::InProgress;
        acte.deadline = "2026-09-15T18:00";
        acte.chapter = "Chapitre premier";
        acte.cards = {"regina", "blanche"};
        acte.extraLeadMinutes = 1440; // « préviens-moi aussi la veille »
        plan.parts.push_back(acte);

        io::RoadmapPart suite = partie("p-002", "Acte II");
        suite.summary = "La chasse.";
        plan.parts.push_back(suite);

        const io::Roadmap relu = io::parseRoadmap(io::serializeRoadmap(plan));
        assert(relu.parts.size() == 2);

        const io::RoadmapPart* premier = relu.find("p-001");
        assert(premier != nullptr);
        assert(premier->title == "Acte I : Le Destin de Cendres");
        // Le résumé garde ses sauts de ligne ET ses lignes vides : c'est
        // du texte d'auteur, pas une étiquette.
        assert(premier->summary == "Regina boit l'élixir.\n\nBlanche assiste, sans comprendre.");
        assert(premier->state == io::PartState::InProgress);
        assert(premier->deadline == "2026-09-15T18:00");
        assert(premier->chapter == "Chapitre premier");
        assert(premier->cards.size() == 2 && premier->cards[0] == "regina");
        assert(premier->extraLeadMinutes == 1440);

        // L'ORDRE est celui de l'auteur, pas un tri : c'est son plan.
        assert(relu.parts[0].id == "p-001");
        assert(relu.parts[1].id == "p-002");
        std::cout << "Test 1 OK (aller-retour, resume multi-ligne, ordre garde)\n";
    }

    // --- Test 2 : un fichier abîmé ne fait pas perdre le reste ---
    {
        // Trois blocs : un bon, un sans identifiant, un bon. Le mauvais
        // est ignoré, les deux autres survivent.
        const std::string texte =
            "# Feuille de route\n"
            "\n---\n"
            "id: \"p-1\"\n"
            "titre: \"Bon\"\n"
            "\n---\n"
            "titre: \"Sans identifiant, donc rien ne peut le designer\"\n"
            "\n---\n"
            "id: \"p-2\"\n"
            "titre: \"Bon aussi\"\n"
            "etat: \"n-importe-quoi\"\n";

        const io::Roadmap plan = io::parseRoadmap(texte);
        assert(plan.parts.size() == 2);
        assert(plan.find("p-1") != nullptr);
        assert(plan.find("p-2") != nullptr);
        // Un état inconnu ne fait pas perdre la partie : on retombe sur
        // « prévu » plutôt que d'inventer.
        assert(plan.find("p-2")->state == io::PartState::Planned);

        // Contenus vraiment hostiles : aucun plantage, aucun résultat faux.
        assert(io::parseRoadmap("").parts.empty());
        assert(io::parseRoadmap("---\n---\n---\n").parts.empty());
        assert(io::parseRoadmap(std::string(5000, '\x01')).parts.empty());
        assert(io::parseRoadmap("id: sans-separateur\n").parts.empty());
        std::cout << "Test 2 OK (bloc abime ignore, le reste garde)\n";
    }

    // --- Test 3 : réorganiser ---
    {
        io::Roadmap plan;
        plan.parts.push_back(partie("a", "A"));
        plan.parts.push_back(partie("b", "B"));
        plan.parts.push_back(partie("c", "C"));

        assert(plan.move("b", -1));
        assert(plan.parts[0].id == "b" && plan.parts[1].id == "a");

        // Aux extrémités : sans effet, et surtout sans perte.
        assert(!plan.move("b", -1));
        assert(!plan.move("c", 1));
        assert(!plan.move("inconnu", 1));
        assert(plan.parts.size() == 3);
        std::cout << "Test 3 OK (reorganisation sans perte)\n";
    }

    // --- Test 4 : le calendrier, avant de s'en servir ---
    {
        int64_t secondes = 0;
        bool avecHeure = true;

        // Repère absolu : le 1er janvier 1970 vaut zéro.
        assert(io::parseDeadline("1970-01-01", secondes, avecHeure));
        assert(secondes == 0 && !avecHeure);

        // Un jour plus tard, exactement 86 400 secondes.
        assert(io::parseDeadline("1970-01-02", secondes, avecHeure));
        assert(secondes == 86400);

        // Avec heure.
        assert(io::parseDeadline("1970-01-01T12:30", secondes, avecHeure));
        assert(secondes == 12 * 3600 + 30 * 60 && avecHeure);

        // Les années bissextiles comptent : 2024 en est une, 2026 non.
        assert(io::parseDeadline("2024-02-29", secondes, avecHeure));
        assert(!io::parseDeadline("2026-02-29", secondes, avecHeure));

        // Et tout ce qui n'est pas une échéance est refusé plutôt
        // qu'interprété de travers.
        const char* const mauvaises[] = {
            "", "2026", "2026-13-01", "2026-00-10", "2026-04-31",
            "2026-01-01T24:00", "2026-01-01T12:60", "2026-01-01 12:00",
            "hier", "2026/01/01", "1899-01-01", "3001-01-01",
        };
        for (const char* texte : mauvaises) {
            assert(!io::parseDeadline(texte, secondes, avecHeure));
        }
        std::cout << "Test 4 OK (calendrier verifie contre des dates connues)\n";
    }

    // --- Test 5 : LE comportement demandé — prévenir à l'approche ---
    {
        io::RoadmapPart p = partie("p", "Rendre le manuscrit");
        p.deadline = "2026-09-15T18:00";
        const int64_t echeance = instant("2026-09-15T18:00");
        const uint32_t preavis = 60; // une heure, le réglage par défaut

        // Deux heures avant : encore loin, on ne dérange pas.
        assert(io::dueStateOf(p, echeance - 2 * 3600, preavis) == io::DueState::Far);
        // Une heure pile avant : c'est le moment.
        assert(io::dueStateOf(p, echeance - 3600, preavis) == io::DueState::Soon);
        // Une minute avant : toujours.
        assert(io::dueStateOf(p, echeance - 60, preavis) == io::DueState::Soon);
        // Après : en retard.
        assert(io::dueStateOf(p, echeance + 60, preavis) == io::DueState::Late);

        // Le préavis est paramétrable : à deux heures, on prévient plus tôt.
        assert(io::dueStateOf(p, echeance - 2 * 3600 + 60, 120) == io::DueState::Soon);

        // UNE PARTIE TERMINÉE NE RELANCE PLUS PERSONNE, même en retard :
        // le travail est fait, la date n'a plus rien à réclamer.
        p.state = io::PartState::Done;
        assert(io::dueStateOf(p, echeance + 86400, preavis) == io::DueState::None);
        std::cout << "Test 5 OK (preavis d'une heure, parametrable)\n";
    }

    // --- Test 6 : une échéance sans heure est un JOUR ---
    {
        io::RoadmapPart p = partie("p", "Relecture");
        p.deadline = "2026-09-15";
        const int64_t jour = instant("2026-09-15");

        // « Une heure avant le 15 septembre » ne veut rien dire : on ne
        // devine pas une heure de rendu. On prévient le jour même.
        assert(io::dueStateOf(p, jour - 3600, 60) == io::DueState::Far);
        assert(io::dueStateOf(p, jour, 60) == io::DueState::Today);
        assert(io::dueStateOf(p, jour + 12 * 3600, 60) == io::DueState::Today);
        assert(io::dueStateOf(p, jour + 86400, 60) == io::DueState::Late);

        // Sans échéance du tout : rien à dire.
        io::RoadmapPart libre = partie("q", "Idée de scène");
        assert(io::dueStateOf(libre, jour, 60) == io::DueState::None);
        std::cout << "Test 6 OK (echeance sans heure = le jour meme)\n";
    }

    // --- Test 7 : le rappel supplémentaire ---
    {
        io::RoadmapPart p = partie("p", "Rendre le manuscrit");
        p.deadline = "2026-09-15T18:00";
        p.extraLeadMinutes = 1440; // la veille
        const int64_t echeance = instant("2026-09-15T18:00");

        assert(!io::extraReminderDue(p, echeance - 2 * 86400)); // deux jours avant : non
        assert(io::extraReminderDue(p, echeance - 86400));      // la veille : oui
        assert(io::extraReminderDue(p, echeance - 3600));       // et après aussi

        // Sans rappel supplémentaire demandé, jamais.
        p.extraLeadMinutes = 0;
        assert(!io::extraReminderDue(p, echeance - 60));

        // Terminée : plus rien.
        p.extraLeadMinutes = 1440;
        p.state = io::PartState::Done;
        assert(!io::extraReminderDue(p, echeance - 3600));
        std::cout << "Test 7 OK (rappel supplementaire, independant du preavis)\n";
    }

    // --- Test 8 : sur disque, à côté du manuscrit ---
    {
        const fs::path dossier = fs::temp_directory_path() / "test_roadmap";
        std::error_code ec;
        fs::remove_all(dossier, ec);
        fs::create_directories(dossier, ec);
        const fs::path manuscrit = dossier / "roman.md";

        // Le fichier voisine le manuscrit : il voyage donc avec le projet,
        // se sauvegarde avec lui et se synchronise avec lui.
        const fs::path attendu = dossier / "roman_suivi.md";
        assert(io::roadmapPathFor(manuscrit) == attendu);

        // Absent : feuille de route vide, et surtout pas une erreur.
        assert(io::loadRoadmap(manuscrit).parts.empty());

        io::Roadmap plan;
        io::RoadmapPart p = partie("p-001", "Acte I");
        p.summary = "Un résumé accentué : été, forêt.";
        p.deadline = "2026-09-15";
        plan.parts.push_back(p);

        std::string erreur;
        assert(io::saveRoadmap(plan, manuscrit, &erreur));
        assert(fs::exists(attendu));

        const io::Roadmap relu = io::loadRoadmap(manuscrit);
        assert(relu.parts.size() == 1);
        assert(relu.parts[0].summary == "Un résumé accentué : été, forêt.");

        // Aucun fichier temporaire ne subsiste après une écriture réussie.
        assert(!fs::exists(fs::path(attendu.string() + ".tmp")));

        // Sans manuscrit enregistré, on refuse en le disant.
        assert(!io::saveRoadmap(plan, fs::path(), &erreur));
        assert(!erreur.empty());
        assert(io::roadmapPathFor(fs::path()).empty());

        fs::remove_all(dossier, ec);
        std::cout << "Test 8 OK (fichier voisin du manuscrit, ecriture atomique)\n";
    }

    std::cout << "Tous les tests de feuille de route sont passes.\n";
    return 0;
}
