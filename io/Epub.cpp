// Epub.cpp
// Construction de l'archive EPUB 3. Voir Epub.h.

#include "Epub.h"

#include "HtmlText.h"
#include "Zip.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace io {

namespace {

const char* kOpfPath = "OEBPS/content.opf";

// Nom de fichier d'un chapitre : "chap001" -> "text/chap001.xhtml".
std::string chapterId(size_t index) {
    std::string n = std::to_string(index);
    while (n.size() < 3) n.insert(n.begin(), '0');
    return "chap" + n;
}

// Enveloppe un contenu de corps dans un document XHTML complet.
// 'cssHref' est le chemin de la feuille de style RELATIF au document.
std::string xhtmlDocument(const std::string& title,
                          const std::string& lang,
                          const std::string& cssHref,
                          const std::string& bodyInner,
                          const std::string& extraNamespace = {}) {
    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    out += "<!DOCTYPE html>\n";
    out += "<html xmlns=\"http://www.w3.org/1999/xhtml\"";
    if (!extraNamespace.empty()) out += " " + extraNamespace;
    out += " xml:lang=\"" + htmlEscape(lang) + "\" lang=\"" + htmlEscape(lang) + "\">\n";
    out += "<head>\n";
    out += "  <meta charset=\"utf-8\"/>\n";
    out += "  <title>" + htmlEscape(title) + "</title>\n";
    out += "  <link rel=\"stylesheet\" type=\"text/css\" href=\"" + cssHref + "\"/>\n";
    out += "</head>\n<body>\n";
    out += bodyInner;
    out += "</body>\n</html>\n";
    return out;
}

// Balises de bloc autorisées en sortie. Le nom de balise est le SEUL
// morceau du livre qui ne soit pas échappé (c'en est un, pas du texte) :
// on n'accepte donc que cette liste fermée. Aujourd'hui les balises sont
// produites par io/Book, mais un futur convertisseur qui y mettrait du
// texte d'auteur ne pourrait pas casser le XHTML pour autant.
bool isAllowedTag(const std::string& tag) {
    return tag == "p" || tag == "ul" || tag == "figure" ||
           tag == "h1" || tag == "h2" || tag == "h3" ||
           tag == "h4" || tag == "h5" || tag == "h6";
}

// Rend les blocs d'un chapitre : <tag class="...">contenu</tag>.
std::string renderBlocks(const std::vector<BookBlock>& blocks) {
    std::string out;
    for (const BookBlock& b : blocks) {
        const std::string tag = isAllowedTag(b.tag) ? b.tag : "p";
        out += "  <" + tag;
        if (!b.cls.empty()) out += " class=\"" + htmlEscape(b.cls) + "\"";
        if (b.html.empty()) {
            // Balise vide : refermée immédiatement (XHTML n'admet pas
            // <p> sans </p>).
            out += "></" + tag + ">\n";
        } else {
            out += ">" + b.html + "</" + tag + ">\n";
        }
    }
    return out;
}

// Feuille de style du livre : sobre et lisible, sans police externe (une
// liseuse n'a pas accès au réseau, et le cadrage interdit les
// dépendances externes).
const char* kStyleCss = R"css(/* Feuille de style du livre exporte. */
body {
  font-family: Georgia, "Times New Roman", serif;
  line-height: 1.5;
  margin: 1em;
  text-align: justify;
  hyphens: auto;
}
h1, h2, h3, h4, h5, h6 {
  font-family: Georgia, "Times New Roman", serif;
  text-align: center;
  page-break-after: avoid;
  line-height: 1.25;
}
h1 { font-size: 1.6em; margin: 2em 0 1.5em; }
h2 { font-size: 1.3em; margin: 1.8em 0 1em; }
h3 { font-size: 1.1em; margin: 1.6em 0 0.8em; }
h4, h5, h6 { font-size: 1em; margin: 1.4em 0 0.8em; }
p { margin: 0; text-indent: 1.2em; }
/* Pas d'alinea sur le premier paragraphe apres un titre. */
h1 + p, h2 + p, h3 + p, h4 + p, h5 + p, h6 + p,
figure + p, ul + p, p.champ { text-indent: 0; }
p.champ { margin-top: 0.8em; }
ul { margin: 0.2em 0 0.6em 1.2em; padding: 0; text-align: left; }
figure { margin: 1em 0; text-align: center; page-break-inside: avoid; }
img { max-width: 100%; }
/* Page de titre */
.page-titre { text-align: center; }
.page-titre h1 { page-break-before: avoid; margin-top: 1em; }
.page-titre .auteur { text-indent: 0; font-size: 1.1em; margin: 1em 0; }
.page-titre .resume { text-indent: 0; text-align: left; margin-top: 3em;
                      font-style: italic; }
.couverture { text-align: center; margin: 0; }
/* Table des matieres lisible */
nav ol { list-style: none; margin: 0; padding: 0; }
nav li { margin: 0.4em 0; text-indent: 0; }
)css";

