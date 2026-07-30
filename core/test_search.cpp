// test_search.cpp
// Tests de la recherche / remplacement (core::Search), sans pont.

#include "Search.h"
#include "TextModel.h"

#include <cassert>
#include <initializer_list>
#include <iostream>

using core::Document;
using core::SearchHit;
using core::SearchOptions;
using core::TextStyle;

static Document makeDoc(std::initializer_list<const char32_t*> texts) {
    Document d;
    for (const char32_t* t : texts) {
        d.blocks().emplace_back();
        d.blocks().back().insertText(0, t);
    }
    return d;
}

int main() {
    // Document de référence.
    //  bloc0 : "le chat le chien"  -> "le" en 0 et 8
    //  bloc1 : "un LE loup"        -> "LE" en 3 (casse)
    //  bloc2 : "vallee"            -> "le" en 3 (à l'intérieur d'un mot)
    Document doc = makeDoc({U"le chat le chien", U"un LE loup", U"vallee"});
    const size_t N = doc.blocks().size();
    SearchOptions ci;  // insensible à la casse (défaut)
    SearchOptions cs; cs.caseSensitive = true;
    SearchOptions ww; ww.wholeWord = true;

    // --- Test 1 : recherche insensible à la casse, total + index ---
    {
        SearchHit h = core::findNext(doc, U"le", 0, N, 0, 0, ci, false, true);
        assert(h.found);
        assert(h.block == 0 && h.start == 0 && h.end == 2);
        assert(h.total == 4); // (0,0)(0,8)(1,3 "LE")(2,3)
        assert(h.index == 1);
        std::cout << "Test 1 OK (recherche insensible a la casse)\n";
    }

    // --- Test 2 : recherche sensible à la casse ("LE" exclu) ---
    {
        assert(core::countMatches(doc, U"le", 0, N, cs) == 3); // (0,0)(0,8)(2,3)
        SearchHit h = core::findNext(doc, U"le", 0, N, 0, 1, cs, false, true);
        assert(h.found && h.block == 0 && h.start == 8); // le suivant après pos 1
        assert(h.index == 2 && h.total == 3);
        std::cout << "Test 2 OK (sensible a la casse + position de depart)\n";
    }

    // --- Test 3 : mots entiers ("le" dans "vallee" exclu) ---
    {
        assert(core::countMatches(doc, U"le", 0, N, ww) == 3); // 2 dans bloc0 + "LE"
        SearchHit h = core::findNext(doc, U"le", 0, N, 2, 0, ww, false, false);
        assert(!h.found); // rien en mots entiers à partir du bloc 2 sans enroulement
        std::cout << "Test 3 OK (mots entiers)\n";
    }

    // --- Test 4 : recherche en arrière ---
    {
        SearchHit h = core::findNext(doc, U"le", 0, N, 2, 0, ci, true, true);
        assert(h.found && h.block == 1 && h.start == 3); // dernière occurrence avant (2,0)
        std::cout << "Test 4 OK (recherche en arriere)\n";
    }

    // --- Test 5 : enroulement ---
    {
        // Après la dernière occurrence, sans enroulement : rien.
        SearchHit noWrap = core::findNext(doc, U"le", 0, N, 2, 5, ci, false, false);
        assert(!noWrap.found);
        // Avec enroulement : on revient à la première.
        SearchHit wrap = core::findNext(doc, U"le", 0, N, 2, 5, ci, false, true);
        assert(wrap.found && wrap.block == 0 && wrap.start == 0);
        std::cout << "Test 5 OK (enroulement)\n";
    }

    // --- Test 6 : portée restreinte (un seul bloc) ---
    {
        assert(core::countMatches(doc, U"le", 0, 1, ci) == 2); // bloc0 uniquement
        SearchHit h = core::findNext(doc, U"le", 1, 2, 1, 0, ci, false, false);
        assert(h.found && h.block == 1); // portée = bloc1 seulement
        assert(h.total == 1);
        std::cout << "Test 6 OK (portee restreinte)\n";
    }

    // --- Test 7 : requête vide et introuvable ---
    {
        assert(!core::findNext(doc, U"", 0, N, 0, 0, ci, false, true).found);
        SearchHit h = core::findNext(doc, U"zzz", 0, N, 0, 0, ci, false, true);
        assert(!h.found && h.total == 0);
        std::cout << "Test 7 OK (vide / introuvable)\n";
    }

    // --- Test 8 : remplacement de toutes les occurrences ---
    {
        Document d = makeDoc({U"le chat le chien", U"un LE loup", U"vallee"});
        const size_t count = core::replaceAll(d, U"le", U"X", 0, d.blocks().size(), ci);
        assert(count == 4);
        assert(d.blocks()[0].text() == U"X chat X chien");
        assert(d.blocks()[1].text() == U"un X loup");
        assert(d.blocks()[2].text() == U"valXe");
        std::cout << "Test 8 OK (remplacer tout, insensible a la casse)\n";
    }

    // --- Test 9 : remplacement dans une portée + styles préservés ---
    {
        Document d = makeDoc({U"abcXYdef", U"XY ailleurs"});
        d.blocks()[0].applyStyle(0, 8, TextStyle::Bold); // tout le bloc0 en gras

        // Portée = bloc0 uniquement : le "XY" du bloc1 n'est pas touché.
        const size_t count = core::replaceAll(d, U"XY", U"Z", 0, 1, ci);
        assert(count == 1);
        assert(d.blocks()[0].text() == U"abcZdef");
        assert(d.blocks()[1].text() == U"XY ailleurs"); // hors portée
        // Le texte autour du remplacement reste en gras.
        assert(core::hasStyle(d.blocks()[0].styleAt(0), TextStyle::Bold)); // "a"
        assert(core::hasStyle(d.blocks()[0].styleAt(6), TextStyle::Bold)); // "f"
        std::cout << "Test 9 OK (remplacement borne a la portee, styles preserves)\n";
    }

    // --- Test 10 : suppression via remplacement par une chaîne vide ---
    {
        Document d = makeDoc({U"a-b-c-d"});
        const size_t count = core::replaceAll(d, U"-", U"", 0, 1, ci);
        assert(count == 3);
        assert(d.blocks()[0].text() == U"abcd");
        std::cout << "Test 10 OK (suppression par remplacement vide)\n";
    }

    std::cout << "Tous les tests Search sont passes.\n";
    return 0;
}
