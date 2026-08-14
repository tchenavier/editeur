// test_markdown.cpp
// Tests de la sérialisation Markdown (io/MarkdownIO) : aller-retour,
// échappement, parsing de fichiers externes, sauvegarde atomique.

#include "MarkdownIO.h"
#include "PathUtf8.h"
#include "TextModel.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using core::BlockType;
using core::Document;
using core::TextBlock;
using core::TextStyle;

int main() {
    // --- Test 1 : sérialisation et aller-retour complet ---
    {
        Document doc;
        doc.metadata().title = "Mon roman";
        doc.metadata().author = "Autrice Test";

        TextBlock heading(BlockType::Heading1);
        heading.insertText(0, U"Chapitre 1");
        doc.blocks().push_back(heading);

        TextBlock para;
        para.insertText(0, U"Un début en fanfare.");
        para.applyStyle(3, 5, TextStyle::Bold); // "début"
        doc.blocks().push_back(para);

        const std::string md = io::serialize(doc);
        assert(md.find("title: \"Mon roman\"") != std::string::npos);
        assert(md.find("author: \"Autrice Test\"") != std::string::npos);
        assert(md.find("# Chapitre 1") != std::string::npos);
        assert(md.find("**début**") != std::string::npos);

        const Document back = io::parse(md);
        assert(back.metadata().title == "Mon roman");
        assert(back.metadata().author == "Autrice Test");
        assert(back.blocks().size() == 2);
        assert(back.blocks()[0].type() == BlockType::Heading1);
        assert(back.blocks()[0].text() == U"Chapitre 1");
        assert(back.blocks()[1].text() == U"Un début en fanfare.");
        assert(core::hasStyle(back.blocks()[1].styleAt(3), TextStyle::Bold));
        assert(core::hasStyle(back.blocks()[1].styleAt(7), TextStyle::Bold));
        assert(!core::hasStyle(back.blocks()[1].styleAt(2), TextStyle::Bold));
        assert(!core::hasStyle(back.blocks()[1].styleAt(8), TextStyle::Bold));
        std::cout << "Test 1 OK (aller-retour complet)\n";
    }

    // --- Test 2 : style sur une PARTIE d'un mot ("1er" en exposant) ---
    {
        Document doc;
        TextBlock para;
        para.insertText(0, U"Le 1er jour");
        para.applyStyle(4, 2, TextStyle::Superscript); // "er" seulement
        doc.blocks().push_back(para);

        const std::string md = io::serialize(doc);
        assert(md.find("1<sup>er</sup>") != std::string::npos);

        const Document back = io::parse(md);
        assert(back.blocks()[0].text() == U"Le 1er jour");
        assert(core::hasStyle(back.blocks()[0].styleAt(4), TextStyle::Superscript));
        assert(!core::hasStyle(back.blocks()[0].styleAt(3), TextStyle::Superscript));
        assert(!core::hasStyle(back.blocks()[0].styleAt(6), TextStyle::Superscript));
        std::cout << "Test 2 OK (exposant sur une partie de mot)\n";
    }

    // --- Test 3 : styles combinés et échappement des caractères spéciaux ---
    {
        Document doc;
        TextBlock para;
        para.insertText(0, U"2 * 3 = 6 #calcul <fin>");
        doc.blocks().push_back(para);

        TextBlock styled;
        styled.insertText(0, U"gras italique");
        styled.applyStyle(0, 13, TextStyle::Bold);
        styled.applyStyle(5, 8, TextStyle::Italic); // " italique" en gras+italique
        doc.blocks().push_back(styled);

        const std::string md = io::serialize(doc);
        const Document back = io::parse(md);
        // Les caractères spéciaux tapés par l'auteur ressortent intacts...
        assert(back.blocks()[0].text() == U"2 * 3 = 6 #calcul <fin>");
        // ...sans avoir été interprétés comme du formatage.
        assert(back.blocks()[0].styleAt(2) == TextStyle::None);
        // Et les styles combinés survivent à l'aller-retour.
        assert(core::hasStyle(back.blocks()[1].styleAt(0), TextStyle::Bold));
        assert(!core::hasStyle(back.blocks()[1].styleAt(0), TextStyle::Italic));
        assert(core::hasStyle(back.blocks()[1].styleAt(6), TextStyle::Bold));
        assert(core::hasStyle(back.blocks()[1].styleAt(6), TextStyle::Italic));
        std::cout << "Test 3 OK (echappement et styles combines)\n";
    }

    // --- Test 4 : parsing d'un Markdown écrit à la main (externe) ---
    {
        const std::string md =
            "# Titre\n"
            "\n"
            "Du **gras**, de l'*italique* et du _souligne_ standard.\n"
            "Suite du paragraphe, et mon_fichier_config reste intact.\n"
            "\n"
            "### Sous-partie\n";

        const Document doc = io::parse(md);
        assert(doc.blocks().size() == 3);
        assert(doc.blocks()[0].type() == BlockType::Heading1);
        assert(doc.blocks()[2].type() == BlockType::Heading3);

        // Les deux lignes consécutives forment UN paragraphe.
        const TextBlock& para = doc.blocks()[1];
        const std::u32string& text = para.text();
        assert(text.find(U"Suite du paragraphe") != std::u32string::npos);

        // "gras" est en gras, "italique" en italique.
        const size_t boldPos = text.find(U"gras");
        assert(core::hasStyle(para.styleAt(boldPos), TextStyle::Bold));
        const size_t italicPos = text.find(U"italique");
        assert(core::hasStyle(para.styleAt(italicPos), TextStyle::Italic));

        // "_souligne_" : underscore en bordure de mot = italique Markdown.
        const size_t underPos = text.find(U"souligne");
        assert(core::hasStyle(para.styleAt(underPos), TextStyle::Italic));

        // "mon_fichier_config" : underscores INTRA-mot, restés littéraux.
        assert(text.find(U"mon_fichier_config") != std::u32string::npos);
        std::cout << "Test 4 OK (markdown externe)\n";
    }

    // --- Test 5 : sauvegarde atomique puis relecture ---
    {
        namespace fs = std::filesystem;
        const fs::path path = fs::temp_directory_path() / "test_editeur_markdown.md";

        Document doc;
        doc.metadata().title = "Titre \"cite\" avec \\ antislash";
        TextBlock para;
        para.insertText(0, U"Contenu à sauvegarder.");
        doc.blocks().push_back(para);

        std::string error;
        assert(io::saveToFile(doc, path, &error));
        assert(fs::exists(path));
        fs::path tmp = path;
        tmp += ".tmp";
        assert(!fs::exists(tmp)); // pas de fichier temporaire laissé derrière

        // Écrasement du fichier existant : même chemin, nouveau contenu.
        doc.blocks()[0].insertText(0, U"V2 : ");
        assert(io::saveToFile(doc, path, &error));

        Document loaded;
        assert(io::loadFromFile(loaded, path, &error));
        assert(loaded.metadata().title == "Titre \"cite\" avec \\ antislash");
        assert(loaded.blocks().size() == 1);
        assert(loaded.blocks()[0].text() == U"V2 : Contenu à sauvegarder.");

        fs::remove(path); // nettoyage
        std::cout << "Test 5 OK (sauvegarde atomique)\n";
    }

    // --- Test 6 : entrées dégradées, jamais de plantage ---
    {
        // Vide : un document avec un bloc prêt à l'emploi.
        const Document empty = io::parse("");
        assert(empty.blocks().size() == 1);
        assert(empty.blocks()[0].text().empty());

        // En-tête YAML jamais fermé : traité comme du contenu.
        const Document broken = io::parse("---\ntitle: \"perdu\"");
        assert(!broken.blocks().empty());
        assert(broken.metadata().title.empty());

        // Marqueur jamais fermé : le style s'arrête au bout du bloc.
        const Document unclosed = io::parse("du **gras jamais ferme\n");
        assert(unclosed.blocks()[0].text() == U"du gras jamais ferme");
        assert(core::hasStyle(unclosed.blocks()[0].styleAt(5), TextStyle::Bold));

        // Fichier inexistant : échec propre avec message.
        Document target;
        std::string error;
        assert(!io::loadFromFile(target, "Z:/nulle/part/fichier.md", &error));
        assert(!error.empty());
        std::cout << "Test 6 OK (entrees degradees)\n";
    }

    // --- Test 7 : sauvegardes horodatées + purge ---
    {
        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "test_editeur_backup.md";
        const fs::path dir = fs::temp_directory_path() / "test_editeur_backup_backups";
        std::error_code ec;
        fs::remove_all(dir, ec); // repartir propre

        Document doc;
        doc.blocks().emplace_back();
        doc.blocks()[0].insertText(0, U"Contenu à sauvegarder");

        // Première sauvegarde (sans purge) : le dossier et le fichier existent.
        std::string outPath, err;
        assert(io::writeBackup(doc, base, 0, &outPath, &err));
        assert(fs::exists(dir));
        assert(fs::exists(outPath));

        // Simule des sauvegardes plus anciennes (horodatage 2020, donc
        // triées avant celle qu'on vient d'écrire).
        for (const char* ts : {"2020-01-01_00-00-01", "2020-01-01_00-00-02", "2020-01-01_00-00-03"}) {
            std::ofstream(dir / (std::string("test_editeur_backup_") + ts + ".md")) << "ancien";
        }

        // Nouvelle sauvegarde avec purge à 2 : il ne doit rester que les 2
        // plus récentes.
        assert(io::writeBackup(doc, base, 2, &outPath, &err));
        size_t remaining = 0;
        for (const auto& e : fs::directory_iterator(dir)) { if (e.is_regular_file()) ++remaining; }
        assert(remaining == 2);
        // La plus ancienne a bien été supprimée ; la dernière écrite subsiste.
        assert(!fs::exists(dir / "test_editeur_backup_2020-01-01_00-00-01.md"));
        assert(fs::exists(outPath));

        fs::remove_all(dir, ec); // nettoyage
        std::cout << "Test 7 OK (backups horodates + purge)\n";
    }

    // --- Test 8 : chemins ACCENTUÉS (UTF-8) de bout en bout ---
    // Sur Windows, path::string() convertit vers la page de codes ANSI :
    // ce test échouerait si l'on repassait par des chaînes étroites.
    {
        namespace fs = std::filesystem;
        const std::string nomUtf8 = "Écrits d'été";
        const fs::path dossier = fs::temp_directory_path() / "test_editeur_accents";
        std::error_code ec;
        fs::remove_all(dossier, ec);
        fs::create_directories(dossier, ec);

        const fs::path fichier = dossier / utils::pathFromUtf8(nomUtf8 + ".md");

        Document doc;
        doc.metadata().title = "Été";
        doc.blocks().emplace_back();
        doc.blocks()[0].insertText(0, U"Un château près de la forêt.");

        // Aller-retour disque avec un nom de fichier accentué.
        std::string err;
        assert(io::saveToFile(doc, fichier, &err));
        assert(fs::exists(fichier));

        Document relu;
        assert(io::loadFromFile(relu, fichier, &err));
        assert(relu.metadata().title == "Été");
        assert(relu.blocks()[0].text() == U"Un château près de la forêt.");

        // Le chemin rendu en UTF-8 doit être identique à celui fourni.
        assert(utils::pathToUtf8(fichier).find(nomUtf8) != std::string::npos);

        // Sauvegarde horodatée : le dossier et le fichier gardent les accents.
        std::string backupPath;
        assert(io::writeBackup(doc, fichier, 5, &backupPath, &err));
        assert(fs::exists(utils::pathFromUtf8(backupPath)));
        assert(backupPath.find(nomUtf8) != std::string::npos); // pas de mojibake
        assert(fs::exists(dossier / utils::pathFromUtf8(nomUtf8 + "_backups")));

        // Message d'erreur : le chemin doit y figurer en UTF-8 lisible.
        Document cible;
        std::string erreur;
        const fs::path absent = dossier / utils::pathFromUtf8("Rêve absent.md");
        assert(!io::loadFromFile(cible, absent, &erreur));
        assert(erreur.find("Rêve absent") != std::string::npos);

        fs::remove_all(dossier, ec);
        std::cout << "Test 8 OK (chemins accentues, UTF-8 de bout en bout)\n";
    }

    // --- Test 9 : taille de caractères (<span font-size>) aller-retour ---
    {
        Document doc;
        TextBlock para;
        para.insertText(0, U"Un mot agrandi ici.");
        para.setFontSize(3, 11, 150);           // "mot agrandi" à 150 %
        para.applyStyle(3, 3, TextStyle::Bold);  // "mot" aussi en gras
        doc.blocks().push_back(para);

        const std::string md = io::serialize(doc);
        // La taille sort en em (150 % -> 1.5em), le gras est imbriqué dedans.
        assert(md.find("<span style=\"font-size:1.5em\">") != std::string::npos);
        assert(md.find("</span>") != std::string::npos);
        assert(md.find("**mot**") != std::string::npos);

        const Document back = io::parse(md);
        const TextBlock& b = back.blocks()[0];
        assert(b.text() == U"Un mot agrandi ici.");
        assert(b.sizeAt(3) == 150);   // début de "mot"
        assert(b.sizeAt(13) == 150);  // fin de "agrandi"
        assert(b.sizeAt(2) == 100);   // espace avant : normal
        assert(b.sizeAt(14) == 100);  // espace après : normal
        assert(core::hasStyle(b.styleAt(3), TextStyle::Bold)); // le gras survit
        assert(!core::hasStyle(b.styleAt(7), TextStyle::Bold));
        std::cout << "Test 9 OK (taille de caracteres, aller-retour)\n";
    }

    // --- Test 10 : taille fractionnaire + remise à 100 % (pas de span) ---
    {
        Document doc;
        TextBlock para;
        para.insertText(0, U"AB CD EF");
        para.setFontSize(0, 2, 125);   // "AB" à 125 % (1.25em)
        para.setFontSize(6, 2, 200);   // "EF" à 200 %
        para.setFontSize(6, 2, 100);   // ...puis remis à normal : doit disparaître
        doc.blocks().push_back(para);

        const std::string md = io::serialize(doc);
        assert(md.find("font-size:1.25em") != std::string::npos);
        // "EF" remis à 100 % : aucune seconde balise de taille ne subsiste.
        assert(md.find("font-size:2em") == std::string::npos);

        const Document back = io::parse(md);
        assert(back.blocks()[0].sizeAt(0) == 125);
        assert(back.blocks()[0].sizeAt(6) == 100); // "E" redevenu normal
        std::cout << "Test 10 OK (taille fractionnaire + remise a zero)\n";
    }

    // --- Test 11 : couleur du texte, aller-retour (+ combinée à taille/style) ---
    {
        const uint32_t rouge = core::colorFromHex("#cf1124");
        Document doc;
        TextBlock para;
        para.insertText(0, U"Un mot rouge ici.");
        para.setColor(3, 9, rouge);                 // "mot rouge"
        para.applyStyle(3, 3, TextStyle::Bold);      // "mot" aussi en gras
        para.setFontSize(7, 5, 150);                 // "rouge" aussi agrandi
        doc.blocks().push_back(para);

        const std::string md = io::serialize(doc);
        assert(md.find("color:#cf1124") != std::string::npos);
        // Taille et couleur voyagent dans le MÊME span.
        assert(md.find("font-size:1.5em;color:#cf1124") != std::string::npos);

        const Document back = io::parse(md);
        const TextBlock& b = back.blocks()[0];
        assert(b.text() == U"Un mot rouge ici.");
        assert(b.colorAt(3) == rouge);   // début de "mot"
        assert(b.colorAt(11) == rouge);  // fin de "rouge"
        assert(b.colorAt(2) == 0);       // espace avant : couleur par défaut
        assert(b.colorAt(12) == 0);      // espace après
        assert(core::hasStyle(b.styleAt(3), TextStyle::Bold)); // le gras survit
        assert(b.sizeAt(7) == 150);                            // la taille survit
        std::cout << "Test 11 OK (couleur du texte, aller-retour)\n";
    }

    std::cout << "Tous les tests MarkdownIO sont passes.\n";
    return 0;
}
