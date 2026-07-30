// test_undo.cpp
// Tests de l'historique d'annulation groupé par mot (UndoManager),
// sans pont ni webview : uniquement le modèle, comme test_model.cpp.

#include "TextModel.h"
#include "UndoManager.h"

#include <cassert>
#include <iostream>

using core::Document;
using core::TextBlock;
using core::TextStyle;
using core::UndoManager;

// Simule la frappe d'une chaîne caractère par caractère à la fin du
// bloc, en enregistrant chaque frappe comme le fera la couche API.
static void typeText(Document& doc, UndoManager& undo, size_t blockIndex,
                     const std::u32string& text) {
    for (char32_t ch : text) {
        TextBlock& block = doc.blocks()[blockIndex];
        TextBlock before = block;
        const size_t pos = block.length();
        const std::u32string one(1, ch);
        block.insertText(pos, one);
        undo.recordTyping(blockIndex, pos, one, std::move(before), block);
    }
}

// Simule un retour arrière (suppression du dernier caractère du bloc).
static void backspace(Document& doc, UndoManager& undo, size_t blockIndex) {
    TextBlock& block = doc.blocks()[blockIndex];
    if (block.length() == 0) return;
    TextBlock before = block;
    const size_t pos = block.length() - 1;
    const std::u32string removed(1, block.text()[pos]);
    block.removeText(pos, 1);
    undo.recordDeletion(blockIndex, pos, removed, std::move(before), block);
}

