// Test manuel rapide, avant de passer à un vrai framework de tests
// (ex: Catch2 ou GoogleTest) en Phase 1. Sert ici uniquement à valider
// que le modèle se comporte comme prévu avant de le documenter au client.

#include "TextModel.h"
#include <cassert>
#include <iostream>

using namespace core;

static std::u32string U(const std::string& s) {
    return std::u32string(s.begin(), s.end());
}

int main() {
    // --- Test 1 : insertion simple + style sur un mot ---
    {
        TextBlock block;
        block.insertText(0, U("Bonjour le monde"));
        block.applyStyle(0, 7, TextStyle::Bold); // "Bonjour" en gras
        assert(hasStyle(block.styleAt(0), TextStyle::Bold));
        assert(hasStyle(block.styleAt(6), TextStyle::Bold));
        assert(!hasStyle(block.styleAt(7), TextStyle::Bold)); // l'espace après
        std::cout << "Test 1 OK\n";
    }

    // --- Test 2 : style partiel à l'intérieur d'un mot (cas "1er") ---
    {
        TextBlock block;
        block.insertText(0, U("1er chapitre"));
        block.applyStyle(1, 2, TextStyle::Superscript); // "er" en exposant
        assert(!hasStyle(block.styleAt(0), TextStyle::Superscript)); // '1'
        assert(hasStyle(block.styleAt(1), TextStyle::Superscript));  // 'e'
        assert(hasStyle(block.styleAt(2), TextStyle::Superscript));  // 'r'
        assert(!hasStyle(block.styleAt(3), TextStyle::Superscript)); // ' '
        std::cout << "Test 2 OK\n";
    }

    // --- Test 3 : suppression au milieu d'une plage stylée ---
    {
        TextBlock block;
        block.insertText(0, U("gras et italique"));
        block.applyStyle(0, 4, TextStyle::Bold);      // "gras"
        block.applyStyle(8, 8, TextStyle::Italic);    // "italique"
        block.removeText(2, 2);                        // retire "as", reste "gr et italique"
        assert(block.text() == U("gr et italique"));
        assert(hasStyle(block.styleAt(0), TextStyle::Bold)); // 'g' toujours gras
        std::cout << "Test 3 OK\n";
    }

    // --- Test 4 : limites de mots (utilisé par la barre d'outils) ---
    {
        auto text = U("le chat noir");
        auto range = WordBoundary::wordAt(text, 4); // dans "chat"
        assert(range.start == 3 && range.end == 7);
        auto onSpace = WordBoundary::wordAt(text, 2); // sur l'espace
        assert(onSpace.start == onSpace.end);
        std::cout << "Test 4 OK\n";
    }

    // --- Test 5 : comptage de mots ---
    {
        Document doc;
        TextBlock block;
        block.insertText(0, U("Il etait une fois, dans un royaume lointain."));
        doc.blocks().push_back(block);
        assert(doc.wordCount() == 8);
        std::cout << "Test 5 OK\n";
    }

    std::cout << "Tous les tests sont passes.\n";
    return 0;
}
