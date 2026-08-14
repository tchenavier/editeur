// test_blocks.cpp
// Tests des opérations structurelles du modèle (Document::splitBlock,
// mergeWithPrevious, setBlockType), sans pont ni webview.

#include "TextModel.h"

#include <cassert>
#include <iostream>

using core::BlockType;
using core::Document;
using core::TextStyle;

int main() {
    // --- Test 1 : couper un bloc au milieu, styles préservés ---
    {
        Document doc;
        doc.blocks().emplace_back();
        doc.blocks()[0].insertText(0, U"Bonjour le monde");
        doc.blocks()[0].applyStyle(0, 7, TextStyle::Bold); // "Bonjour"

        doc.splitBlock(0, 8); // "Bonjour " | "le monde"
        assert(doc.blocks().size() == 2);
        assert(doc.blocks()[0].text() == U"Bonjour ");
        assert(doc.blocks()[1].text() == U"le monde");
        assert(doc.blocks()[1].type() == BlockType::Paragraph);
        assert(core::hasStyle(doc.blocks()[0].styleAt(0), TextStyle::Bold));
        assert(!core::hasStyle(doc.blocks()[1].styleAt(0), TextStyle::Bold));
        std::cout << "Test 1 OK (coupe simple, styles preserves)\n";
    }

    // --- Test 2 : coupe À L'INTÉRIEUR d'une plage stylée ---
    {
        Document doc;
        doc.blocks().emplace_back();
        doc.blocks()[0].insertText(0, U"abcdef");
        doc.blocks()[0].applyStyle(2, 2, TextStyle::Italic); // "cd"

        doc.splitBlock(0, 3); // "abc" | "def", la plage "cd" est coupée
        assert(doc.blocks()[0].text() == U"abc");
        assert(doc.blocks()[1].text() == U"def");
        assert(core::hasStyle(doc.blocks()[0].styleAt(2), TextStyle::Italic)); // "c"
        assert(core::hasStyle(doc.blocks()[1].styleAt(0), TextStyle::Italic)); // "d"
        assert(!core::hasStyle(doc.blocks()[1].styleAt(1), TextStyle::Italic)); // "e"
        std::cout << "Test 2 OK (coupe au milieu d'un style)\n";
    }

    // --- Test 3 : couper un titre -> la queue devient un paragraphe ---
    {
        Document doc;
        doc.blocks().emplace_back(BlockType::Heading1);
        doc.blocks()[0].insertText(0, U"Titre puis texte");

        doc.splitBlock(0, 6); // "Titre " (H1) | "puis texte" (paragraphe)
        assert(doc.blocks()[0].type() == BlockType::Heading1);
        assert(doc.blocks()[1].type() == BlockType::Paragraph);
        std::cout << "Test 3 OK (coupe d'un titre)\n";
    }

    // --- Test 4 : coupe en début et en fin de bloc ---
    {
        Document doc;
        doc.blocks().emplace_back();
        doc.blocks()[0].insertText(0, U"mot");

        doc.splitBlock(0, 0); // bloc vide avant, "mot" ensuite
        assert(doc.blocks().size() == 2);
        assert(doc.blocks()[0].text().empty());
        assert(doc.blocks()[1].text() == U"mot");

        doc.splitBlock(1, 3); // coupe en fin : "mot" | bloc vide
        assert(doc.blocks().size() == 3);
        assert(doc.blocks()[1].text() == U"mot");
        assert(doc.blocks()[2].text().empty());
        std::cout << "Test 4 OK (coupe en bord de bloc)\n";
    }

    // --- Test 5 : fusion avec le bloc précédent, styles préservés ---
    {
        Document doc;
        doc.blocks().emplace_back();
        doc.blocks()[0].insertText(0, U"Bonjour ");
        doc.blocks().emplace_back();
        doc.blocks()[1].insertText(0, U"monde");
        doc.blocks()[1].applyStyle(0, 5, TextStyle::Bold); // "monde" en gras

        doc.mergeWithPrevious(1);
        assert(doc.blocks().size() == 1);
        assert(doc.blocks()[0].text() == U"Bonjour monde");
        assert(!core::hasStyle(doc.blocks()[0].styleAt(0), TextStyle::Bold)); // "Bonjour"
        assert(core::hasStyle(doc.blocks()[0].styleAt(8), TextStyle::Bold));  // "monde"
        std::cout << "Test 5 OK (fusion, styles decales preserves)\n";
    }

    // --- Test 6 : la coupe suivie d'une fusion redonne l'original ---
    {
        Document doc;
        doc.blocks().emplace_back();
        doc.blocks()[0].insertText(0, U"un deux trois");
        doc.blocks()[0].applyStyle(3, 4, TextStyle::Underline); // "deux"

        doc.splitBlock(0, 7);   // "un deux" | " trois"
        doc.mergeWithPrevious(1);
        assert(doc.blocks().size() == 1);
        assert(doc.blocks()[0].text() == U"un deux trois");
        assert(core::hasStyle(doc.blocks()[0].styleAt(3), TextStyle::Underline));
        assert(core::hasStyle(doc.blocks()[0].styleAt(6), TextStyle::Underline));
        assert(!core::hasStyle(doc.blocks()[0].styleAt(7), TextStyle::Underline));
        std::cout << "Test 6 OK (coupe + fusion = identite)\n";
    }

    // --- Test 7 : cas dégradés, jamais de plantage ni d'accès invalide ---
    {
        Document doc;
        doc.blocks().emplace_back();
        doc.blocks()[0].insertText(0, U"x");

        doc.mergeWithPrevious(0);   // pas de bloc précédent : sans effet
        assert(doc.blocks().size() == 1);
        doc.mergeWithPrevious(9);   // index hors limites : sans effet
        assert(doc.blocks().size() == 1);
        doc.splitBlock(9, 0);       // index hors limites : sans effet
        assert(doc.blocks().size() == 1);
        doc.splitBlock(0, 99);      // position hors limites : bornée à la fin
        assert(doc.blocks().size() == 2);
        assert(doc.blocks()[1].text().empty());
        doc.setBlockType(50, BlockType::Heading2); // hors limites : sans effet
        std::cout << "Test 7 OK (cas degrades)\n";
    }

    // --- Test 8 : déplacement d'une plage de blocs (chapitres) ---
    {
        // Construit 4 blocs identifiables par leur texte : A B C D.
        auto make = [](const std::u32string& t) {
            core::TextBlock b; b.insertText(0, t); return b;
        };
        Document doc;
        doc.blocks().push_back(make(U"A"));
        doc.blocks().push_back(make(U"B"));
        doc.blocks().push_back(make(U"C"));
        doc.blocks().push_back(make(U"D"));
        auto order = [&doc]() {
            std::u32string s;
            for (const auto& b : doc.blocks()) s += b.text();
            return s;
        };

        // Déplace [C] (start=2,count=1) tout en tête (before=0) -> C A B D
        assert(doc.moveBlocks(2, 1, 0));
        assert(order() == U"CABD");

        // Déplace [C] (start=0,count=1) à la fin (before=4) -> A B D C
        assert(doc.moveBlocks(0, 1, 4));
        assert(order() == U"ABDC");

        // Déplace la plage [A,B] (start=0,count=2) avant D (before=3) ->
        // après retrait : D C ; insertion avant l'ancien D -> A B D C ...
        // (vérifie surtout l'ajustement d'indice) : D C -> insert à 1 : D A B C
        assert(doc.moveBlocks(0, 2, 3));
        assert(order() == U"DABC");
        std::cout << "Test 8 OK (deplacement de plages)\n";
    }

    // --- Test 9 : moveBlocks sans effet -> retourne false ---
    {
        auto make = [](const std::u32string& t) {
            core::TextBlock b; b.insertText(0, t); return b;
        };
        Document doc;
        doc.blocks().push_back(make(U"A"));
        doc.blocks().push_back(make(U"B"));
        doc.blocks().push_back(make(U"C"));

        assert(!doc.moveBlocks(1, 1, 1)); // destination = position actuelle
        assert(!doc.moveBlocks(1, 1, 2)); // juste après soi-même : sans effet
        assert(!doc.moveBlocks(5, 1, 0)); // start hors limites
        assert(!doc.moveBlocks(0, 0, 2)); // count nul
        // L'ordre n'a pas bougé.
        std::u32string s;
        for (const auto& b : doc.blocks()) s += b.text();
        assert(s == U"ABC");
        std::cout << "Test 9 OK (deplacements sans effet)\n";
    }

    // --- Test 10 : setFontSize / sizeAt + héritage à l'insertion ---
    {
        Document doc;
        doc.blocks().emplace_back();
        core::TextBlock& b = doc.blocks()[0];
        b.insertText(0, U"abcdef");
        b.setFontSize(2, 2, 150); // "cd" à 150 %
        assert(b.sizeAt(1) == 100);
        assert(b.sizeAt(2) == 150);
        assert(b.sizeAt(3) == 150);
        assert(b.sizeAt(4) == 100);

        // Insertion À L'INTÉRIEUR de la zone agrandie : hérite de la taille.
        b.insertText(3, U"XY");
        assert(b.text() == U"abcXYdef");
        assert(b.sizeAt(3) == 150);
        assert(b.sizeAt(4) == 150);

        // Remise à 100 % partout : les plages « par défaut » sont éliminées.
        b.setFontSize(0, b.length(), 100);
        for (size_t i = 0; i < b.length(); ++i) assert(b.sizeAt(i) == 100);
        assert(b.formatRuns().empty());
        std::cout << "Test 10 OK (setFontSize, sizeAt, heritage)\n";
    }

    // --- Test 11 : la taille suit coupe ET fusion (comme les styles) ---
    {
        Document doc;
        doc.blocks().emplace_back();
        doc.blocks()[0].insertText(0, U"petit GRAND petit");
        doc.blocks()[0].setFontSize(6, 5, 180); // "GRAND" à 180 %

        doc.splitBlock(0, 12); // "petit GRAND " | "petit"
        assert(doc.blocks()[0].sizeAt(6) == 180);  // "GRAND" reste agrandi
        assert(doc.blocks()[0].sizeAt(0) == 100);
        assert(doc.blocks()[1].sizeAt(0) == 100);  // "petit" normal

        doc.mergeWithPrevious(1); // recolle à l'identique
        assert(doc.blocks().size() == 1);
        assert(doc.blocks()[0].text() == U"petit GRAND petit");
        assert(doc.blocks()[0].sizeAt(6) == 180);
        assert(doc.blocks()[0].sizeAt(11) == 100);
        std::cout << "Test 11 OK (taille preservee par coupe + fusion)\n";
    }

    // --- Test 12 : couleur du texte, indépendante du style et de la taille ---
    {
        using core::colorFromHex;
        using core::colorToHex;
        const uint32_t rouge = colorFromHex("#cf1124");
        const uint32_t bleu = colorFromHex("#0a5ecc");
        assert(rouge != 0 && bleu != 0 && rouge != bleu);
        // Aller-retour hex : la conversion est stable.
        assert(colorToHex(rouge) == "#cf1124");
        assert(colorToHex(0) == "");           // couleur non définie -> vide
        assert(colorFromHex("#abc") == 0);     // format invalide -> non définie
        assert(colorToHex(colorFromHex("#000000")) == "#000000"); // noir explicite

        Document doc;
        doc.blocks().emplace_back();
        core::TextBlock& b = doc.blocks()[0];
        b.insertText(0, U"abcdef");
        b.setColor(2, 2, rouge);                 // "cd" en rouge
        b.applyStyle(2, 2, core::TextStyle::Bold); // ...et en gras
        b.setFontSize(2, 2, 150);                 // ...et agrandi
        assert(b.colorAt(1) == 0);
        assert(b.colorAt(2) == rouge);
        assert(b.colorAt(3) == rouge);
        assert(b.colorAt(4) == 0);
        // Les trois dimensions coexistent sur les mêmes caractères.
        assert(core::hasStyle(b.styleAt(2), core::TextStyle::Bold));
        assert(b.sizeAt(2) == 150);

        // Changer la couleur ne touche ni le style ni la taille.
        b.setColor(2, 2, bleu);
        assert(b.colorAt(2) == bleu);
        assert(core::hasStyle(b.styleAt(2), core::TextStyle::Bold));
        assert(b.sizeAt(2) == 150);

        // Remise à la couleur par défaut : la plage n'est pas perdue (gras+taille),
        // seule la couleur disparaît.
        b.setColor(0, b.length(), 0);
        assert(b.colorAt(2) == 0);
        assert(core::hasStyle(b.styleAt(2), core::TextStyle::Bold));
        assert(b.sizeAt(2) == 150);
        std::cout << "Test 12 OK (couleur independante du style et de la taille)\n";
    }

    // --- Test 13 : la couleur suit coupe ET fusion ---
    {
        const uint32_t vert = core::colorFromHex("#1a7f37");
        Document doc;
        doc.blocks().emplace_back();
        doc.blocks()[0].insertText(0, U"un DEUX trois");
        doc.blocks()[0].setColor(3, 4, vert); // "DEUX" en vert

        doc.splitBlock(0, 8); // "un DEUX " | "trois"
        assert(doc.blocks()[0].colorAt(3) == vert);
        assert(doc.blocks()[0].colorAt(0) == 0);
        assert(doc.blocks()[1].colorAt(0) == 0);

        doc.mergeWithPrevious(1);
        assert(doc.blocks().size() == 1);
        assert(doc.blocks()[0].text() == U"un DEUX trois");
        assert(doc.blocks()[0].colorAt(3) == vert);
        assert(doc.blocks()[0].colorAt(8) == 0);
        std::cout << "Test 13 OK (couleur preservee par coupe + fusion)\n";
    }

    std::cout << "Tous les tests des operations de bloc sont passes.\n";
    return 0;
}
