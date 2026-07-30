// HtmlText.cpp
// Implémentation du rendu XHTML du texte. Voir HtmlText.h.

#include "HtmlText.h"

#include "TextModel.h"
#include "Utf8.h"

#include <string>

namespace io {

namespace {

using core::TextStyle;

// Taille en em (pct/100) formatée sans zéros inutiles : 140 -> "1.4",
// 125 -> "1.25", 90 -> "0.9". Même convention que MarkdownIO, pour que
// les deux exports restent cohérents.
std::string emValue(uint16_t pct) {
    std::string em = std::to_string(pct / 100);
    const int frac = pct % 100;
    if (frac != 0) {
        em += '.';
        em += static_cast<char>('0' + frac / 10);
        if (frac % 10 != 0) em += static_cast<char>('0' + frac % 10);
    }
    return em;
}

} // namespace

std::string htmlEscape(const std::string& utf8) {
    std::string out;
    out.reserve(utf8.size() + 8);
    for (char c : utf8) {
        switch (c) {
            case '&': out += "&amp;";  break;
            case '<': out += "&lt;";   break;
            case '>': out += "&gt;";   break;
            case '"': out += "&quot;"; break;
            default:  out += c;        break;
        }
    }
    return out;
}

std::string renderBlockHtml(const core::TextBlock& block) {
    const std::u32string& text = block.text();
    std::string out;
    out.reserve(text.size() + 16);

    auto emitPlain = [&](size_t from, size_t to) {
        if (from < to) {
            out += htmlEscape(utils::u32ToUtf8(text.substr(from, to - from)));
        }
    };

    size_t pos = 0;
    for (const auto& run : block.formatRuns()) {
        // Texte non stylé avant la plage.
        if (run.start > pos) emitPlain(pos, run.start);

        // Ouvre les balises de la plage (extérieur -> intérieur) et prépare
        // leur fermeture dans l'ordre INVERSE (prepend = ordre miroir).
        std::string open, close;
        auto wrap = [&open, &close](const char* openTag, const char* closeTag) {
            open += openTag;
            close = std::string(closeTag) + close;
        };
        using S = TextStyle;
        if (run.sizePercent != 100) {
            open += "<span style=\"font-size:" + emValue(run.sizePercent) + "em\">";
            close = "</span>" + close;
        }
        if (core::hasStyle(run.style, S::Superscript))   wrap("<sup>", "</sup>");
        if (core::hasStyle(run.style, S::Subscript))     wrap("<sub>", "</sub>");
        if (core::hasStyle(run.style, S::Underline))     wrap("<u>", "</u>");
        if (core::hasStyle(run.style, S::Strikethrough)) wrap("<s>", "</s>");
        if (core::hasStyle(run.style, S::Bold))          wrap("<strong>", "</strong>");
        if (core::hasStyle(run.style, S::Italic))        wrap("<em>", "</em>");

        out += open;
        emitPlain(run.start, run.end());
        out += close;
        pos = run.end();
    }
    // Texte non stylé restant.
    if (pos < text.size()) emitPlain(pos, text.size());

    return out;
}

} // namespace io