// --- Identifiant déterministe -----------------------------------------
// Hachage FNV-1a 64 bits : court, sans dépendance, et suffisant ici — il
// ne sert pas à sécuriser quoi que ce soit, seulement à donner au même
// livre le même identifiant d'un export à l'autre.
uint64_t fnv1a(const std::string& text, uint64_t seed) {
    uint64_t h = seed;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string hex(uint64_t value, int digits) {
    static const char* kDigits = "0123456789abcdef";
    std::string out(static_cast<size_t>(digits), '0');
    for (int i = digits - 1; i >= 0; --i) {
        out[static_cast<size_t>(i)] = kDigits[value & 0xF];
        value >>= 4;
    }
    return out;
}

} // namespace

std::string uuidFromText(const std::string& text) {
    const uint64_t a = fnv1a(text, 14695981039346656037ull);
    const uint64_t b = fnv1a(text + "\x01" + text, a);

    // Mise en forme 8-4-4-4-12 avec les nibbles de version (4) et de
    // variant (8) attendus, pour que ce soit un UUID syntaxiquement
    // valide et non une chaîne quelconque.
    std::string s = hex(a, 16) + hex(b, 16); // 32 chiffres hexadécimaux
    s[12] = '4';
    s[16] = '8';
    return "urn:uuid:" + s.substr(0, 8) + "-" + s.substr(8, 4) + "-" +
           s.substr(12, 4) + "-" + s.substr(16, 4) + "-" + s.substr(20, 12);
}

