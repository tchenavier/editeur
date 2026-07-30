// MarkdownIO.cpp
// Implémentation de la sérialisation Markdown. Voir MarkdownIO.h pour
// le format et les limites assumées.

#include "MarkdownIO.h"

#include "PathUtf8.h"
#include "Utf8.h"
#include "Uuid.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iterator>
#include <vector>

namespace io {

namespace {

using core::BlockType;
using core::TextBlock;
using core::TextStyle;

// =====================================================================
// Aides communes
// =====================================================================

// Ponctuation ASCII, seule catégorie échappable par '\' (règle
// CommonMark : un antislash devant autre chose reste un antislash).
bool isAsciiPunct(char32_t c) {
    return (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
           (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E);
}

// Vrai si 's' contient exactement 'tag' à la position 'i'.
bool matchesAt(const std::u32string& s, size_t i, const char32_t* tag) {
    for (size_t k = 0; tag[k] != U'\0'; ++k) {
        if (i + k >= s.size() || s[i + k] != tag[k]) return false;
    }
    return true;
}

size_t tagLength(const char32_t* tag) {
    size_t n = 0;
    while (tag[n] != U'\0') ++n;
    return n;
}

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

// =====================================================================
// Sérialisation
// =====================================================================

// Échappe le texte BRUT d'un bloc (jamais les marqueurs de style, qui
// sont émis à part) pour qu'un caractère spécial tapé par l'auteur
// ("2 * 3", "#hashtag"...) ne soit pas relu comme du formatage.
std::u32string escapeText(const std::u32string& text) {
    std::u32string out;
    out.reserve(text.size());
    for (char32_t c : text) {
        switch (c) {
            case U'\n': case U'\r':
                out += U' '; // limite documentée : pas de saut de ligne intra-bloc
                continue;
            case U'\\': case U'*': case U'_': case U'~': case U'`':
            case U'<': case U'#':
                out += U'\\';
                break;
            default:
                break;
        }
        out += c;
    }
    return out;
}

// Ouvre les marqueurs de style dans un ordre FIXE (et les ferme dans
// l'ordre inverse) : l'imbrication est toujours bien formée, et deux
// sauvegardes du même document produisent exactement le même fichier
// (diffs lisibles, exigence de stabilité).
void openMarkers(std::u32string& out, TextStyle style) {
    using S = TextStyle;
    if (core::hasStyle(style, S::Superscript))   out += U"<sup>";
    if (core::hasStyle(style, S::Subscript))     out += U"<sub>";
    if (core::hasStyle(style, S::Underline))     out += U"<u>";
    if (core::hasStyle(style, S::Strikethrough)) out += U"~~";
    if (core::hasStyle(style, S::Bold))          out += U"**";
    if (core::hasStyle(style, S::Italic))        out += U"*";
}

void closeMarkers(std::u32string& out, TextStyle style) {
    using S = TextStyle;
    if (core::hasStyle(style, S::Italic))        out += U"*";
    if (core::hasStyle(style, S::Bold))          out += U"**";
    if (core::hasStyle(style, S::Strikethrough)) out += U"~~";
    if (core::hasStyle(style, S::Underline))     out += U"</u>";
    if (core::hasStyle(style, S::Subscript))     out += U"</sub>";
    if (core::hasStyle(style, S::Superscript))   out += U"</sup>";
}

// Balise ouvrante de taille : <span style="font-size:<em>em">, où <em> vaut
// le pourcentage / 100, formaté sans zéros inutiles (140 -> "1.4",
// 125 -> "1.25", 90 -> "0.9"). N'est émise que pour une taille != 100.
std::u32string sizeSpanOpen(uint16_t pct) {
    std::string em = std::to_string(pct / 100);
    const int frac = pct % 100;
    if (frac != 0) {
        em += '.';
        em += static_cast<char>('0' + frac / 10);
        if (frac % 10 != 0) em += static_cast<char>('0' + frac % 10);
    }
    return utils::utf8ToU32("<span style=\"font-size:" + em + "em\">");
}

// Contenu en ligne d'un bloc : texte échappé + marqueurs de style + balises
// de taille. Parcourt les plages (triées, non chevauchantes : invariant de
// TextBlock) et comble les trous non stylés entre elles.
//
// Les marqueurs de style sont émis par TRANSITION : à chaque frontière de
// plage, on ferme uniquement les styles qui disparaissent et on ouvre ceux
// qui apparaissent. Refermer puis rouvrir un style qui continue ("**gras**"
// immédiatement suivi de "**gras italique**") collerait des marqueurs entre
// eux ("****") et fausserait la relecture.
//
// La TAILLE forme la couche la plus externe : quand elle change, on referme
// d'abord tous les styles internes et l'ancien <span>, on ouvre le nouveau
// <span>, puis on rouvre les styles voulus. L'imbrication reste ainsi bien
// formée (le <span> enveloppe toujours des styles complets).
std::u32string renderInline(const TextBlock& block) {
    const std::u32string& text = block.text();
    std::u32string out;
    out.reserve(text.size() + 16);

    TextStyle curStyle = TextStyle::None;
    uint16_t curSize = 100;
    auto transitionTo = [&out, &curStyle, &curSize](TextStyle nextStyle, uint16_t nextSize) {
        if (nextSize != curSize) {
            closeMarkers(out, curStyle);           // ferme tous les styles ouverts
            if (curSize != 100) out += U"</span>"; // ferme l'ancien span de taille
            if (nextSize != 100) out += sizeSpanOpen(nextSize);
            curSize = nextSize;
            openMarkers(out, nextStyle);           // rouvre les styles voulus
            curStyle = nextStyle;
        } else {
            closeMarkers(out, curStyle & ~nextStyle); // styles qui s'arrêtent ici
            openMarkers(out, nextStyle & ~curStyle);  // styles qui commencent ici
            curStyle = nextStyle;
        }
    };

    size_t pos = 0;
    for (const auto& run : block.formatRuns()) {
        if (run.start > pos) {
            transitionTo(TextStyle::None, 100);
            out += escapeText(text.substr(pos, run.start - pos));
        }
        transitionTo(run.style, run.sizePercent);
        out += escapeText(text.substr(run.start, run.length));
        pos = run.end();
    }
    if (pos < text.size()) {
        transitionTo(TextStyle::None, 100);
        out += escapeText(text.substr(pos));
    }
    transitionTo(TextStyle::None, 100); // referme tout ce qui reste ouvert
    return out;
}

const char* headingPrefix(BlockType type) {
    switch (type) {
        case BlockType::Heading1: return "# ";
        case BlockType::Heading2: return "## ";
        case BlockType::Heading3: return "### ";
        case BlockType::Heading4: return "#### ";
        case BlockType::Heading5: return "##### ";
        case BlockType::Heading6: return "###### ";
        default:                  return "";
    }
}

// Valeur YAML entre guillemets doubles : seul '\', '"' et le saut de
// ligne ont besoin d'être échappés (le résumé peut être multi-lignes).
std::string yamlEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '\\')      out += "\\\\";
        else if (c == '"')  out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c != '\r') out += c;
    }
    return out;
}

