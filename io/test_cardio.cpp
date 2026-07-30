// test_cardio.cpp
// Tests de la sérialisation des fiches (io/Yaml + io/CardIO).

#include "CardIO.h"
#include "CardModel.h"
#include "PathUtf8.h"
#include "Yaml.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using core::Card;
using core::CardLibrary;
using io::YamlBlock;

int main() {
    // --- Test 1 : YAML, aller-retour des trois formes ---
    {
        YamlBlock block;
        block.setScalar("titre", "Aria de Val");
        block.setList("tags", {"noblesse", "chapitre-1"});
        block.setScalar("description", "Grande, cheveux sombres.\nUne cicatrice.");

        const std::string text = block.toString();
        // Forme attendue : scalaire quoté, liste à tirets, bloc « | ».
        assert(text.find("titre: \"Aria de Val\"") != std::string::npos);
        assert(text.find("tags:\n  - \"noblesse\"\n  - \"chapitre-1\"") != std::string::npos);
        assert(text.find("description: |\n  Grande, cheveux sombres.\n  Une cicatrice.") != std::string::npos);

        const YamlBlock relu = YamlBlock::parse(io::splitLines(text));
        assert(relu.scalar("titre") == "Aria de Val");
        assert(relu.get("tags")->size() == 2);
        assert((*relu.get("tags"))[1] == "chapitre-1");
        assert(relu.scalar("description") == "Grande, cheveux sombres.\nUne cicatrice.");
        std::cout << "Test 1 OK (YAML : scalaire, liste, bloc multi-ligne)\n";
    }

    // --- Test 2 : l'ordre des clés est conservé ---
    {
        YamlBlock block;
        block.setScalar("zebre", "z");
        block.setScalar("alpha", "a");
        block.setScalar("milieu", "m");
        const auto relu = YamlBlock::parse(io::splitLines(block.toString()));
        assert(relu.entries().size() == 3);
        assert(relu.entries()[0].key == "zebre");   // pas de tri alphabétique
        assert(relu.entries()[2].key == "milieu");
        std::cout << "Test 2 OK (ordre des cles preserve)\n";
    }

    // --- Test 3 : caractères délicats et YAML écrit à la main ---
    {
        YamlBlock block;
        block.setScalar("piege", "un \"guillemet\", deux points : et un \\ antislash");
        const auto relu = YamlBlock::parse(io::splitLines(block.toString()));
        assert(relu.scalar("piege") == "un \"guillemet\", deux points : et un \\ antislash");

        // Fichier tapé à la main : valeurs non quotées, commentaire, ligne vide.
        const std::vector<std::string> manuel = {
            "# un commentaire", "titre: Sans guillemets", "", "tags:", "  - alpha", "  - beta",
            "ligne bizarre sans deux-points",
        };
        const YamlBlock lu = YamlBlock::parse(manuel);
        assert(lu.scalar("titre") == "Sans guillemets");
        assert(lu.get("tags")->size() == 2);
        std::cout << "Test 3 OK (echappements et YAML manuel)\n";
    }

    // --- Test 4 : aller-retour complet d'une fiche ---
    {
        Card card("aria", "personnage");
        card.setTitle("Aria de Val");
        card.addTag("noblesse");
        card.addTag("chapitre-1");
        card.editField("age").setScalar("24 ans");
        card.editField("description_physique").setScalar("Grande.\nCheveux sombres.");
        card.editField("relations").setValues({"bran", "elise"});
        card.editField(io::kCardNotesField).setScalar("Notes libres en **Markdown**.");

        const std::string text = io::serializeCard(card);
        assert(text.rfind("---\n", 0) == 0);                       // commence par l'en-tête
        assert(text.find("id: \"aria\"") != std::string::npos);
        assert(text.find("Notes libres") != std::string::npos);     // corps après l'en-tête

        Card relue;
        assert(io::parseCard(text, relue));
        assert(relue.id() == "aria");
        assert(relue.typeId() == "personnage");
        assert(relue.title() == "Aria de Val");
        assert(relue.tags().size() == 2 && relue.hasTag("noblesse"));
        assert(relue.field("age")->scalar() == "24 ans");
        assert(relue.field("description_physique")->scalar() == "Grande.\nCheveux sombres.");
        assert(relue.field("relations")->size() == 2);
        assert(relue.field("relations")->contains("elise"));
        assert(relue.field(io::kCardNotesField)->scalar() == "Notes libres en **Markdown**.");
        std::cout << "Test 4 OK (aller-retour d'une fiche)\n";
    }

    // --- Test 5 : entrées dégradées, jamais de plantage ---
    {
        Card c;
        assert(!io::parseCard("", c));                       // vide
        assert(!io::parseCard("juste du texte", c));         // pas d'en-tête
        assert(!io::parseCard("---\ntitre: X\n---\n", c));   // en-tête sans id
        assert(!io::parseCard("---\nid: X\n", c));           // en-tête non fermé

        // Type absent : accepté (la fiche reste exploitable, rangée dans « divers »).
        Card sansType;
        assert(io::parseCard("---\nid: \"x\"\n---\n", sansType));
        assert(sansType.typeId().empty());
        std::cout << "Test 5 OK (entrees degradees)\n";
    }

    // --- Test 6 : slugify (identifiants lisibles, sans accents) ---
    {
        assert(io::slugify("Aria de Val") == "aria-de-val");
        assert(io::slugify("Élise & Cie !") == "elise-cie");
        assert(io::slugify("  Château   fort  ") == "chateau-fort");
        assert(io::slugify("") == "fiche");
        assert(io::slugify("!!!") == "fiche");     // rien d'exploitable
        std::cout << "Test 6 OK (slugify)\n";
    }

    // --- Test 7 : disque — rangement par type, aller-retour ---
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "test_editeur_fiches";
        std::error_code ec;
        fs::remove_all(root, ec);

        CardLibrary lib;
        Card aria("aria", "personnage");
        aria.setTitle("Aria de Val");
        aria.editField("age").setScalar("24 ans");
        lib.put(std::move(aria));

        Card tour("tour-de-brume", "lieu");
        tour.setTitle("Tour de Brume");
        tour.addTag("nord");
        lib.put(std::move(tour));

        std::string err;
        assert(io::saveLibrary(lib, root, &err));
        // Rangement lisible : un dossier par type.
        assert(fs::exists(root / "personnage" / "aria.md"));
        assert(fs::exists(root / "lieu" / "tour-de-brume.md"));

        CardLibrary relue;
        size_t ignores = 0;
        assert(io::loadLibrary(relue, root, &ignores, &err));
        assert(ignores == 0);
        assert(relue.size() == 2);
        assert(relue.find("aria")->title() == "Aria de Val");
        assert(relue.find("aria")->field("age")->scalar() == "24 ans");
        assert(relue.find("tour-de-brume")->hasTag("nord"));

        // Un fichier abîmé ne doit pas empêcher de charger les autres.
        std::ofstream(root / "personnage" / "casse.md") << "ceci n'est pas une fiche";
        CardLibrary partielle;
        assert(io::loadLibrary(partielle, root, &ignores, &err));
        assert(ignores == 1);
        assert(partielle.size() == 2); // les deux fiches valides sont là

        // Suppression du fichier d'une fiche.
        assert(io::removeCardFile(*relue.find("aria"), root, &err));
        assert(!fs::exists(root / "personnage" / "aria.md"));

        // Dossier inexistant : ce n'est pas une erreur (bible pas encore créée).
        CardLibrary vide;
        assert(io::loadLibrary(vide, root / "nulle-part", &ignores, &err));
        assert(vide.size() == 0);

        fs::remove_all(root, ec);
        std::cout << "Test 7 OK (disque : rangement, chargement, fichier abime)\n";
    }

    // --- Test 8 : identifiants et titres accentués sur disque ---
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "test_editeur_fiches_accents";
        std::error_code ec;
        fs::remove_all(root, ec);

        Card c("elise-de-val", "personnage");
        c.setTitle("Élise de Val");
        c.editField("historique").setScalar("Née à Brume.\nPuis exilée.");

        std::string err;
        assert(io::saveCard(c, root, &err));

        CardLibrary lib;
        assert(io::loadLibrary(lib, root, nullptr, &err));
        assert(lib.size() == 1);
        const Card* relue = lib.find("elise-de-val");
        assert(relue != nullptr);
        assert(relue->title() == "Élise de Val"); // accents intacts
        assert(relue->field("historique")->scalar() == "Née à Brume.\nPuis exilée.");

        fs::remove_all(root, ec);
        std::cout << "Test 8 OK (accents preserves sur disque)\n";
    }

    // --- Test 9 : forme STABLE des champs de type liste ---
    // Un champ que le schéma déclare « liste » doit garder la forme à
    // tirets même avec une seule valeur : ajouter un élément ne doit pas
    // bouleverser la structure du fichier (différences lisibles).
    {
        core::SchemaRegistry reg;
        Card c("aria", "personnage");
        c.editField("illustrations").setScalar("images/aria.png"); // une seule valeur

        // Sans schéma : forme compacte (scalaire).
        const std::string sansSchema = io::serializeCard(c);
        assert(sansSchema.find("illustrations: \"images/aria.png\"") != std::string::npos);

        // Avec schéma : forme liste, car « illustrations » est un ImageList.
        const std::string avecSchema = io::serializeCard(c, &reg);
        assert(avecSchema.find("illustrations:\n  - \"images/aria.png\"") != std::string::npos);

        // Ajouter une 2e image ne change QUE la ligne ajoutée.
        Card c2 = c;
        c2.editField("illustrations").add("images/aria2.png");
        const std::string deux = io::serializeCard(c2, &reg);
        assert(deux.find("illustrations:\n  - \"images/aria.png\"\n  - \"images/aria2.png\"") != std::string::npos);

        // Les deux formes se relisent à l'identique.
        Card a, b;
        assert(io::parseCard(sansSchema, a) && io::parseCard(avecSchema, b));
        assert(a.field("illustrations")->values() == b.field("illustrations")->values());

        // Les tags sont toujours en liste, même seuls.
        Card t("t", "personnage");
        t.addTag("noblesse");
        assert(io::serializeCard(t).find("tags:\n  - \"noblesse\"") != std::string::npos);
        std::cout << "Test 9 OK (forme stable des champs liste)\n";
    }

    std::cout << "Tous les tests CardIO sont passes.\n";
    return 0;
}
