// test_htmltext.cpp
// Tests du rendu XHTML du texte (io/HtmlText) : échappement, styles,
// taille, imbrication bien formée.

#include "HtmlText.h"
#include "TextModel.h"

#include <cassert>
#include <iostream>

using core::TextBlock;
using core::TextStyle;

int main() {
    // Sortie non tamponnée : en cas d'assertion, on voit quand même les
    // tests déjà passés (le tampon serait perdu à l'arrêt brutal).
    std::cout << std::unitbuf;

    // --- Test 1 : échappement des caractères spéciaux ---
    {
        assert(io::htmlEscape("a < b & c > d \"e\"") ==
               "a &lt; b &amp; c &gt; d &quot;e&quot;");
        // Le texte de l'auteur ne peut jamais injecter de balise.
        assert(io::htmlEscape("<script>alert(1)</script>") ==
               "&lt;script&gt;alert(1)&lt;/script&gt;");
        std::cout << "Test 1 OK (echappement HTML)\n";
    }

    // --- Test 2 : styles combinés, imbrication bien formée ---
    {
        TextBlock b;
        b.insertText(0, U"gras et italique");
        b.applyStyle(0, 16, TextStyle::Bold);    // tout en gras
        // "italique" commence au 9e caractère (indice 8) et en fait 8.
        b.applyStyle(8, 8, TextStyle::Italic);   // "italique" en gras+italique

        const std::string html = io::renderBlockHtml(b);
        // "gras et " en gras seul, puis "italique" en gras+italique.
        assert(html.find("<strong>gras et </strong>") != std::string::npos);
        assert(html.find("<strong><em>italique</em></strong>") != std::string::npos);
        // Aucune balise ouvrante sans fermeture correspondante (bien formé) :
        // autant de <strong> que de </strong>, idem <em>.
        auto count = [&](const std::string& needle) {
            size_t n = 0, p = 0;
            while ((p = html.find(needle, p)) != std::string::npos) { ++n; p += needle.size(); }
            return n;
        };
        assert(count("<strong>") == count("</strong>"));
        assert(count("<em>") == count("</em>"));
        std::cout << "Test 2 OK (styles combines, bien formes)\n";
    }

    // --- Test 3 : exposant (1er) + échappement dans le contenu ---
    {
        TextBlock b;
        b.insertText(0, U"1er <test>");
        b.applyStyle(1, 2, TextStyle::Superscript); // "er"
        const std::string html = io::renderBlockHtml(b);
        assert(html.find("1<sup>er</sup>") != std::string::npos);
        // Le "<test>" tapé par l'auteur est échappé.
        assert(html.find("&lt;test&gt;") != std::string::npos);
        assert(html.find("<test>") == std::string::npos);
        std::cout << "Test 3 OK (exposant + echappement)\n";
    }

    // --- Test 4 : taille de caractères -> span font-size en em ---
    {
        TextBlock b;
        b.insertText(0, U"petit GRAND");
        b.setFontSize(6, 5, 150); // "GRAND" a 150 %
        const std::string html = io::renderBlockHtml(b);
        assert(html.find("<span style=\"font-size:1.5em\">GRAND</span>") != std::string::npos);
        std::cout << "Test 4 OK (taille en em)\n";
    }

    // --- Test 5 : taille + style combinés (span englobe le style) ---
    {
        TextBlock b;
        b.insertText(0, U"mot");
        b.setFontSize(0, 3, 125);
        b.applyStyle(0, 3, TextStyle::Bold);
        const std::string html = io::renderBlockHtml(b);
        // Le span (taille) est la couche externe, le gras à l'intérieur.
        assert(html.find("<span style=\"font-size:1.25em\"><strong>mot</strong></span>")
               != std::string::npos);
        std::cout << "Test 5 OK (taille + style imbriques)\n";
    }

    // --- Test 6 : bloc vide et texte nu ---
    {
        TextBlock empty;
        assert(io::renderBlockHtml(empty).empty());
        TextBlock plain;
        plain.insertText(0, U"juste du texte");
        assert(io::renderBlockHtml(plain) == "juste du texte");
        std::cout << "Test 6 OK (bloc vide / texte nu)\n";
    }

    std::cout << "Tous les tests HtmlText sont passes.\n";
    return 0;
}