std::string serializeFrontMatter(const core::DocumentMetadata& meta) {
    if (meta.title.empty() && meta.author.empty() &&
        meta.coverPath.empty() && meta.summary.empty() && meta.projectId.empty()) {
        return ""; // aucun en-tête si rien à y mettre
    }
    std::string out = "---\n";
    auto field = [&out](const char* key, const std::string& value) {
        if (!value.empty()) {
            out += std::string(key) + ": \"" + yamlEscape(value) + "\"\n";
        }
    };
    field("title", meta.title);
    field("author", meta.author);
    field("cover", meta.coverPath);
    field("summary", meta.summary);
    // En dernier : c'est une donnée technique, l'auteur qui ouvre son
    // fichier dans un autre éditeur voit d'abord ce qui le concerne.
    field("project", meta.projectId);
    out += "---\n";
    return out;
}

// =====================================================================
// Parsing
// =====================================================================

std::string yamlUnescape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const char next = value[i + 1];
            if (next == 'n')       { out += '\n'; ++i; continue; }
            if (next == '"' || next == '\\') { out += next; ++i; continue; }
        }
        out += value[i];
    }
    return out;
}

// Découpe en lignes, en acceptant les fins de ligne Windows (\r\n)
// comme Unix (\n) : les fichiers peuvent venir de n'importe où.
std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(std::move(current));
            current.clear();
        } else if (c != '\r') {
            current += c;
        }
    }
    if (!current.empty()) lines.push_back(std::move(current));
    return lines;
}

