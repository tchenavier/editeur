// leak_check.cpp
// -----------------------------------------------------------------------
// Détection de fuites mémoire (équivalent LeakSanitizer, indisponible sur
// MSVC) via le tas de débogage du CRT.
//
// Principe : on « réchauffe » d'abord les allocations statiques (faites une
// seule fois, ex. std::string statiques), on prend un point de repère, on
// exécute un GRAND NOMBRE de fois toutes les opérations du projet dans des
// portées (pour que les destructeurs s'exécutent), puis on compare la
// mémoire. Une vraie fuite s'accumule à chaque itération ; les allocations
// statiques, elles, restent constantes.
//
// À compiler avec le CRT de débogage (/MDd) et lancé seul. Sortie 0 si
// aucune fuite, 1 sinon.
// -----------------------------------------------------------------------

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

#include "AffixDictionary.h"
#include "Book.h"
#include "Bridge.h"
#include "CardApi.h"
#include "CardIO.h"
#include "CardModel.h"
#include "ChangeLog.h"
#include "EditorApi.h"
#include "Epub.h"
#include "ExportApi.h"
#include "FileApi.h"
#include "MarkdownIO.h"
#include "PathUtf8.h"
#include "ProjectIndex.h"
#include "Search.h"
#include "Speller.h"
#include "TextModel.h"
#include "Thesaurus.h"
#include "UndoManager.h"
#include "Uuid.h"
#include "Yaml.h"
#include "Zip.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