namespace {

// Construit l'archive (structure ZIP comprise) sans l'écrire : buildEpub
// et writeEpub s'appuient tous deux dessus, il n'y a donc qu'UNE
// description de l'EPUB dans le code.
ZipWriter buildEpubZip(const Book& book, const EpubOptions& options) {
    const std::string lang = book.meta.language.empty() ? "fr" : book.meta.language;
    const std::string title = book.meta.title.empty() ? "Sans titre" : book.meta.title;
    const std::string modified =
        options.modified.empty() ? "2020-01-01T00:00:00Z" : options.modified;
    const std::string bookId =
        options.identifier.empty() ? uuidFromText(title + "\n" + book.meta.author)
                                   : options.identifier;

    ZipWriter zip;
    if (options.year != 0) {
        zip.setTimestamp(options.year, options.month, options.day,
                         options.hour, options.minute, options.second);
    }

    // 1. mimetype — DOIT être la première entrée et rester non compressée.
    zip.addFile("mimetype", "application/epub+zip");

    // 2. Le pointeur vers le fichier de package.
    zip.addFile("META-INF/container.xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<container version=\"1.0\" "
                "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
                "  <rootfiles>\n"
                "    <rootfile full-path=\"" + std::string(kOpfPath) + "\" "
                "media-type=\"application/oebps-package+xml\"/>\n"
                "  </rootfiles>\n"
                "</container>\n");

    zip.addFile("OEBPS/style.css", kStyleCss);

    // 3. Images (couverture et illustrations).
    for (const BookImage& img : book.images) {
        zip.addFile("OEBPS/images/" + img.fileName, img.data);
    }

    // 4. Page de titre.
    std::vector<std::pair<std::string, std::string>> spine; // id -> href
    if (options.titlePage) {
        std::string body = "<section class=\"page-titre\" epub:type=\"titlepage\">\n";
        const BookImage* cover = book.image(book.coverImageId);
        if (cover) {
            body += "  <figure class=\"couverture\"><img src=\"../images/" +
                    cover->fileName + "\" alt=\"Couverture\"/></figure>\n";
        }
        body += "  <h1>" + htmlEscape(title) + "</h1>\n";
        if (!book.meta.author.empty()) {
            body += "  <p class=\"auteur\">" + htmlEscape(book.meta.author) + "</p>\n";
        }
        if (!book.meta.summary.empty()) {
            body += "  <p class=\"resume\">" + htmlEscape(book.meta.summary) + "</p>\n";
        }
        body += "</section>\n";
        zip.addFile("OEBPS/text/titre.xhtml",
                    xhtmlDocument(title, lang, "../style.css", body,
                                  "xmlns:epub=\"http://www.idpf.org/2007/ops\""));
        spine.emplace_back("titlepage", "text/titre.xhtml");
    }

    // 5. Un fichier par chapitre.
    std::vector<std::pair<std::string, std::string>> chapters; // id -> href
    for (size_t i = 0; i < book.chapters.size(); ++i) {
        const BookChapter& chapter = book.chapters[i];
        const std::string id = chapterId(i + 1);
        const std::string href = "text/" + id + ".xhtml";
        zip.addFile("OEBPS/" + href,
                    xhtmlDocument(chapter.title.empty() ? title : chapter.title,
                                  lang, "../style.css", renderBlocks(chapter.blocks)));
        chapters.emplace_back(id, href);
    }

    // 6. Table des matières lisible (une page du livre), optionnelle.
    if (options.tableOfContents && !chapters.empty()) {
        std::string body = "<h1>Table des matières</h1>\n<nav epub:type=\"toc\">\n  <ol>\n";
        for (size_t i = 0; i < chapters.size(); ++i) {
            body += "    <li><a href=\"" + chapters[i].second + "\">" +
                    htmlEscape(book.chapters[i].title) + "</a></li>\n";
        }
        body += "  </ol>\n</nav>\n";
        zip.addFile("OEBPS/text/sommaire.xhtml",
                    xhtmlDocument("Table des matières", lang, "../style.css", body,
                                  "xmlns:epub=\"http://www.idpf.org/2007/ops\""));
        spine.emplace_back("sommaire", "text/sommaire.xhtml");
    }
    for (const auto& c : chapters) spine.push_back(c);

    // 7. Navigation EPUB 3 (celle du menu de la liseuse). Elle est à la
    //    racine d'OEBPS : ses liens pointent donc vers "text/...".
    {
        std::string body = "<nav epub:type=\"toc\" id=\"toc\">\n";
        body += "  <h1>Table des matières</h1>\n  <ol>\n";
        if (chapters.empty()) {
            // La navigation ne peut pas être vide : on pointe la page de
            // titre (seul contenu du livre dans ce cas).
            body += "    <li><a href=\"text/titre.xhtml\">" + htmlEscape(title) +
                    "</a></li>\n";
        }
        for (size_t i = 0; i < chapters.size(); ++i) {
            body += "    <li><a href=\"" + chapters[i].second + "\">" +
                    htmlEscape(book.chapters[i].title) + "</a></li>\n";
        }
        body += "  </ol>\n</nav>\n";
        zip.addFile("OEBPS/nav.xhtml",
                    xhtmlDocument("Table des matières", lang, "style.css", body,
                                  "xmlns:epub=\"http://www.idpf.org/2007/ops\""));
    }

    // 8. Table des matières EPUB 2 (toc.ncx) : ignorée par les liseuses
    //    récentes, indispensable aux anciennes.
    {
        std::string ncx;
        ncx += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        ncx += "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n";
        ncx += "  <head>\n";
        ncx += "    <meta name=\"dtb:uid\" content=\"" + htmlEscape(bookId) + "\"/>\n";
        ncx += "    <meta name=\"dtb:depth\" content=\"1\"/>\n";
        ncx += "    <meta name=\"dtb:totalPageCount\" content=\"0\"/>\n";
        ncx += "    <meta name=\"dtb:maxPageNumber\" content=\"0\"/>\n";
        ncx += "  </head>\n";
        ncx += "  <docTitle><text>" + htmlEscape(title) + "</text></docTitle>\n";
        ncx += "  <navMap>\n";
        for (size_t i = 0; i < chapters.size(); ++i) {
            const std::string order = std::to_string(i + 1);
            ncx += "    <navPoint id=\"nav" + order + "\" playOrder=\"" + order + "\">\n";
            ncx += "      <navLabel><text>" + htmlEscape(book.chapters[i].title) +
                   "</text></navLabel>\n";
            ncx += "      <content src=\"" + chapters[i].second + "\"/>\n";
            ncx += "    </navPoint>\n";
        }
        ncx += "  </navMap>\n</ncx>\n";
        zip.addFile("OEBPS/toc.ncx", ncx);
    }

    // 9. Le fichier de package : métadonnées, manifeste (tout ce que
    //    contient le livre) et ordre de lecture.
    {
        std::string opf;
        opf += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        opf += "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" "
               "unique-identifier=\"bookid\">\n";
        opf += "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n";
        opf += "    <dc:identifier id=\"bookid\">" + htmlEscape(bookId) + "</dc:identifier>\n";
        opf += "    <dc:title>" + htmlEscape(title) + "</dc:title>\n";
        opf += "    <dc:language>" + htmlEscape(lang) + "</dc:language>\n";
        if (!book.meta.author.empty()) {
            opf += "    <dc:creator>" + htmlEscape(book.meta.author) + "</dc:creator>\n";
        }
        if (!book.meta.summary.empty()) {
            opf += "    <dc:description>" + htmlEscape(book.meta.summary) +
                   "</dc:description>\n";
        }
        opf += "    <meta property=\"dcterms:modified\">" + htmlEscape(modified) +
               "</meta>\n";
        const BookImage* cover = book.image(book.coverImageId);
        if (cover) {
            // Ancienne convention de couverture, comprise par les
            // liseuses EPUB 2 (la propriété EPUB 3 est sur l'item).
            opf += "    <meta name=\"cover\" content=\"" + cover->id + "\"/>\n";
        }
        opf += "  </metadata>\n";

        opf += "  <manifest>\n";
        opf += "    <item id=\"nav\" href=\"nav.xhtml\" "
               "media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n";
        opf += "    <item id=\"ncx\" href=\"toc.ncx\" "
               "media-type=\"application/x-dtbncx+xml\"/>\n";
        opf += "    <item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>\n";
        for (const BookImage& img : book.images) {
            opf += "    <item id=\"" + img.id + "\" href=\"images/" + img.fileName +
                   "\" media-type=\"" + img.mediaType + "\"";
            if (img.id == book.coverImageId) opf += " properties=\"cover-image\"";
            opf += "/>\n";
        }
        for (const auto& item : spine) {
            opf += "    <item id=\"" + item.first + "\" href=\"" + item.second +
                   "\" media-type=\"application/xhtml+xml\"/>\n";
        }
        opf += "  </manifest>\n";

        opf += "  <spine toc=\"ncx\">\n";
        for (const auto& item : spine) {
            opf += "    <itemref idref=\"" + item.first + "\"/>\n";
        }
        opf += "  </spine>\n";
        opf += "</package>\n";
        zip.addFile(kOpfPath, opf);
    }

    return zip;
}

} // namespace

std::string buildEpub(const Book& book, const EpubOptions& options) {
    return buildEpubZip(book, options).build();
}

bool writeEpub(const Book& book,
               const std::filesystem::path& path,
               const EpubOptions& options,
               std::string* errorMessage) {
    // L'écriture atomique (temporaire + renommage) est celle de ZipWriter :
    // un export interrompu ne laisse jamais un .epub tronqué.
    return buildEpubZip(book, options).writeToFile(path, errorMessage);
}

} // namespace io