// En-tête YAML : "---" en première ligne, puis "clé: valeur" jusqu'au
// "---" fermant. Retourne l'indice de la première ligne APRÈS l'en-tête,
// ou 0 si l'en-tête est absent ou non fermé (tout est alors du contenu).
size_t parseFrontMatter(const std::vector<std::string>& lines,
                        core::DocumentMetadata& meta) {
    if (lines.empty() || trim(lines[0]) != "---") return 0;

    // Chercher d'abord la fermeture : un "---" jamais fermé signifie
    // que la première ligne était en réalité du contenu.
    size_t closing = 0;
    for (size_t i = 1; i < lines.size(); ++i) {
        if (trim(lines[i]) == "---") { closing = i; break; }
    }
    if (closing == 0) return 0;

    for (size_t i = 1; i < closing; ++i) {
        const size_t colon = lines[i].find(':');
        if (colon == std::string::npos) continue; // ligne inconnue : ignorée
        const std::string key = trim(lines[i].substr(0, colon));
        std::string value = trim(lines[i].substr(colon + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = yamlUnescape(value.substr(1, value.size() - 2));
        }
        if (key == "title")        meta.title = value;
        else if (key == "author")  meta.author = value;
        else if (key == "cover")   meta.coverPath = value;
        else if (key == "summary") meta.summary = value;
        else if (key == "project") {
            // Identifiant de projet : on ne reprend que ce qui a la bonne
            // forme. Une valeur abîmée par une édition à la main est
            // écartée (un identifiant sera réattribué), plutôt que
            // propagée telle quelle jusqu'au serveur.
            if (utils::isValidUuid(value)) meta.projectId = value;
        }
        // Clé inconnue : ignorée sans erreur (fichier édité à la main).
    }
    return closing + 1;
}

// "# Titre" à "###### Titre". Retourne false si la ligne n'est pas un titre.
bool parseHeading(const std::string& line, BlockType& type, std::string& content) {
    size_t level = 0;
    while (level < line.size() && line[level] == '#') ++level;
    if (level == 0 || level > 6 || level >= line.size() || line[level] != ' ') {
        return false;
    }
    static const BlockType kTypes[] = {
        BlockType::Heading1, BlockType::Heading2, BlockType::Heading3,
        BlockType::Heading4, BlockType::Heading5, BlockType::Heading6,
    };
    type = kTypes[level - 1];
    content = line.substr(level + 1);
    return true;
}

// Résultat du parsing en ligne : le texte débarrassé des marqueurs, le
// style de chacun de ses caractères (masque TextStyle en uint32_t) et sa
// taille en pourcentage (100 = normal).
struct InlineResult {
    std::u32string text;
    std::vector<uint32_t> styles;
    std::vector<uint16_t> sizes;
};

// Tente de lire une balise ouvrante de taille à la position 'i' (qui doit
// pointer sur '<'). En cas de succès : renvoie true, place la taille (en %,
// bornée à [20, 800]) dans 'pctOut' et le nombre de caractères consommés
// (balise entière, '>' compris) dans 'lenOut'. Tolérant (fichier édité à la
// main) : ignore les espaces, accepte les unités « em » (défaut) et « % »,
// se replie sur 100 % si « font-size » manque. Retourne false si ce n'est
// pas une balise <span …> bien formée (le '<' sera alors traité comme un
// caractère littéral, comme pour les autres balises non reconnues).
bool parseSpanSizeOpen(const std::u32string& s, size_t i,
                       uint16_t& pctOut, size_t& lenOut) {
    if (!matchesAt(s, i, U"<span")) return false;
    size_t gt = i + 5;
    while (gt < s.size() && s[gt] != U'>') {
        if (s[gt] == U'<') return false; // balise non fermée / mal formée
        ++gt;
    }
    if (gt >= s.size()) return false;
    lenOut = gt - i + 1;

    const std::u32string tag = s.substr(i, lenOut);
    const size_t key = tag.find(U"font-size:");
    if (key == std::u32string::npos) { pctOut = 100; return true; } // span sans taille
    size_t k = key + 10; // longueur de "font-size:"
    while (k < tag.size() && tag[k] == U' ') ++k;

    // Nombre décimal -> partie entière + centièmes (2 décimales au plus).
    // Calcul entier : ni std::stod (exceptions) ni flottant.
    unsigned long whole = 0;
    unsigned int hundredths = 0, decDigits = 0;
    bool afterDot = false, anyDigit = false;
    for (; k < tag.size(); ++k) {
        const char32_t d = tag[k];
        if (d == U'.') { if (afterDot) break; afterDot = true; continue; }
        if (d < U'0' || d > U'9') break;
        anyDigit = true;
        if (!afterDot) {
            whole = whole * 10 + static_cast<unsigned long>(d - U'0');
            if (whole > 1000) whole = 1000; // borne de sûreté (débordement)
        } else if (decDigits < 2) {
            hundredths = hundredths * 10 + static_cast<unsigned int>(d - U'0');
            ++decDigits;
        }
    }
    if (!anyDigit) { pctOut = 100; return true; }
    if (decDigits == 1) hundredths *= 10; // "1.4" -> 40 centièmes

    while (k < tag.size() && tag[k] == U' ') ++k;
    unsigned long pct = (k < tag.size() && tag[k] == U'%')
                            ? whole + (hundredths >= 50 ? 1UL : 0UL) // % : ~ entier
                            : whole * 100 + hundredths;              // em (défaut)
    if (pct < 20) pct = 20;
    if (pct > 800) pct = 800;
    pctOut = static_cast<uint16_t>(pct);
    return true;
}

// Interprète les marqueurs **, *, ~~, _, __ et les balises <u>, <sup>,
// <sub> en avançant de gauche à droite avec un état de style courant.
// Un marqueur non fermé s'arrête en fin de bloc (l'état est local).
InlineResult parseInline(const std::u32string& src) {
    using S = TextStyle;
    InlineResult out;
    out.text.reserve(src.size());
    out.styles.reserve(src.size());

    uint32_t state = 0;
    uint16_t curSize = 100;
    auto append = [&out, &state, &curSize](char32_t c) {
        out.text += c;
        out.styles.push_back(state);
        out.sizes.push_back(curSize);
    };

    struct HtmlTag { const char32_t* text; uint32_t bit; bool closing; };
    static const HtmlTag kTags[] = {
        {U"<u>",    static_cast<uint32_t>(S::Underline),   false},
        {U"</u>",   static_cast<uint32_t>(S::Underline),   true},
        {U"<sup>",  static_cast<uint32_t>(S::Superscript), false},
        {U"</sup>", static_cast<uint32_t>(S::Superscript), true},
        {U"<sub>",  static_cast<uint32_t>(S::Subscript),   false},
        {U"</sub>", static_cast<uint32_t>(S::Subscript),   true},
    };

    size_t i = 0;
    while (i < src.size()) {
        const char32_t c = src[i];

        // Échappement : '\' + ponctuation ASCII = caractère littéral.
        if (c == U'\\' && i + 1 < src.size() && isAsciiPunct(src[i + 1])) {
            append(src[i + 1]);
            i += 2;
            continue;
        }

        // ** (gras) avant * (italique) : le plus long d'abord, sinon
        // "**" serait lu comme deux bascules d'italique.
        if (c == U'*') {
            if (matchesAt(src, i, U"**")) {
                state ^= static_cast<uint32_t>(S::Bold);
                i += 2;
            } else {
                state ^= static_cast<uint32_t>(S::Italic);
                i += 1;
            }
            continue;
        }

        // _ et __ : équivalents Markdown de * et **, mais UNIQUEMENT en
        // bordure de mot ("mon_fichier_config" ne doit pas devenir de
        // l'italique, règle alignée sur CommonMark).
        if (c == U'_') {
            const size_t len = matchesAt(src, i, U"__") ? 2 : 1;
            const bool wordBefore =
                i > 0 && !core::WordBoundary::isWordSeparator(src[i - 1]) &&
                src[i - 1] != U'_';
            const bool wordAfter =
                i + len < src.size() &&
                !core::WordBoundary::isWordSeparator(src[i + len]) &&
                src[i + len] != U'_';
            if (wordBefore && wordAfter) {
                for (size_t k = 0; k < len; ++k) append(U'_'); // intra-mot : littéral
            } else {
                state ^= static_cast<uint32_t>(len == 2 ? S::Bold : S::Italic);
            }
            i += len;
            continue;
        }

        if (c == U'~' && matchesAt(src, i, U"~~")) {
            state ^= static_cast<uint32_t>(S::Strikethrough);
            i += 2;
            continue;
        }

        if (c == U'<') {
            // Balise de taille : <span style="font-size:…em"> … </span>.
            // Traitée en couche externe, en amont des balises de style.
            if (matchesAt(src, i, U"</span>")) {
                curSize = 100; // fin d'une zone agrandie/réduite
                i += 7;
                continue;
            }
            uint16_t spanPct = 100;
            size_t spanLen = 0;
            if (parseSpanSizeOpen(src, i, spanPct, spanLen)) {
                curSize = spanPct;
                i += spanLen;
                continue;
            }

            bool matched = false;
            for (const auto& tag : kTags) {
                if (matchesAt(src, i, tag.text)) {
                    if (tag.closing) state &= ~tag.bit;
                    else             state |= tag.bit;
                    i += tagLength(tag.text);
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
            // '<' quelconque : caractère littéral (pas une de nos balises).
        }

        append(c);
        ++i;
    }
    return out;
}

// Construit un TextBlock depuis une ligne de contenu : parsing en ligne,
// insertion du texte nu, puis application des styles par plages de
// caractères consécutifs identiques. On passe par l'API publique de
// TextBlock (insertText/applyStyle) : ses invariants font foi.
TextBlock buildBlock(BlockType type, const std::string& rawContent) {
    TextBlock block(type);
    const InlineResult inl = parseInline(utils::utf8ToU32(rawContent));
    block.insertText(0, inl.text);

    size_t i = 0;
    while (i < inl.styles.size()) {
        const uint32_t mask = inl.styles[i];
        size_t j = i;
        while (j < inl.styles.size() && inl.styles[j] == mask) ++j;
        if (mask != 0) {
            block.applyStyle(i, j - i, static_cast<TextStyle>(mask));
        }
        i = j;
    }

    // Puis les tailles, par plages de caractères de même taille.
    size_t s = 0;
    while (s < inl.sizes.size()) {
        const uint16_t sz = inl.sizes[s];
        size_t j = s;
        while (j < inl.sizes.size() && inl.sizes[j] == sz) ++j;
        if (sz != 100) {
            block.setFontSize(s, j - s, sz);
        }
        s = j;
    }
    return block;
}

} // namespace

// =====================================================================
// API publique
// =====================================================================

std::string serialize(const core::Document& doc) {
    std::string out = serializeFrontMatter(doc.metadata());

    for (const auto& block : doc.blocks()) {
        const std::u32string content = renderInline(block);
        if (content.empty() && block.type() == BlockType::Paragraph) {
            continue; // limite documentée : paragraphe vide sans représentation
        }
        if (!out.empty()) out += "\n"; // ligne vide entre les éléments
        out += headingPrefix(block.type());
        out += utils::u32ToUtf8(content);
        out += "\n";
    }
    return out;
}

core::Document parse(const std::string& markdown) {
    core::Document doc;

    // BOM UTF-8 tolérée en tête de fichier (éditeurs Windows).
    std::string content = markdown;
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content.erase(0, 3);
    }

    const std::vector<std::string> lines = splitLines(content);
    size_t i = parseFrontMatter(lines, doc.metadata());

    // Accumulation des lignes d'un même paragraphe : en Markdown, des
    // lignes consécutives non vides forment UN paragraphe (retour à la
    // ligne "mou"), joint ici par une espace.
    std::string paragraph;
    auto flushParagraph = [&doc, &paragraph]() {
        if (!paragraph.empty()) {
            doc.blocks().push_back(buildBlock(BlockType::Paragraph, paragraph));
            paragraph.clear();
        }
    };

    for (; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (trim(line).empty()) {
            flushParagraph();
            continue;
        }
        BlockType headingType;
        std::string headingContent;
        if (parseHeading(line, headingType, headingContent)) {
            flushParagraph();
            doc.blocks().push_back(buildBlock(headingType, headingContent));
            continue;
        }
        if (!paragraph.empty()) paragraph += ' ';
        paragraph += line;
    }
    flushParagraph();

    // L'éditeur suppose toujours au moins un bloc où placer le curseur.
    if (doc.blocks().empty()) {
        doc.blocks().emplace_back(BlockType::Paragraph);
    }
    return doc;
}

bool saveToFile(const core::Document& doc,
                const std::filesystem::path& path,
                std::string* errorMessage) {
    // Tout sérialiser AVANT d'ouvrir quoi que ce soit : aucune écriture
    // partielle possible si la sérialisation échouait.
    const std::string content = serialize(doc);

    // Fichier temporaire DANS LE MÊME DOSSIER que la destination : le
    // renommage n'est atomique qu'au sein d'un même volume.
    std::filesystem::path tmp = path;
    tmp += ".tmp";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (errorMessage) {
                *errorMessage = "Impossible de créer le fichier temporaire : " + utils::pathToUtf8(tmp);
            }
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ignored;
            std::filesystem::remove(tmp, ignored); // ne pas laisser traîner un .tmp corrompu
            if (errorMessage) {
                *errorMessage = "Échec d'écriture dans : " + utils::pathToUtf8(tmp);
            }
            return false;
        }
    } // fermeture du flux AVANT le renommage (obligatoire sous Windows)

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec); // remplace la destination si elle existe
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        if (errorMessage) {
            *errorMessage = "Échec du renommage vers : " + utils::pathToUtf8(path) +
                            " (" + ec.message() + ")";
        }
        return false;
    }
    return true;
}

