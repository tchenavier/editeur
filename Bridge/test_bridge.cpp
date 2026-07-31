// test_bridge.cpp
// Tests du pont SANS webview : on appelle Bridge::dispatch directement,
// exactement comme le ferait la glue webview. C'est la preuve concrète
// que la logique du pont est indépendante du transport.

#include "Bridge.h"
#include "CompareApi.h"
#include "EditorApi.h"
#include "CardApi.h"
#include "MarkdownIO.h"
#include "ExportApi.h"
#include "FileApi.h"
#include "ProjectApi.h"
#include "TextModel.h"
#include "UndoManager.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

using bridge::json;

// Petit assistant : envoie une commande et retourne la réponse parsée.
static json call(bridge::Bridge& b, const std::string& command, const json& payload) {
    const json request = {{"command", command}, {"payload", payload}};
    return json::parse(b.dispatch(request.dump()));
}

int main() {
    core::Document document;
    document.blocks().emplace_back(core::BlockType::Paragraph);

    core::UndoManager undoManager;
    bridge::Bridge dispatcher;
    bridge::EditorApi api(document, undoManager);
    bridge::FileApi fileApi(document, undoManager);
    assert(api.registerOn(dispatcher));
    assert(fileApi.registerOn(dispatcher));

    // --- Test 1 : insertion avec accents (aller-retour UTF-8) ---
    {
        auto r = call(dispatcher, "editor.insertText",
                      {{"block", 0}, {"pos", 0}, {"text", "Été à la forêt"}});
        assert(r["ok"] == true);
        assert(r["result"]["block"]["text"] == "Été à la forêt");
        std::cout << "Test 1 OK (UTF-8 accentue)\n";
    }

    // --- Test 2 : bascule de style par mot ---
    {
        // Position 1 = dans "Été" -> le mot entier passe en gras.
        auto r = call(dispatcher, "editor.toggleWordStyle",
                      {{"block", 0}, {"pos", 1}, {"style", "bold"}});
        assert(r["ok"] == true);
        assert(r["result"]["changed"] == true);
        const auto& runs = r["result"]["block"]["runs"];
        assert(runs.size() >= 1);
        assert(runs[0]["start"] == 0);
        assert(runs[0]["length"] == 3); // É-t-é : 3 caractères, pas 5 octets
        assert(runs[0]["styles"][0] == "bold");
        std::cout << "Test 2 OK (bascule par mot, positions en caracteres)\n";
    }

    // --- Test 3 : deuxième bascule = retrait du style ---
    {
        auto r = call(dispatcher, "editor.toggleWordStyle",
                      {{"block", 0}, {"pos", 1}, {"style", "bold"}});
        assert(r["ok"] == true);
        const auto& runs = r["result"]["block"]["runs"];
        // Plus aucun style : soit aucune plage, soit aucune avec "bold".
        for (const auto& run : runs) {
            for (const auto& s : run["styles"]) assert(s != "bold");
        }
        std::cout << "Test 3 OK (bascule inverse)\n";
    }

    // --- Test 4 : erreurs propres, jamais de plantage ---
    {
        // JSON invalide
        auto r1 = json::parse(dispatcher.dispatch("{pas du json"));
        assert(r1["ok"] == false);

        // Commande inconnue
        auto r2 = call(dispatcher, "editor.nExistePas", {});
        assert(r2["ok"] == false);

        // Bloc hors limites
        auto r3 = call(dispatcher, "editor.insertText",
                       {{"block", 42}, {"pos", 0}, {"text", "x"}});
        assert(r3["ok"] == false);

        // Position négative (arrive en JSON comme nombre signé)
        auto r4 = call(dispatcher, "editor.insertText",
                       {{"block", 0}, {"pos", -3}, {"text", "x"}});
        assert(r4["ok"] == false);

        // Style inconnu
        auto r5 = call(dispatcher, "editor.applyStyle",
                       {{"block", 0}, {"pos", 0}, {"count", 2}, {"style", "clignotant"}});
        assert(r5["ok"] == false);

        // Payload du mauvais type
        const json bad = {{"command", "editor.wordCount"}, {"payload", "oups"}};
        auto r6 = json::parse(dispatcher.dispatch(bad.dump()));
        assert(r6["ok"] == false);

        std::cout << "Test 4 OK (toutes les erreurs sont des reponses propres)\n";
    }

    // --- Test 5 : comptage de mots via le pont ---
    {
        auto r = call(dispatcher, "editor.wordCount", {});
        assert(r["ok"] == true);
        assert(r["result"]["total"] == 4); // "Été à la forêt"
        std::cout << "Test 5 OK (comptage)\n";
    }

    // --- Test 6 : double enregistrement refusé ---
    {
        bridge::Bridge other;
        assert(other.registerCommand("x", [](const json&) { return json{}; }));
        assert(!other.registerCommand("x", [](const json&) { return json{}; }));
        std::cout << "Test 6 OK (doublon de commande refuse)\n";
    }

    // --- Test 7 : annulation groupée par mot via le pont ---
    {
        // Frappe simulée caractère par caractère : " un" à la fin du
        // texte actuel ("Été à la forêt", 14 caractères).
        const char* keys[] = {" ", "u", "n"};
        for (size_t i = 0; i < 3; ++i) {
            auto r = call(dispatcher, "editor.insertText",
                          {{"block", 0}, {"pos", 14 + i}, {"text", keys[i]}});
            assert(r["ok"] == true);
        }

        // Premier undo : le mot "un" disparaît d'un coup (pas lettre à lettre).
        auto r1 = call(dispatcher, "editor.undo", {});
        assert(r1["ok"] == true);
        assert(r1["result"]["changed"] == true);
        assert(r1["result"]["block"]["text"] == "Été à la forêt ");

        // Deuxième undo : l'espace.
        auto r2 = call(dispatcher, "editor.undo", {});
        assert(r2["result"]["block"]["text"] == "Été à la forêt");

        // Redo : l'espace revient.
        auto r3 = call(dispatcher, "editor.redo", {});
        assert(r3["result"]["changed"] == true);
        assert(r3["result"]["block"]["text"] == "Été à la forêt ");

        auto r4 = call(dispatcher, "editor.historyState", {});
        assert(r4["result"]["canUndo"] == true);
        assert(r4["result"]["canRedo"] == true);
        std::cout << "Test 7 OK (undo/redo groupe par mot via le pont)\n";
    }

    // --- Test 8 : métadonnées et sérialisation Markdown via le pont ---
    {
        auto r1 = call(dispatcher, "file.setMetadata",
                       {{"title", "Essai"}, {"author", "Moi"}});
        assert(r1["ok"] == true);
        assert(r1["result"]["title"] == "Essai");

        auto r2 = call(dispatcher, "file.serialize", {});
        assert(r2["ok"] == true);
        const std::string markdown = r2["result"]["markdown"].get<std::string>();
        assert(markdown.find("title: \"Essai\"") != std::string::npos);
        assert(markdown.find("author: \"Moi\"") != std::string::npos);
        assert(markdown.find("Été à la forêt") != std::string::npos);

        // Chemin invalide : réponse d'erreur propre, pas de plantage.
        auto r3 = call(dispatcher, "file.save", {{"path", "Z:/nulle/part/x.md"}});
        assert(r3["ok"] == false);
        std::cout << "Test 8 OK (file.* via le pont)\n";
    }

    // --- Test 9 : opérations de bloc via le pont (Entrée, titre, fusion) ---
    {
        // On repart d'un document propre pour ce test.
        core::Document doc2;
        doc2.blocks().emplace_back(core::BlockType::Paragraph);
        core::UndoManager undo2;
        bridge::Bridge b2;
        bridge::EditorApi api2(doc2, undo2);
        assert(api2.registerOn(b2));

        call(b2, "editor.insertText", {{"block", 0}, {"pos", 0}, {"text", "un deux"}});

        // Titre : le bloc 0 devient un H1.
        auto rt = call(b2, "editor.setBlockType", {{"block", 0}, {"type", "h1"}});
        assert(rt["ok"] == true);
        assert(rt["result"]["block"]["type"] == "h1");

        // Entrée au milieu : coupe en deux blocs, le second est un paragraphe.
        auto rs = call(b2, "editor.splitBlock", {{"block", 0}, {"pos", 3}});
        assert(rs["ok"] == true);
        const auto& blocks = rs["result"]["blocks"];
        assert(blocks.size() == 2);
        assert(blocks[0]["text"] == "un ");
        assert(blocks[0]["type"] == "h1");
        assert(blocks[1]["text"] == "deux");
        assert(blocks[1]["type"] == "p");
        assert(rs["result"]["newBlock"] == 1);

        // Retour arrière en début de bloc 1 : fusion dans le bloc 0.
        auto rm = call(b2, "editor.mergeBlock", {{"block", 1}});
        assert(rm["ok"] == true);
        assert(rm["result"]["merged"] == true);
        assert(rm["result"]["caretBlock"] == 0);
        assert(rm["result"]["caretPos"] == 3); // longueur de "un " avant fusion
        assert(rm["result"]["blocks"].size() == 1);
        assert(rm["result"]["blocks"][0]["text"] == "un deux");

        // Undo de la fusion : structure restaurée, tout le document renvoyé.
        auto ru = call(b2, "editor.undo", {});
        assert(ru["ok"] == true);
        assert(ru["result"]["changed"] == true);
        assert(ru["result"]["wholeDocument"] == true);
        assert(ru["result"]["blocks"].size() == 2);

        // Fusion sur le bloc 0 : refusée proprement (pas de précédent).
        auto rm0 = call(b2, "editor.mergeBlock", {{"block", 0}});
        assert(rm0["ok"] == true);
        assert(rm0["result"]["merged"] == false);

        // Type de bloc inconnu : réponse d'erreur propre.
        auto rbad = call(b2, "editor.setBlockType", {{"block", 0}, {"type", "h9"}});
        assert(rbad["ok"] == false);
        std::cout << "Test 9 OK (setBlockType / splitBlock / mergeBlock via le pont)\n";
    }

    // --- Test 10 : réorganisation de chapitres via editor.moveBlocks ---
    {
        core::Document doc3;
        core::UndoManager undo3;
        bridge::Bridge b3;
        bridge::EditorApi api3(doc3, undo3);
        assert(api3.registerOn(b3));

        // Trois "chapitres" d'un bloc chacun : A, B, C.
        for (const char32_t* t : {U"A", U"B", U"C"}) {
            doc3.blocks().emplace_back(core::BlockType::Heading1);
            doc3.blocks().back().insertText(0, t);
        }

        // Déplace le chapitre C (index 2) en tête (before 0) -> C A B.
        auto rm = call(b3, "editor.moveBlocks", {{"start", 2}, {"count", 1}, {"before", 0}});
        assert(rm["ok"] == true);
        assert(rm["result"]["moved"] == true);
        const auto& blocks = rm["result"]["blocks"];
        assert(blocks.size() == 3);
        assert(blocks[0]["text"] == "C");
        assert(blocks[1]["text"] == "A");
        assert(blocks[2]["text"] == "B");

        // Undo : retour à A B C (opération structurelle).
        auto ru = call(b3, "editor.undo", {});
        assert(ru["result"]["wholeDocument"] == true);
        assert(ru["result"]["blocks"][0]["text"] == "A");
        assert(ru["result"]["blocks"][2]["text"] == "C");

        // Déplacement sans effet : moved == false, pas d'undo créé.
        auto rn = call(b3, "editor.moveBlocks", {{"start", 1}, {"count", 1}, {"before", 1}});
        assert(rn["result"]["moved"] == false);
        std::cout << "Test 10 OK (moveBlocks + undo structurel)\n";
    }

    // --- Test 11 : les commandes de dialogue natif sont enregistrées ---
    {
        // On NE les appelle PAS (elles ouvriraient un vrai dialogue modal
        // et bloqueraient le test) : on vérifie seulement que les enregistrer
        // une seconde fois échoue, preuve qu'elles existent déjà.
        assert(!dispatcher.registerCommand("file.chooseOpen",
            [](const json&) { return json{}; }));
        assert(!dispatcher.registerCommand("file.chooseSave",
            [](const json&) { return json{}; }));
        std::cout << "Test 11 OK (commandes de dialogue natif enregistrees)\n";
    }

    // --- Test 12 : recherche / remplacement via le pont ---
    {
        core::Document doc4;
        core::UndoManager undo4;
        bridge::Bridge b4;
        bridge::EditorApi api4(doc4, undo4);
        assert(api4.registerOn(b4));
        doc4.blocks().emplace_back(core::BlockType::Paragraph);
        call(b4, "editor.insertText", {{"block", 0}, {"pos", 0}, {"text", "le chat le chien"}});

        // find insensible à la casse : 2 occurrences de "le".
        auto rf = call(b4, "editor.find", {{"query", "le"}, {"fromBlock", 0}, {"fromPos", 0}});
        assert(rf["ok"] == true);
        assert(rf["result"]["found"] == true);
        assert(rf["result"]["total"] == 2);
        assert(rf["result"]["start"] == 0 && rf["result"]["end"] == 2);

        // replaceRange : remplace la 2e occurrence (pos 8) par "X".
        auto rr = call(b4, "editor.replaceRange", {{"block", 0}, {"start", 8}, {"count", 2}, {"text", "X"}});
        assert(rr["ok"] == true);
        assert(rr["result"]["block"]["text"] == "le chat X chien");

        // undo : le remplacement est UNE seule étape.
        auto ru = call(b4, "editor.undo", {});
        assert(ru["result"]["block"]["text"] == "le chat le chien");

        // replaceAll : remplace tous les "le" par "YY", une seule étape.
        auto ra = call(b4, "editor.replaceAll", {{"query", "le"}, {"replacement", "YY"}});
        assert(ra["ok"] == true);
        assert(ra["result"]["count"] == 2);
        assert(ra["result"]["blocks"][0]["text"] == "YY chat YY chien");
        auto ru2 = call(b4, "editor.undo", {});
        assert(ru2["result"]["wholeDocument"] == true);
        assert(ru2["result"]["blocks"][0]["text"] == "le chat le chien");
        std::cout << "Test 12 OK (find / replaceRange / replaceAll via le pont)\n";
    }

    // --- Test 13 : sauvegarde horodatée via file.backup ---
    {
        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "test_editeur_bridge_backup.md";
        const fs::path dir = fs::temp_directory_path() / "test_editeur_bridge_backup_backups";
        std::error_code ec; fs::remove_all(dir, ec);

        auto rb = call(dispatcher, "file.backup", {{"path", base.string()}, {"keep", 5}});
        assert(rb["ok"] == true);
        const std::string backupPath = rb["result"]["backup"].get<std::string>();
        assert(fs::exists(backupPath));

        fs::remove_all(dir, ec); // nettoyage
        std::cout << "Test 13 OK (file.backup via le pont)\n";
    }

    // --- Test 14 : état du document pour le garde-fou de fermeture ---
    {
        core::Document doc5;
        core::UndoManager undo5;
        bridge::Bridge b5;
        bridge::FileApi file5(doc5, undo5);
        assert(file5.registerOn(b5));
        doc5.blocks().emplace_back(core::BlockType::Paragraph);
        doc5.blocks()[0].insertText(0, U"Du texte");

        // Au départ : rien de modifié, aucun chemin.
        assert(!file5.hasUnsavedChanges());
        assert(file5.currentPath().empty());

        // L'UI signale des modifications non enregistrées.
        auto r1 = call(b5, "file.setState", {{"dirty", true}, {"path", "C:/romans/x.md"}});
        assert(r1["ok"] == true);
        assert(r1["result"]["dirty"] == true);
        assert(file5.hasUnsavedChanges());
        assert(file5.currentPath() == "C:/romans/x.md");

        // Après un enregistrement, l'UI repasse l'état à « propre ».
        call(b5, "file.setState", {{"dirty", false}});
        assert(!file5.hasUnsavedChanges());
        assert(file5.currentPath() == "C:/romans/x.md"); // chemin conservé

        // saveCurrent écrit réellement dans le fichier courant.
        namespace fs = std::filesystem;
        const fs::path target = fs::temp_directory_path() / "test_editeur_savecurrent.md";
        std::error_code ec; fs::remove(target, ec);
        call(b5, "file.setState", {{"dirty", true}, {"path", target.string()}});
        std::string err;
        assert(file5.saveCurrent(&err));
        assert(fs::exists(target));
        assert(!file5.hasUnsavedChanges()); // saveCurrent remet l'état à propre
        fs::remove(target, ec);
        std::cout << "Test 14 OK (etat du document + saveCurrent)\n";
    }

    // --- Test 15 : commandes cards.* (fiches de worldbuilding) ---
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "test_editeur_cardapi";
        std::error_code ec; fs::remove_all(root, ec);

        core::CardLibrary library;
        core::SchemaRegistry registry;
        bridge::Bridge b6;
        bridge::CardApi cards(library, registry);
        assert(cards.registerOn(b6));

        // Les schémas pilotent l'interface : ils doivent décrire les champs.
        auto rs = call(b6, "cards.schemas", {});
        assert(rs["ok"] == true);
        const auto& schemas = rs["result"]["schemas"];
        assert(schemas.size() == 6);
        bool trouvePersonnage = false;
        for (const auto& s : schemas) {
            if (s["typeId"] == "personnage") {
                trouvePersonnage = true;
                assert(!s["fields"].empty());
                bool relationsEstCardrefs = false;
                for (const auto& f : s["fields"]) {
                    if (f["key"] == "relations") { relationsEstCardrefs = (f["type"] == "cardrefs"); }
                }
                assert(relationsEstCardrefs); // l'UI saura afficher un sélecteur de fiches
            }
        }
        assert(trouvePersonnage);

        // Fixe le dossier (vide au départ) puis crée une fiche.
        auto ro = call(b6, "cards.open", {{"path", root.string()}});
        assert(ro["ok"] == true && ro["result"]["count"] == 0);

        auto rc = call(b6, "cards.save", {
            {"type", "personnage"}, {"title", "Aria de Val"},
            {"tags", {"noblesse"}},
            {"fields", {{"age", {"24 ans"}}, {"relations", {"bran"}}}},
        });
        assert(rc["ok"] == true);
        const std::string id = rc["result"]["card"]["id"].get<std::string>();
        assert(id == "aria-de-val");                    // identifiant déduit du titre
        assert(fs::exists(root / "personnage" / "aria-de-val.md")); // écrit sur disque

        // Deuxième fiche de même titre : identifiant unique automatique.
        auto rc2 = call(b6, "cards.save", {{"type", "personnage"}, {"title", "Aria de Val"}});
        assert(rc2["result"]["card"]["id"] == "aria-de-val-2");

        // Consultation et filtres.
        auto rg = call(b6, "cards.get", {{"id", id}});
        assert(rg["result"]["card"]["title"] == "Aria de Val");
        assert(rg["result"]["card"]["fields"]["age"][0] == "24 ans");

        call(b6, "cards.save", {{"type", "lieu"}, {"title", "Tour de Brume"}});
        assert(call(b6, "cards.list", {}) ["result"]["cards"].size() == 3);
        assert(call(b6, "cards.list", {{"type", "lieu"}})["result"]["cards"].size() == 1);
        assert(call(b6, "cards.list", {{"tag", "noblesse"}})["result"]["cards"].size() == 1);
        assert(call(b6, "cards.list", {{"query", "brume"}})["result"]["cards"].size() == 1);
        assert(call(b6, "cards.tags", {})["result"]["tags"].size() == 1);

        // Type inconnu : refus explicite (aucun formulaire ne saurait l'afficher).
        auto rbad = call(b6, "cards.save", {{"type", "dragon-vapeur"}, {"title", "X"}});
        assert(rbad["ok"] == false);

        // Suppression : fiche, fichier, ET références vers elle.
        call(b6, "cards.save", {{"type", "personnage"}, {"id", "bran"}, {"title", "Bran"}});
        auto rrm = call(b6, "cards.remove", {{"id", "bran"}});
        assert(rrm["result"]["removed"] == true);
        assert(!fs::exists(root / "personnage" / "bran.md"));
        auto rapres = call(b6, "cards.get", {{"id", id}});
        const auto& relations = rapres["result"]["card"]["fields"]["relations"];
        assert(relations.empty()); // le lien mort a disparu

        // cards.setRoot : des fiches créées SANS dossier sont écrites au
        // moment où on rattache un dossier (cas « on enregistre le
        // manuscrit après avoir créé des fiches »).
        {
            core::CardLibrary memLib;
            core::SchemaRegistry memReg;
            bridge::Bridge bMem;
            bridge::CardApi memCards(memLib, memReg);
            assert(memCards.registerOn(bMem));
            // Aucun dossier fixé : la fiche reste en mémoire.
            call(bMem, "cards.save", {{"type", "personnage"}, {"title", "En mémoire"}});
            const fs::path memRoot = fs::temp_directory_path() / "test_editeur_setroot";
            fs::remove_all(memRoot, ec);
            assert(!fs::exists(memRoot));
            // On rattache un dossier : la fiche déjà créée doit y atterrir.
            auto rsr = call(bMem, "cards.setRoot", {{"path", memRoot.string()}});
            assert(rsr["ok"] == true && rsr["result"]["count"] == 1);
            assert(fs::exists(memRoot / "personnage" / "en-memoire.md"));
            fs::remove_all(memRoot, ec);
        }

        // Rechargement depuis le disque : tout est retrouvé.
        core::CardLibrary autre;
        core::SchemaRegistry registry2;
        bridge::Bridge b7;
        bridge::CardApi cards2(autre, registry2);
        assert(cards2.registerOn(b7));
        auto rr = call(b7, "cards.open", {{"path", root.string()}});
        assert(rr["result"]["count"] == 3);
        assert(rr["result"]["skipped"] == 0);

        // Fiche inexistante : erreur propre.
        assert(call(b7, "cards.get", {{"id", "fantome"}})["ok"] == false);

        fs::remove_all(root, ec);
        std::cout << "Test 15 OK (commandes cards.*)\n";
    }

    // --- Test 16 : détection de modification externe (manuscrit) ---
    {
        namespace fs = std::filesystem;
        core::Document d;
        d.blocks().emplace_back();
        d.blocks()[0].insertText(0, U"Contenu");
        core::UndoManager u;
        bridge::Bridge b8;
        bridge::FileApi f8(d, u);
        assert(f8.registerOn(b8));

        const fs::path p = fs::temp_directory_path() / "test_editeur_conflict.md";
        std::error_code ec; fs::remove(p, ec);

        // Premier enregistrement : réussit et mémorise la date.
        auto r1 = call(b8, "file.save", {{"path", p.string()}});
        assert(r1["result"]["saved"] == true);

        // Modification externe simulée : on avance la date du fichier.
        fs::last_write_time(p, fs::file_time_type::clock::now() + std::chrono::seconds(120), ec);

        // Sans force : conflit détecté, aucune écriture.
        auto r2 = call(b8, "file.save", {{"path", p.string()}});
        assert(r2["ok"] == true);
        assert(r2["result"].contains("conflict") && r2["result"]["conflict"] == true);
        assert(!r2["result"].contains("saved"));

        // Avec force : l'écriture a lieu, et la référence est remise à jour.
        auto r3 = call(b8, "file.save", {{"path", p.string()}, {"force", true}});
        assert(r3["result"]["saved"] == true);
        auto r4 = call(b8, "file.save", {{"path", p.string()}});
        assert(r4["result"]["saved"] == true); // plus de conflit

        fs::remove(p, ec);
        std::cout << "Test 16 OK (conflit de fichier manuscrit)\n";
    }

    // --- Test 17 : détection de modification externe (fiche) ---
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "test_editeur_cardconflict";
        std::error_code ec; fs::remove_all(root, ec);

        core::CardLibrary lib;
        core::SchemaRegistry reg;
        bridge::Bridge bc;
        bridge::CardApi cards(lib, reg);
        assert(cards.registerOn(bc));
        call(bc, "cards.open", {{"path", root.string()}});

        auto rc = call(bc, "cards.save", {{"type", "personnage"}, {"title", "Aria"}});
        const std::string id = rc["result"]["card"]["id"].get<std::string>();
        const fs::path cardFile = root / "personnage" / (id + ".md");
        assert(fs::exists(cardFile));

        // Modification externe de la fiche.
        fs::last_write_time(cardFile, fs::file_time_type::clock::now() + std::chrono::seconds(120), ec);

        // Ré-enregistrer la MÊME fiche sans force : conflit.
        auto r2 = call(bc, "cards.save", {{"type", "personnage"}, {"id", id}, {"title", "Aria modifiée"}});
        assert(r2["result"].contains("conflict") && r2["result"]["conflict"] == true);

        // Avec force : la fiche est réécrite.
        auto r3 = call(bc, "cards.save",
                       {{"type", "personnage"}, {"id", id}, {"title", "Aria modifiée"}, {"force", true}});
        assert(r3["result"]["card"]["title"] == "Aria modifiée");

        // Une fiche NEUVE ne déclenche jamais de conflit (fichier inexistant).
        auto r4 = call(bc, "cards.save", {{"type", "lieu"}, {"title", "Nouveau lieu"}});
        assert(r4["result"].contains("card"));

        fs::remove_all(root, ec);
        std::cout << "Test 17 OK (conflit de fiche)\n";
    }

    // --- Test 18 : recherche / remplacement globaux dans les fiches ---
    {
        core::CardLibrary lib;
        core::SchemaRegistry reg;
        bridge::Bridge bs;
        bridge::CardApi cards(lib, reg);
        assert(cards.registerOn(bs));

        call(bs, "cards.save", {{"type", "personnage"}, {"id", "aria"}, {"title", "Aria de Val"},
                                {"fields", {{"caractere", {"Réservée mais loyale."}},
                                            {"relations", {"bran"}}}}});
        call(bs, "cards.save", {{"type", "lieu"}, {"id", "tour"}, {"title", "Tour Loyale"},
                                {"fields", {{"ambiance", {"Une loyale sentinelle."}}}}});

        // Recherche « loyale » : titre d'un lieu + deux champs.
        auto rse = call(bs, "cards.search", {{"query", "loyale"}});
        assert(rse["ok"] == true);
        const auto& results = rse["result"]["results"];
        assert(results.size() == 3); // aria/caractere, tour/titre, tour/ambiance
        bool titreVu = false, champVu = false;
        for (const auto& r : results) {
            if (r["field"] == "titre" && r["id"] == "tour") titreVu = true;
            if (r["field"] == "caractere" && r["id"] == "aria") champVu = true;
            assert(r.contains("snippet"));
        }
        assert(titreVu && champVu);

        // Insensible à la casse par défaut (le titre « Loyale » compte).
        assert(call(bs, "cards.search", {{"query", "loyale"}})["result"]["results"].size() == 3);
        // Sensible à la casse : « Loyale » majuscule n'est que dans le titre.
        assert(call(bs, "cards.search", {{"query", "Loyale"}, {"caseSensitive", true}})
                   ["result"]["results"].size() == 1);
        assert(call(bs, "cards.search", {{"query", "loyale"}, {"caseSensitive", true}})
                   ["result"]["results"].size() == 2); // les deux minuscules

        // Remplacement global « loyale » -> « fidèle » (titre + champs de
        // texte), SANS toucher aux références de fiches (champ relations).
        auto rr = call(bs, "cards.replaceAll", {{"query", "loyale"}, {"replacement", "fidèle"}});
        assert(rr["ok"] == true);
        assert(rr["result"]["count"] == 3);
        assert(rr["result"]["cards"] == 2);
        assert(call(bs, "cards.get", {{"id", "tour"}})["result"]["card"]["title"] == "Tour fidèle");
        // La relation « bran » d'Aria est intacte (pas de remplacement de texte).
        assert(call(bs, "cards.get", {{"id", "aria"}})["result"]["card"]["fields"]["relations"][0] == "bran");
        // Plus aucune occurrence de « loyale ».
        assert(call(bs, "cards.search", {{"query", "loyale"}})["result"]["results"].empty());
        std::cout << "Test 18 OK (recherche / remplacement globaux des fiches)\n";
    }

    // --- Test 19 : détection de références de fiches cassées ---
    {
        core::CardLibrary lib;
        core::SchemaRegistry reg;
        bridge::Bridge br;
        bridge::CardApi cards(lib, reg);
        assert(cards.registerOn(br));

        // Aria est liée à Bran (existe) et à un fantôme (n'existe pas).
        call(br, "cards.save", {{"type", "personnage"}, {"id", "bran"}, {"title", "Bran"}});
        call(br, "cards.save", {{"type", "personnage"}, {"id", "aria"}, {"title", "Aria"},
                                {"fields", {{"relations", {"bran", "personnage-disparu"}}}}});

        auto rc = call(br, "cards.checkRefs", {});
        assert(rc["ok"] == true);
        const auto& broken = rc["result"]["broken"];
        assert(broken.size() == 1);
        assert(broken[0]["id"] == "aria");
        assert(broken[0]["field"] == "relations");
        assert(broken[0]["targetId"] == "personnage-disparu");

        // Supprimer la fiche cible casserait le lien -> mais la suppression
        // purge justement les références, donc plus aucun lien mort.
        call(br, "cards.remove", {{"id", "bran"}});
        auto rc2 = call(br, "cards.checkRefs", {});
        assert(rc2["result"]["broken"].size() == 1); // toujours le fantôme, pas bran
        std::cout << "Test 19 OK (references de fiches cassees)\n";
    }

    // --- Test 20 : taille de caractères via le pont (+ undo) ---
    {
        core::Document doc3;
        doc3.blocks().emplace_back(core::BlockType::Paragraph);
        core::UndoManager undo3;
        bridge::Bridge b3;
        bridge::EditorApi api3(doc3, undo3);
        assert(api3.registerOn(b3));

        call(b3, "editor.insertText", {{"block", 0}, {"pos", 0}, {"text", "abcdef"}});

        // Agrandit "cd" (positions 2..3) à 150 %.
        auto rs = call(b3, "editor.setFontSize",
                       {{"block", 0}, {"pos", 2}, {"count", 2}, {"size", 150}});
        assert(rs["ok"] == true);
        // La plage renvoyée porte bien la taille 150 sur "cd".
        bool found150 = false;
        for (const auto& run : rs["result"]["block"]["runs"]) {
            if (run["start"] == 2 && run["length"] == 2) {
                assert(run["size"] == 150);
                found150 = true;
            }
        }
        assert(found150);

        // Valeur hors bornes : rejetée proprement (size attendu >= 0).
        auto rbad = call(b3, "editor.setFontSize",
                         {{"block", 0}, {"pos", 0}, {"count", 1}, {"size", -5}});
        assert(rbad["ok"] == false);

        // Undo : la taille revient à la normale (aucune plage de taille).
        auto ru = call(b3, "editor.undo", {});
        assert(ru["ok"] == true);
        assert(ru["result"]["changed"] == true);
        for (const auto& run : ru["result"]["block"]["runs"]) {
            assert(run["size"] == 100);
        }
        std::cout << "Test 20 OK (taille de caracteres via le pont + undo)\n";
    }

    // --- Test 21 : export EPUB et aperçu d'image via le pont (Phase 4) ---
    {
        core::Document doc4;
        doc4.metadata().title = "Roman exporté";
        doc4.metadata().author = "A. Auteur";
        core::TextBlock h(core::BlockType::Heading1);
        h.insertText(0, U"Chapitre premier");
        doc4.blocks().push_back(h);
        core::TextBlock p(core::BlockType::Paragraph);
        p.insertText(0, U"Il faisait nuit.");
        doc4.blocks().push_back(p);

        core::UndoManager undo4;
        core::CardLibrary library4;
        core::SchemaRegistry registry4;
        bridge::Bridge b4;
        bridge::ExportApi exportApi(doc4, library4, registry4);
        assert(exportApi.registerOn(b4));
        // Un second enregistrement doit échouer (doublon de commande).
        assert(!exportApi.registerOn(b4));

        const auto epubPath =
            std::filesystem::temp_directory_path() / "test_bridge_export.epub";
        std::error_code ec;
        std::filesystem::remove(epubPath, ec);

        auto r = call(b4, "export.epub", {{"path", epubPath.string()}});
        assert(r["ok"] == true);
        assert(r["result"]["chapters"] == 1);
        assert(r["result"]["bytes"].get<size_t>() > 0);
        assert(std::filesystem::exists(epubPath));

        // Type d'export inconnu : refus explicite, aucun fichier écrit.
        auto rbad = call(b4, "export.epub",
                         {{"path", epubPath.string()}, {"kind", "inconnu"}});
        assert(rbad["ok"] == false);

        // Bible sans aucune fiche : message clair plutôt qu'un livre vide.
        auto rbible = call(b4, "export.epub",
                           {{"path", epubPath.string()}, {"kind", "bible"}});
        assert(rbible["ok"] == false);

        // Avec une fiche, la bible s'exporte et reprend le titre du roman.
        core::Card elise("elise", "personnage");
        elise.setTitle("Élise");
        library4.put(elise);
        auto rbible2 = call(b4, "export.epub",
                            {{"path", epubPath.string()}, {"kind", "bible"}});
        assert(rbible2["ok"] == true);
        assert(rbible2["result"]["chapters"].get<size_t>() >= 1);

        // Chemin manquant : erreur de validation, pas de plantage.
        assert(call(b4, "export.epub", {})["ok"] == false);

        // --- export.image : data URI (l'interface ne peut pas lire file://)
        const auto pngPath =
            std::filesystem::temp_directory_path() / "test_bridge_image.png";
        {
            std::ofstream out(pngPath, std::ios::binary);
            const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
            out.write(reinterpret_cast<const char*>(signature), 8);
            const std::string body(32, 'x');
            out.write(body.data(), static_cast<std::streamsize>(body.size()));
        }
        auto rimg = call(b4, "export.image", {{"path", pngPath.string()}});
        assert(rimg["ok"] == true);
        assert(rimg["result"]["mediaType"] == "image/png");
        assert(rimg["result"]["dataUri"].get<std::string>().rfind(
                   "data:image/png;base64,", 0) == 0);
        assert(rimg["result"]["bytes"] == 40);

        // Fichier absent, et fichier qui n'est pas une image : erreurs propres.
        assert(call(b4, "export.image", {{"path", "zzz_inexistant.png"}})["ok"] == false);
        {
            const auto notImage =
                std::filesystem::temp_directory_path() / "test_bridge_pas_image.png";
            { std::ofstream out(notImage); out << "du texte"; }
            assert(call(b4, "export.image", {{"path", notImage.string()}})["ok"] == false);
            std::filesystem::remove(notImage, ec);
        }

        // Base64 : vecteurs de référence (RFC 4648).
        assert(bridge::base64Encode("") == "");
        assert(bridge::base64Encode("f") == "Zg==");
        assert(bridge::base64Encode("fo") == "Zm8=");
        assert(bridge::base64Encode("foo") == "Zm9v");
        assert(bridge::base64Encode("foobar") == "Zm9vYmFy");

        // Base64 : propriétés vérifiées sur TOUTES les tailles de 0 à 300
        // et sur tous les octets possibles (l'encodeur est écrit à la
        // main ; un décalage d'un octet ne se verrait pas à l'œil, mais
        // corromprait silencieusement les images de couverture).
        {
            const std::string alphabet =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            for (size_t n = 0; n <= 300; ++n) {
                std::string data;
                data.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                    data.push_back(static_cast<char>((i * 7 + n) % 256)); // tous les octets
                }
                const std::string encoded = bridge::base64Encode(data);
                // Longueur : 4 caractères par groupe de 3 octets, complété.
                assert(encoded.size() == 4 * ((n + 2) / 3));
                // Remplissage : uniquement en fin, et en nombre exact.
                const size_t padding = (n % 3 == 0) ? 0 : (3 - n % 3);
                for (size_t i = 0; i < encoded.size(); ++i) {
                    const bool isPad = (i >= encoded.size() - padding) && padding > 0;
                    if (isPad) assert(encoded[i] == '=');
                    else assert(alphabet.find(encoded[i]) != std::string::npos);
                }
            }
        }

        std::filesystem::remove(epubPath, ec);
        std::filesystem::remove(pngPath, ec);
        std::cout << "Test 21 OK (export EPUB et apercu d'image via le pont)\n";
    }

    // --- Test 22 : suivi de projet via le pont (Phase 5, jalon A) ---
    {
        const auto racine = std::filesystem::temp_directory_path() / "test_bridge_projet";
        std::error_code ec;
        std::filesystem::remove_all(racine, ec);
        std::filesystem::create_directories(racine, ec);
        const auto manuscrit = racine / "roman.md";
        const std::string chemin = manuscrit.string();

        core::Document doc5;
        core::TextBlock b5(core::BlockType::Paragraph);
        b5.insertText(0, U"Premier jet.");
        doc5.blocks().push_back(b5);
        core::UndoManager undo5;
        bridge::Bridge b6;
        bridge::FileApi files5(doc5, undo5);
        bridge::ProjectApi project5(doc5);
        assert(files5.registerOn(b6));
        assert(project5.registerOn(b6));
        assert(!project5.registerOn(b6)); // doublon de commande refusé

        // Avant tout enregistrement : aucune identité de projet.
        assert(call(b6, "file.getMetadata", {})["result"]["project"] == "");

        // L'enregistrement SCELLE l'identité du projet.
        assert(call(b6, "file.save", {{"path", chemin}})["ok"] == true);
        const std::string projectId =
            call(b6, "file.getMetadata", {})["result"]["project"].get<std::string>();
        assert(projectId.size() == 36);
        // Elle est écrite dans le fichier, donc elle survit à sa relecture.
        assert(call(b6, "file.load", {{"path", chemin}})["ok"] == true);
        assert(call(b6, "file.getMetadata", {})["result"]["project"] == projectId);
        // Et elle n'est PAS modifiable depuis l'interface.
        call(b6, "file.setMetadata", {{"project", "550e8400-e29b-41d4-a716-446655440000"}});
        assert(call(b6, "file.getMetadata", {})["result"]["project"] == projectId);

        // Un enregistrement ultérieur ne change pas l'identité.
        assert(call(b6, "file.save", {{"path", chemin}})["ok"] == true);
        assert(call(b6, "file.getMetadata", {})["result"]["project"] == projectId);

        // État initial : le manuscrit est là, mais rien n'a encore été
        // inventorié — il est donc « en attente ».
        auto etat = call(b6, "project.state", {{"path", chemin}});
        assert(etat["ok"] == true);
        assert(etat["result"]["tracked"] == false);
        assert(etat["result"]["files"] == 1);
        assert(etat["result"]["pending"].size() == 1);
        assert(etat["result"]["pending"][0]["kind"] == "ajout");

        // Instantané : les changements passent au journal.
        auto instantane = call(b6, "project.snapshot", {{"path", chemin}});
        assert(instantane["ok"] == true);
        assert(instantane["result"]["recorded"].size() == 1);
        assert(instantane["result"]["projectId"] == projectId);

        // Plus rien en attente juste après.
        etat = call(b6, "project.state", {{"path", chemin}});
        assert(etat["result"]["tracked"] == true);
        assert(etat["result"]["pending"].empty());

        // Un second instantané SANS modification n'enregistre rien : une
        // sauvegarde qui ne change rien ne doit pas fabriquer d'histoire.
        assert(call(b6, "project.snapshot", {{"path", chemin}})["result"]["recorded"].empty());

        // Modification réelle du texte, puis enregistrement et instantané.
        call(b6, "file.setMetadata", {{"title", "Roman suivi"}});
        assert(call(b6, "file.save", {{"path", chemin}})["ok"] == true);
        auto suivant = call(b6, "project.snapshot", {{"path", chemin}});
        assert(suivant["result"]["recorded"].size() == 1);
        assert(suivant["result"]["recorded"][0]["kind"] == "modif");
        assert(suivant["result"]["recorded"][0]["path"] == "roman.md");

        // Journal : la plus récente d'abord.
        auto histoire = call(b6, "project.history", {{"path", chemin}});
        assert(histoire["ok"] == true);
        assert(histoire["result"]["entries"].size() == 2);
        assert(histoire["result"]["entries"][0]["kind"] == "modif");
        assert(histoire["result"]["entries"][1]["kind"] == "ajout");
        // Limite respectée.
        assert(call(b6, "project.history", {{"path", chemin}, {"limit", 1}})
                   ["result"]["entries"].size() == 1);

        // Chemin manquant ou vide : erreur propre, jamais de plantage.
        assert(call(b6, "project.state", {})["ok"] == false);
        assert(call(b6, "project.snapshot", {{"path", ""}})["ok"] == false);

        std::filesystem::remove_all(racine, ec);
        std::cout << "Test 22 OK (suivi de projet via le pont)\n";
    }

    // --- Test 23 : comparaison de deux versions via le pont ---
    {
        const auto dossier = std::filesystem::temp_directory_path() / "test_bridge_compare";
        std::error_code ec;
        std::filesystem::remove_all(dossier, ec);
        std::filesystem::create_directories(dossier, ec);
        const auto ancien = dossier / "ancienne_version.md";

        // Version de référence, écrite sur disque.
        {
            core::Document reference;
            reference.metadata().title = "Roman";
            for (const char32_t* t : {U"# Chapitre premier", U"Il faisait nuit.",
                                      U"Élise se leva.", U"Elle sortit."}) {
                core::TextBlock block(t[0] == U'#' ? core::BlockType::Heading1
                                                   : core::BlockType::Paragraph);
                block.insertText(0, t[0] == U'#' ? std::u32string(U"Chapitre premier")
                                                 : std::u32string(t));
                reference.blocks().push_back(block);
            }
            std::string err;
            assert(io::saveToFile(reference, ancien, &err));
        }

        // Version courante : un mot changé, un paragraphe inséré.
        core::Document courant;
        courant.metadata().title = "Roman";
        auto ajoute = [&courant](core::BlockType type, const std::u32string& texte) {
            core::TextBlock block(type);
            block.insertText(0, texte);
            courant.blocks().push_back(block);
        };
        ajoute(core::BlockType::Heading1, U"Chapitre premier");
        ajoute(core::BlockType::Paragraph, U"Il faisait nuit noire.");  // modifié
        ajoute(core::BlockType::Paragraph, U"Un chien aboya.");         // inséré
        ajoute(core::BlockType::Paragraph, U"Élise se leva.");
        ajoute(core::BlockType::Paragraph, U"Elle sortit.");

        core::UndoManager undo7;
        bridge::Bridge b7;
        bridge::CompareApi compareApi(courant, undo7);
        assert(compareApi.registerOn(b7));
        assert(!compareApi.registerOn(b7)); // doublon refusé

        auto r = call(b7, "compare.withFile", {{"path", ancien.string()}});
        assert(r["ok"] == true);
        const auto& res = r["result"];
        assert(res["identical"] == false);
        assert(res["stats"]["changed"] == 1);
        assert(res["stats"]["added"] == 1);
        assert(res["stats"]["removed"] == 0);
        // LE point : les trois autres paragraphes restent « egaux », alors
        // qu'ils sont décalés d'un rang par l'insertion.
        assert(res["stats"]["equal"] == 3);

        // La ligne modifiée porte les deux textes et les mots à surligner.
        bool trouve = false;
        for (const auto& ligne : res["rows"]) {
            if (ligne["kind"] == "changed") {
                trouve = true;
                assert(ligne["left"]["text"] == "Il faisait nuit.");
                assert(ligne["right"]["text"] == "Il faisait nuit noire.");
                assert(ligne["right"]["marks"].size() == 1); // « noire » ajouté
                assert(ligne["left"]["marks"].empty());      // rien retiré
            }
            if (ligne["kind"] == "added") {
                assert(ligne["left"].is_null());
                assert(ligne["right"]["text"] == "Un chien aboya.");
            }
        }
        assert(trouve);

        // Comparer un document à lui-même ne montre aucune différence.
        {
            core::Document copie = courant;
            core::UndoManager undo8;
            bridge::Bridge b8;
            bridge::CompareApi memeApi(copie, undo8);
            assert(memeApi.registerOn(b8));
            std::string err;
            const auto meme = dossier / "identique.md";
            assert(io::saveToFile(courant, meme, &err));
            auto same = call(b8, "compare.withFile", {{"path", meme.string()}});
            assert(same["result"]["identical"] == true);
            assert(same["result"]["stats"]["changed"] == 0);
        }

        // Fichier absent : erreur propre, jamais de plantage.
        assert(call(b7, "compare.withFile", {{"path", "zzz_absent.md"}})["ok"] == false);
        assert(call(b7, "compare.withFile", {})["ok"] == false);

        // L'unité de comparaison retenue et l'état de la structure sont
        // annoncés : ici deux versions bien découpées, donc paragraphes.
        assert(res["structure"]["unit"] == "block");
        assert(res["structure"]["flattened"] == "none");

        std::filesystem::remove_all(dossier, ec);
        std::cout << "Test 23 OK (comparaison de versions via le pont)\n";
    }

    // --- Test 24 : version rendue d'un seul tenant par un correcteur ---
    //
    // LE cas qui a motivé tout ceci : le texte revient corrigé mais sans
    // aucun paragraphe. Deux choses doivent alors se produire — la
    // comparaison bascule sur les phrases (sinon tout ressort comme
    // réécrit), et le redécoupage rend sa structure au manuscrit.
    {
        const auto dossier = std::filesystem::temp_directory_path() / "test_bridge_aplati";
        std::error_code ec;
        std::filesystem::remove_all(dossier, ec);
        std::filesystem::create_directories(dossier, ec);
        const auto ancien = dossier / "avant_correction.md";

        {
            core::Document reference;
            reference.metadata().title = "Roman";
            auto ajoute = [&reference](core::BlockType type, const std::u32string& texte) {
                core::TextBlock block(type);
                block.insertText(0, texte);
                reference.blocks().push_back(block);
            };
            ajoute(core::BlockType::Heading1, U"Acte I");
            ajoute(core::BlockType::Paragraph,
                   U"Le vent hurlait contre les remparts. Regina veillait.");
            ajoute(core::BlockType::Paragraph,
                   U"Elle rêvait de forêts. Personne ne le savait.");
            ajoute(core::BlockType::Paragraph, U"— Je pars, dit-elle.");
            std::string err;
            assert(io::saveToFile(reference, ancien, &err));
        }

        // Ce que rend le correcteur : tout d'un bloc, un mot corrigé.
        core::Document aplati;
        aplati.metadata().title = "Roman";
        {
            core::TextBlock block(core::BlockType::Paragraph);
            block.insertText(0, U"Acte I Le vent soufflait contre les remparts. "
                                U"Regina veillait. Elle rêvait de forêts. "
                                U"Personne ne le savait. — Je pars, dit-elle.");
            aplati.blocks().push_back(block);
        }

        core::UndoManager undo9;
        bridge::Bridge b9;
        bridge::EditorApi editeur9(aplati, undo9);
        bridge::CompareApi compare9(aplati, undo9);
        assert(editeur9.registerOn(b9));
        assert(compare9.registerOn(b9));

        auto vue = call(b9, "compare.withFile", {{"path", ancien.string()}})["result"];
        // Un bloc unique face à quatre : l'appariement des paragraphes ne
        // retrouve rien, la comparaison bascule sur les phrases.
        assert(vue["structure"]["unit"] == "sentence");
        assert(vue["structure"]["flattened"] == "right");
        // Et là, l'essentiel du texte est reconnu comme inchangé.
        assert(vue["stats"]["equal"] >= 3);
        assert(vue["stats"]["changed"] == 1); // la phrase corrigée, seule

        // Redécoupage : la structure du fichier de référence est rendue au
        // manuscrit, titre compris, sans toucher à la correction.
        auto refait = call(b9, "compare.reparagraph", {{"path", ancien.string()}});
        assert(refait["ok"] == true);
        assert(refait["result"]["changed"] == true);
        assert(refait["result"]["count"] == 4);
        assert(aplati.blocks().size() == 4);

        const auto& blocs = refait["result"]["blocks"];
        assert(blocs.size() == 4);
        assert(blocs[0]["type"] == "h1"); // le titre a retrouvé son niveau
        assert(blocs[0]["text"] == "Acte I");
        assert(blocs[1]["text"] == "Le vent soufflait contre les remparts. Regina veillait.");
        assert(blocs[3]["text"] == "— Je pars, dit-elle.");

        // Une fois le manuscrit découpé, l'opération se refuse : elle
        // remplacerait la structure de l'auteur par celle d'un autre.
        assert(call(b9, "compare.reparagraph", {{"path", ancien.string()}})["ok"] == false);

        // Et elle s'annule comme n'importe quelle étape.
        assert(call(b9, "editor.undo", {})["ok"] == true);
        assert(aplati.blocks().size() == 1);

        assert(call(b9, "compare.reparagraph", {{"path", "zzz_absent.md"}})["ok"] == false);

        std::filesystem::remove_all(dossier, ec);
        std::cout << "Test 24 OK (texte aplati : comparaison par phrases, redecoupage)\n";
    }

    std::cout << "Tous les tests du pont sont passes.\n";
    return 0;
}