int main() {
    // --- Test 1 : la frappe d'un mot = UNE seule étape ---
    {
        Document doc;
        doc.blocks().emplace_back();
        UndoManager undo;

        typeText(doc, undo, 0, U"Bonjour");
        assert(doc.blocks()[0].text() == U"Bonjour");

        assert(undo.undo(doc).has_value());
        assert(doc.blocks()[0].text().empty());
        assert(!undo.canUndo()); // une seule étape, pas sept

        assert(undo.redo(doc).has_value());
        assert(doc.blocks()[0].text() == U"Bonjour");
        std::cout << "Test 1 OK (un mot tape = une etape)\n";
    }

    // --- Test 2 : mots et séparateurs forment des groupes distincts ---
    {
        Document doc;
        doc.blocks().emplace_back();
        UndoManager undo;

        typeText(doc, undo, 0, U"un beau"); // groupes attendus : "un", " ", "beau"
        assert(undo.undo(doc).has_value());
        assert(doc.blocks()[0].text() == U"un ");
        assert(undo.undo(doc).has_value());
        assert(doc.blocks()[0].text() == U"un");
        assert(undo.undo(doc).has_value());
        assert(doc.blocks()[0].text().empty());
        assert(!undo.canUndo());
        std::cout << "Test 2 OK (groupes mot / separateur / mot)\n";
    }

    // --- Test 3 : retours arrière regroupés par mot aussi ---
    {
        Document doc;
        doc.blocks().emplace_back();
        doc.blocks()[0].insertText(0, U"un mot"); // contenu initial, hors historique
        UndoManager undo;

        // Efface "mot" (3 retours arrière) puis l'espace (1 de plus).
        backspace(doc, undo, 0);
        backspace(doc, undo, 0);
        backspace(doc, undo, 0);
        backspace(doc, undo, 0);
        assert(doc.blocks()[0].text() == U"un");

        assert(undo.undo(doc).has_value()); // annule la suppression de l'espace
        assert(doc.blocks()[0].text() == U"un ");
        assert(undo.undo(doc).has_value()); // annule la suppression de "mot" d'un coup
        assert(doc.blocks()[0].text() == U"un mot");
        assert(!undo.canUndo());
        std::cout << "Test 3 OK (suppressions groupees)\n";
    }

    // --- Test 4 : un changement de style est une étape isolée ---
    {
        Document doc;
        doc.blocks().emplace_back();
        UndoManager undo;

        typeText(doc, undo, 0, U"mot");
        TextBlock before = doc.blocks()[0];
        doc.blocks()[0].applyStyle(0, 3, TextStyle::Bold);
        undo.recordOther(0, std::move(before), doc.blocks()[0]);
        assert(core::hasStyle(doc.blocks()[0].styleAt(0), TextStyle::Bold));

        assert(undo.undo(doc).has_value()); // annule le gras, pas le texte
        assert(doc.blocks()[0].text() == U"mot");
        assert(!core::hasStyle(doc.blocks()[0].styleAt(0), TextStyle::Bold));
        std::cout << "Test 4 OK (style = etape isolee)\n";
    }

    // --- Test 5 : une nouvelle frappe invalide le rétablissement ---
    {
        Document doc;
        doc.blocks().emplace_back();
        UndoManager undo;

        typeText(doc, undo, 0, U"ab");
        assert(undo.undo(doc).has_value());
        assert(undo.canRedo());

        typeText(doc, undo, 0, U"c"); // nouvelle branche d'historique
        assert(!undo.canRedo());
        assert(doc.blocks()[0].text() == U"c");
        std::cout << "Test 5 OK (redo invalide par une frappe)\n";
    }

    // --- Test 6 : breakGroup coupe le regroupement (clic, flèches) ---
    {
        Document doc;
        doc.blocks().emplace_back();
        UndoManager undo;

        typeText(doc, undo, 0, U"ab");
        undo.breakGroup(); // l'utilisateur a déplacé le curseur
        typeText(doc, undo, 0, U"cd"); // même mot, mais groupe distinct

        assert(undo.undo(doc).has_value());
        assert(doc.blocks()[0].text() == U"ab"); // seul "cd" est annulé
        std::cout << "Test 6 OK (breakGroup)\n";
    }

    // --- Test 7 : la limite d'historique oublie les plus anciennes ---
    {
        Document doc;
        doc.blocks().emplace_back();
        UndoManager undo(2); // limite volontairement minuscule

        typeText(doc, undo, 0, U"a");
        undo.breakGroup();
        typeText(doc, undo, 0, U"b");
        undo.breakGroup();
        typeText(doc, undo, 0, U"c"); // 3 étapes, la première est oubliée

        assert(undo.undo(doc).has_value());
        assert(undo.undo(doc).has_value());
        assert(!undo.canUndo()); // impossible de remonter avant "a"
        assert(doc.blocks()[0].text() == U"a");
        std::cout << "Test 7 OK (limite d'historique)\n";
    }

    // --- Test 8 : piles vides et bloc disparu -> refus propre ---
    {
        Document doc;
        doc.blocks().emplace_back();
        UndoManager undo;

        assert(!undo.undo(doc).has_value()); // rien à annuler : pas de plantage
        assert(!undo.redo(doc).has_value());

        typeText(doc, undo, 0, U"x");
        doc.blocks().clear(); // incohérence simulée : le bloc n'existe plus
        assert(!undo.undo(doc).has_value()); // refus, pas d'accès hors limites
        std::cout << "Test 8 OK (cas degrades)\n";
    }

    // --- Test 9 : clear() après chargement d'un fichier ---
    {
        Document doc;
        doc.blocks().emplace_back();
        UndoManager undo;

        typeText(doc, undo, 0, U"abc");
        undo.clear();
        assert(!undo.canUndo() && !undo.canRedo());
        std::cout << "Test 9 OK (clear)\n";
    }

    // --- Test 10 : annulation d'une coupe de bloc (structurel) ---
    {
        Document doc;
        doc.blocks().emplace_back();
        doc.blocks()[0].insertText(0, U"un deux");
        UndoManager undo;

        std::vector<TextBlock> before = doc.blocks();
        doc.splitBlock(0, 3); // "un " | "deux"
        undo.recordStructural(std::move(before), doc.blocks());
        assert(doc.blocks().size() == 2);

        auto info = undo.undo(doc);
        assert(info.has_value());
        assert(info->wholeDocument); // le JS doit tout re-rendre
        assert(doc.blocks().size() == 1);
        assert(doc.blocks()[0].text() == U"un deux");

        auto redo = undo.redo(doc);
        assert(redo.has_value() && redo->wholeDocument);
        assert(doc.blocks().size() == 2);
        std::cout << "Test 10 OK (undo/redo d'une coupe de bloc)\n";
    }

    // --- Test 11 : étapes par bloc et structurelles mélangées ---
    {
        Document doc;
        doc.blocks().emplace_back();
        UndoManager undo;

        typeText(doc, undo, 0, U"ab");           // étape par bloc
        std::vector<TextBlock> before = doc.blocks();
        doc.splitBlock(0, 1);                     // étape structurelle
        undo.recordStructural(std::move(before), doc.blocks());
        typeText(doc, undo, 1, U"cd");            // étape par bloc sur le nouveau bloc

        // On remonte tout l'historique dans l'ordre inverse, sans jamais
        // d'incohérence d'index.
        assert(undo.undo(doc).has_value()); // annule "cd"
        assert(doc.blocks()[1].text() == U"b");
        assert(undo.undo(doc).has_value()); // annule la coupe
        assert(doc.blocks().size() == 1);
        assert(doc.blocks()[0].text() == U"ab");
        assert(undo.undo(doc).has_value()); // annule "ab"
        assert(doc.blocks()[0].text().empty());
        assert(!undo.canUndo());
        std::cout << "Test 11 OK (etapes par bloc + structurelles melangees)\n";
    }

    std::cout << "Tous les tests UndoManager sont passes.\n";
    return 0;
}