bool writeBackup(const core::Document& doc,
                 const std::filesystem::path& mainPath,
                 size_t keep,
                 std::string* outBackupPath,
                 std::string* errorMessage) {
    namespace fs = std::filesystem;

    // Dossier dédié à côté du fichier : « <nom>_backups/ ».
    //
    // Attention : on reste dans le domaine des CHEMINS, sans repasser par
    // std::string. Sur Windows, path::string() convertit vers la page de
    // codes ANSI : un manuscrit nommé « Écrits.md » produirait un dossier
    // au nom corrompu. Les suffixes ajoutés (« _backups », l'horodatage,
    // « .md ») sont purement ASCII, donc sûrs à concaténer à un path.
    fs::path dir = mainPath.parent_path() / mainPath.stem();
    dir += "_backups";
    // Préfixe des sauvegardes, dans l'encodage natif des chemins.
    const fs::path::string_type prefix = mainPath.stem().native() +
                                         static_cast<fs::path>("_").native();

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        if (errorMessage) {
            *errorMessage = "Impossible de créer le dossier de sauvegardes : " + utils::pathToUtf8(dir);
        }
        return false;
    }

    // Horodatage AAAA-MM-JJ_HH-MM-SS (tri lexicographique = chronologique).
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    // Tampon initialisé : en cas d'échec, strftime retourne 0 et laisse le
    // contenu INDÉTERMINÉ (on l'utiliserait ensuite comme une chaîne).
    char stamp[32] = {0};
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tmv);

    // « <dossier>/<nom>_AAAA-MM-JJ_HH-MM-SS.md », toujours sans passer par
    // une chaîne étroite (cf. remarque plus haut).
    fs::path backupPath = dir / mainPath.stem();
    backupPath += "_";
    backupPath += stamp;
    backupPath += ".md";
    if (!saveToFile(doc, backupPath, errorMessage)) {
        return false; // saveToFile a déjà renseigné le message
    }
    if (outBackupPath) *outBackupPath = utils::pathToUtf8(backupPath);

    // Purge : ne garder que les 'keep' plus récentes.
    if (keep > 0) {
        std::vector<fs::path> backups;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            // Comparaison dans l'encodage natif : pas de conversion, donc
            // pas de risque de corruption sur un nom accentué.
            const fs::path::string_type name = entry.path().filename().native();
            if (name.size() > prefix.size() &&
                name.compare(0, prefix.size(), prefix) == 0 &&
                entry.path().extension() == ".md") {
                backups.push_back(entry.path());
            }
        }
        std::sort(backups.begin(), backups.end()); // ordre chronologique
        if (backups.size() > keep) {
            const size_t toRemove = backups.size() - keep;
            for (size_t i = 0; i < toRemove; ++i) {
                std::error_code rmec;
                fs::remove(backups[i], rmec); // échec silencieux : une purge ratée n'est pas critique
            }
        }
    }
    return true;
}

bool loadFromFile(core::Document& doc,
                  const std::filesystem::path& path,
                  std::string* errorMessage) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (errorMessage) {
            *errorMessage = "Impossible d'ouvrir le fichier : " + utils::pathToUtf8(path);
        }
        return false;
    }
    std::string content{std::istreambuf_iterator<char>(in),
                        std::istreambuf_iterator<char>()};
    if (in.bad()) {
        if (errorMessage) {
            *errorMessage = "Erreur de lecture du fichier : " + utils::pathToUtf8(path);
        }
        return false;
    }

    // parse() ne lance jamais : à partir d'ici, plus d'échec possible,
    // on peut remplacer le document.
    doc = parse(content);
    return true;
}

} // namespace io