// Exerce toutes les couches métier dans des portées locales : à la sortie
// de doWork(), tout doit être libéré.
static void doWork(const fs::path& tmpDir) {
    // --- Modèle de texte + historique + opérations structurelles ---
    {
        core::Document doc;
        core::UndoManager undo;
        doc.blocks().emplace_back();
        for (char32_t c : std::u32string(U"Le château près de la forêt, 1er jour.")) {
            core::TextBlock& b = doc.blocks()[0];
            core::TextBlock before = b;
            const size_t p = b.length();
            b.insertText(p, std::u32string(1, c));
            undo.recordTyping(0, p, std::u32string(1, c), std::move(before), b);
        }
        doc.blocks()[0].applyStyle(3, 7, core::TextStyle::Bold);
        doc.blocks()[0].applyStyle(3, 4, core::TextStyle::Italic);
        doc.blocks()[0].removeStyle(4, 2, core::TextStyle::Bold);
        doc.setBlockType(0, core::BlockType::Heading1);
        doc.splitBlock(0, 5);
        doc.mergeWithPrevious(1);
        doc.moveBlocks(0, 1, 1);
        for (int i = 0; i < 20; ++i) undo.undo(doc);
        for (int i = 0; i < 20; ++i) undo.redo(doc);
        (void)doc.wordCount();
    }

    // --- Recherche / remplacement ---
    {
        core::Document doc;
        for (const char32_t* t : {U"le chat et le chien", U"La souris", U"le loup"}) {
            doc.blocks().emplace_back();
            doc.blocks().back().insertText(0, t);
        }
        core::SearchOptions opts;
        (void)core::findNext(doc, U"le", 0, doc.blocks().size(), 0, 0, opts, false, true);
        (void)core::countMatches(doc, U"le", 0, doc.blocks().size(), opts);
        (void)core::replaceAll(doc, U"le", U"XX", 0, doc.blocks().size(), opts);
        size_t n = 0;
        (void)core::replaceInString(U"un le deux le", U"le", U"la", opts, &n);
        (void)core::contains(U"abcdef", U"cd", opts);
    }

    // --- Sérialisation Markdown : aller-retour + disque + backup ---
    {
        core::Document doc;
        doc.metadata().title = "Été à la forêt";
        doc.metadata().author = "Autrice";
        doc.blocks().emplace_back(core::BlockType::Heading1);
        doc.blocks()[0].insertText(0, U"Chapitre premier");
        doc.blocks().emplace_back();
        doc.blocks()[1].insertText(0, U"Un début en **gras** simulé.");
        doc.blocks()[1].applyStyle(3, 5, core::TextStyle::Underline);

        const std::string md = io::serialize(doc);
        core::Document back = io::parse(md);
        (void)back;

        const fs::path file = tmpDir / "leak_manuscrit.md";
        std::string err;
        io::saveToFile(doc, file, &err);
        core::Document loaded;
        io::loadFromFile(loaded, file, &err);
        std::string bpath;
        io::writeBackup(doc, file, 3, &bpath, &err);
    }

    // --- YAML ---
    {
        io::YamlBlock y;
        y.setScalar("titre", "Aria \"la\" Val");
        y.setList("tags", {"noblesse", "chapitre-1"}, true);
        y.setScalar("desc", "Ligne 1.\nLigne 2.");
        const std::string s = y.toString();
        (void)io::YamlBlock::parse(io::splitLines(s));
    }

    // --- Fiches : modèle, sérialisation, bibliothèque, disque ---
    {
        core::SchemaRegistry reg;
        core::CardLibrary lib;

        core::Card aria("aria", "personnage");
        aria.setTitle("Élise de Val");
        aria.addTag("noblesse");
        aria.editField("age").setScalar("24 ans");
        aria.editField("relations").setValues({"bran", "elise"});
        aria.editField("historique").setScalar("Née à Brume.\nPuis exilée.");
        lib.put(std::move(aria));

        core::Card bran("bran", "personnage");
        bran.setTitle("Bran");
        lib.put(std::move(bran));

        (void)lib.byType("personnage");
        (void)lib.byTag("noblesse");
        (void)lib.searchByTitle("élise");
        (void)lib.allTags();

        const std::string card = io::serializeCard(*lib.find("aria"), &reg);
        core::Card relue;
        io::parseCard(card, relue);
        (void)io::slugify("Élise & Cie !");

        const fs::path root = tmpDir / "leak_fiches";
        std::string err;
        io::saveLibrary(lib, root, &err, &reg);
        core::CardLibrary reread;
        size_t skipped = 0;
        io::loadLibrary(reread, root, &skipped, &err);

        lib.remove("bran", true, &reg);

        // --- Export (Phase 4) : livre depuis les fiches, puis EPUB ---
        const io::Book bible = io::bookFromCards(lib, reg);
        (void)io::buildEpub(bible);
    }

    // --- Export du manuscrit : structure de livre, EPUB, archive ---
    {
        core::Document doc;
        doc.metadata().title = "Roman exporté";
        doc.metadata().author = "Autrice";
        doc.metadata().summary = "Présentation.";
        doc.blocks().emplace_back(core::BlockType::Heading1);
        doc.blocks()[0].insertText(0, U"Chapitre premier");
        doc.blocks().emplace_back();
        doc.blocks()[1].insertText(0, U"Il faisait nuit noire.");
        doc.blocks()[1].applyStyle(3, 7, core::TextStyle::Bold);
        doc.blocks()[1].setFontSize(0, 3, 130);

        const io::Book book = io::bookFromDocument(doc);
        const std::string epub = io::buildEpub(book);
        (void)epub;

        const fs::path file = tmpDir / "leak_livre.epub";
        std::string err;
        io::writeEpub(book, file, {}, &err);

        io::ZipWriter zip;
        zip.addFile("mimetype", "application/epub+zip");
        zip.addFile("dossier/fichier.txt", std::string(2048, 'x'));
        (void)zip.build();
        (void)io::ZipWriter::crc32("données");
    }

    // --- Le PONT de bout en bout -------------------------------------
    // C'est le seul endroit où toutes les couches vivent ensemble, et
    // celui qui manipule des objets JSON (allocations nombreuses et
    // imbriquées). Les chemins d'ERREUR sont exercés aussi : une commande
    // qui échoue ne doit pas fuir davantage qu'une commande qui réussit.
    {
        core::Document doc;
        doc.blocks().emplace_back();
        core::UndoManager undo;
        core::CardLibrary lib;
        core::SchemaRegistry reg;

        bridge::Bridge dispatcher;
        bridge::EditorApi editorApi(doc, undo);
        bridge::FileApi fileApi(doc, undo);
        bridge::CardApi cardApi(lib, reg);
        bridge::ExportApi exportApi(doc, lib, reg);
        editorApi.registerOn(dispatcher);
        fileApi.registerOn(dispatcher);
        cardApi.registerOn(dispatcher);
        exportApi.registerOn(dispatcher);

        auto call = [&dispatcher](const char* command, const bridge::json& payload) {
            const bridge::json request = {{"command", command}, {"payload", payload}};
            (void)dispatcher.dispatch(request.dump());
        };

        call("editor.insertText", {{"block", 0}, {"pos", 0}, {"text", "Le château, 1er jour."}});
        call("editor.applyStyle", {{"block", 0}, {"pos", 3}, {"count", 7}, {"style", "bold"}});
        call("editor.setFontSize", {{"block", 0}, {"pos", 0}, {"count", 2}, {"size", 130}});
        call("editor.splitBlock", {{"block", 0}, {"pos", 3}});
        call("editor.setBlockType", {{"block", 0}, {"type", "h1"}});
        call("editor.find", {{"query", "jour"}, {"rangeStart", 0}, {"rangeEnd", 2}});
        call("editor.undo", bridge::json::object());
        call("editor.redo", bridge::json::object());
        call("editor.getDocument", bridge::json::object());
        call("file.setMetadata", {{"title", "Roman"}, {"author", "Autrice"},
                                  {"summary", "Résumé."}});
        call("file.serialize", bridge::json::object());
        call("cards.schemas", bridge::json::object());
        call("cards.save", {{"type", "personnage"}, {"title", "Élise"},
                            {"fields", {{"age", {"24"}}}}});
        call("cards.list", bridge::json::object());
        call("cards.search", {{"query", "éli"}});
        call("cards.checkRefs", bridge::json::object());

        // Chemins d'ERREUR : JSON illisible, commande inconnue, index hors
        // limites, champ manquant.
        (void)dispatcher.dispatch("ceci n'est pas du json");
        call("commande.inexistante", bridge::json::object());
        call("editor.insertText", {{"block", 99}, {"pos", 0}, {"text", "x"}});
        call("export.epub", bridge::json::object());

        // Export réel : EPUB écrit sur disque, puis image en data URI.
        const fs::path epub = tmpDir / "leak_pont.epub";
        call("export.epub", {{"path", utils::pathToUtf8(epub)}});
        call("export.epub", {{"path", utils::pathToUtf8(epub)}, {"kind", "bible"}});

        const fs::path png = tmpDir / "leak_image.png";
        {
            std::ofstream out(png, std::ios::binary);
            const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
            out.write(reinterpret_cast<const char*>(signature), 8);
            const std::string body(512, 'x');
            out.write(body.data(), static_cast<std::streamsize>(body.size()));
        }
        call("export.image", {{"path", utils::pathToUtf8(png)}});
        call("export.image", {{"path", "fichier_absent_zzz.png"}});
    }

    // --- Suivi de projet : inventaire, journal, comparaison ----------
    {
        const fs::path manuscrit = tmpDir / "leak_projet" / "roman.md";
        std::error_code ec;
        fs::create_directories(manuscrit.parent_path(), ec);
        {
            std::ofstream out(manuscrit, std::ios::binary | std::ios::trunc);
            out << "---\ntitle: \"Roman\"\n---\n\n# Chapitre\n\nTexte.\n";
        }

        const std::string id = utils::newUuid();
        (void)utils::isValidUuid(id);

        const io::ProjectIndex index = io::buildIndex(manuscrit, id);
        const std::string serialise = io::serializeIndex(index);
        const io::ProjectIndex relu = io::parseIndex(serialise);
        const std::vector<io::Change> changements = io::diffIndex(io::ProjectIndex{}, relu);

        std::string err;
        io::saveIndex(index, io::indexPathFor(manuscrit), &err);
        io::ProjectIndex charge;
        io::loadIndex(charge, io::indexPathFor(manuscrit), &err);

        io::appendChanges(io::journalPathFor(manuscrit), changements, &err);
        (void)io::readJournal(io::journalPathFor(manuscrit));
        (void)io::parseJournal(serialise); // contenu inadapté : chemin d'erreur
        (void)io::compactJournal(io::journalPathFor(manuscrit), 1, &err);

        fs::remove_all(manuscrit.parent_path(), ec);
    }

    // --- Correcteur orthographique et synonymes ----------------------
    // Mini-dictionnaire : on vérifie les FUITES, pas la linguistique.
    {
        const char* const aff =
            "SET UTF-8\nFLAG long\nFULLSTRIP\nNEEDAFFIX ()\n"
            "REP 1\nREP ph f\n"
            "SFX Pl Y 1\nSFX Pl 0 s [^sxz]\n";
        const char* const dic = "3\nchat/Pl\nchien/Pl\nélise\n";

        const io::AffixData affix = io::parseAff(aff);
        core::Speller speller;
        speller.setForms(io::expandDictionary(dic, affix));
        speller.setReplacements(affix.replacements);
        speller.setPersonal({"Ysalinde"});
        (void)speller.check(U"Le chat et le chien de Ysalinde, phantôme zzzz.");
        (void)speller.suggest("chatt");
        speller.addPersonal("Brumeval");
        speller.removePersonal("Brumeval");
        speller.ignoreWord("zzzz");
        speller.clearIgnored();

        io::Thesaurus thesaurus;
        thesaurus.load("UTF-8\nroman|1\n(Nom)|récit|fiction\n");
        (void)thesaurus.lookup("roman");
        (void)thesaurus.lookup("absent");
    }
}

// Initialise une bonne fois les allocations statiques du projet, AVANT le
// point de repère, pour qu'elles ne soient pas comptées comme des fuites.
static void warmup(const fs::path& tmpDir) {
    doWork(tmpDir);
}

int main() {
    const fs::path tmpDir = fs::temp_directory_path() / "editeur_leakcheck";
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);

    warmup(tmpDir); // amorce les statiques

    _CrtMemState s1, s2, s3;
    _CrtMemCheckpoint(&s1);

    for (int i = 0; i < 500; ++i) doWork(tmpDir);

    _CrtMemCheckpoint(&s2);

    fs::remove_all(tmpDir, ec);

    if (_CrtMemDifference(&s3, &s1, &s2)) {
        std::cout << "FUITE MEMOIRE DETECTEE (difference apres 500 iterations) :\n";
        _CrtMemDumpStatistics(&s3);
        return 1;
    }
    std::cout << "Aucune fuite memoire detectee (500 iterations, toutes couches).\n";
    return 0;
}
