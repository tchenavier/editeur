// Book.cpp
// Construction de la structure « livre » depuis le manuscrit et depuis
// les fiches. Voir Book.h.

#include "Book.h"

#include "CardModel.h"
#include "HtmlText.h"
#include "PathUtf8.h"
#include "TextModel.h"
#include "Utf8.h"

#include <fstream>

namespace io {

namespace {

// Balise de bloc correspondant au type du modèle.
const char* tagForBlock(core::BlockType type) {
    switch (type) {
        case core::BlockType::Heading1: return "h1";
        case core::BlockType::Heading2: return "h2";
        case core::BlockType::Heading3: return "h3";
        case core::BlockType::Heading4: return "h4";
        case core::BlockType::Heading5: return "h5";
        case core::BlockType::Heading6: return "h6";
        case core::BlockType::Paragraph: break;
    }
    return "p";
}

// Un bloc sans autre chose que des espaces n'a rien à faire dans un
// livre exporté (l'éditeur en laisse au fil de la frappe).
bool isBlank(const std::u32string& text) {
    for (char32_t c : text) {
        // Le dernier caractère testé est l'espace INSÉCABLE (U+00A0) :
        // l'interface en insère devant « : », « ; », « ! », « ? ».
        if (c != U' ' && c != U'\t' && c != U'\n' && c != U'\r' &&
            c != U' ') {
            return false;
        }
    }
    return true;
}

const char* extensionFor(const std::string& mediaType) {
    if (mediaType == "image/jpeg") return ".jpg";
    if (mediaType == "image/gif")  return ".gif";
    return ".png";
}

// Identifiant à numéro fixe : "img001", "img002"...
std::string numberedId(const char* prefix, size_t index) {
    std::string n = std::to_string(index);
    while (n.size() < 3) n.insert(n.begin(), '0');
    return std::string(prefix) + n;
}

// Résout un chemin éventuellement relatif par rapport à un dossier de
// base (dossier du manuscrit ou des fiches).
std::filesystem::path resolvePath(const std::string& utf8Path,
                                  const std::filesystem::path& baseDir) {
    std::filesystem::path p = utils::pathFromUtf8(utf8Path);
    if (p.is_relative() && !baseDir.empty()) return baseDir / p;
    return p;
}

// Libellé de repli pour un champ absent du schéma (fichier écrit par une
// autre version, champ ajouté à la main) : on n'invente rien, on rend la
// clé lisible.
std::string humanLabel(const std::string& key) {
    std::string out = key;
    for (char& c : out) {
        if (c == '_') c = ' ';
    }
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    }
    return out;
}

// Paragraphe « Libellé : valeur », les deux échappés.
BookBlock labelledParagraph(const std::string& label, const std::string& value) {
    BookBlock b;
    b.tag = "p";
    b.html = "<strong>" + htmlEscape(label) + " :</strong> " + htmlEscape(value);
    return b;
}

} // namespace

std::string sniffImageType(const std::string& data) {
    if (data.size() >= 8 &&
        static_cast<unsigned char>(data[0]) == 0x89 && data.compare(1, 3, "PNG") == 0) {
        return "image/png";
    }
    if (data.size() >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xFF &&
        static_cast<unsigned char>(data[1]) == 0xD8 &&
        static_cast<unsigned char>(data[2]) == 0xFF) {
        return "image/jpeg";
    }
    if (data.size() >= 6 &&
        (data.compare(0, 6, "GIF87a") == 0 || data.compare(0, 6, "GIF89a") == 0)) {
        return "image/gif";
    }
    return "";
}

// -------------------------------------------------------------------
// Book : images embarquées
// -------------------------------------------------------------------

std::string Book::addImageFile(const std::filesystem::path& path) {
    if (path.empty()) return {};
    const std::string source = utils::pathToUtf8(path);

    // Déjà embarquée ? On renvoie l'identifiant existant (une image
    // utilisée par dix fiches n'est stockée qu'une fois).
    for (const BookImage& img : images) {
        if (img.sourcePath == source) return img.id;
    }

    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > kMaxImageSize) return {};

    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    if (data.size() != size) return {}; // lecture partielle : on n'embarque pas

    const std::string mediaType = sniffImageType(data);
    if (mediaType.empty()) return {}; // pas une image reconnue

    BookImage img;
    img.id = numberedId("img", images.size() + 1);
    img.fileName = img.id + extensionFor(mediaType);
    img.mediaType = mediaType;
    img.data = std::move(data);
    img.sourcePath = source;
    images.push_back(std::move(img));
    return images.back().id;
}

const BookImage* Book::image(const std::string& id) const {
    for (const BookImage& img : images) {
        if (img.id == id) return &img;
    }
    return nullptr;
}

// -------------------------------------------------------------------
// Manuscrit -> Book
// -------------------------------------------------------------------

Book bookFromDocument(const core::Document& doc,
                      const std::filesystem::path& baseDir) {
    Book book;
    book.meta.title = doc.metadata().title;
    book.meta.author = doc.metadata().author;
    book.meta.summary = doc.metadata().summary;

    if (!doc.metadata().coverPath.empty()) {
        book.coverImageId =
            book.addImageFile(resolvePath(doc.metadata().coverPath, baseDir));
    }

    BookChapter current;
    // Titre du chapitre d'ouverture (ce qui précède le premier titre de
    // niveau 1) : le titre du livre s'il existe, faute de mieux.
    current.title = book.meta.title.empty() ? "Texte" : book.meta.title;

    auto flush = [&book](BookChapter& chapter) {
        if (!chapter.blocks.empty()) book.chapters.push_back(chapter);
        chapter.blocks.clear();
    };

    for (const core::TextBlock& block : doc.blocks()) {
        const bool startsChapter = (block.type() == core::BlockType::Heading1);
        if (startsChapter) {
            flush(current);
            current.title = utils::u32ToUtf8(block.text());
            if (current.title.empty()) {
                current.title = "Chapitre " + std::to_string(book.chapters.size() + 1);
            }
        }
        if (block.type() == core::BlockType::Paragraph && isBlank(block.text())) {
            continue; // paragraphe vide : rien à exporter
        }
        BookBlock out;
        out.tag = tagForBlock(block.type());
        out.html = renderBlockHtml(block);
        current.blocks.push_back(std::move(out));
    }
    flush(current);

    return book;
}

// -------------------------------------------------------------------
// Fiches -> Book (bible narrative)
// -------------------------------------------------------------------

namespace {

// Ajoute les blocs décrivant UN champ de fiche.
void appendField(BookChapter& chapter, Book& book,
                 const std::string& label, core::FieldType type,
                 const core::FieldValue& value,
                 const core::CardLibrary& library,
                 const std::filesystem::path& imageBase) {
    if (value.empty()) return;

    switch (type) {
        case core::FieldType::LongText: {
            BookBlock head;
            head.tag = "p";
            head.cls = "champ";
            head.html = "<strong>" + htmlEscape(label) + "</strong>";
            chapter.blocks.push_back(std::move(head));
            // Un texte long peut contenir des retours à la ligne : chacun
            // devient un paragraphe (sinon tout serait collé).
            for (const std::string& v : value.values()) {
                size_t start = 0;
                while (start <= v.size()) {
                    const size_t nl = v.find('\n', start);
                    const size_t end = (nl == std::string::npos) ? v.size() : nl;
                    std::string line = v.substr(start, end - start);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty()) {
                        BookBlock p;
                        p.tag = "p";
                        p.html = htmlEscape(line);
                        chapter.blocks.push_back(std::move(p));
                    }
                    if (nl == std::string::npos) break;
                    start = nl + 1;
                }
            }
            break;
        }
        case core::FieldType::List: {
            BookBlock head;
            head.tag = "p";
            head.cls = "champ";
            head.html = "<strong>" + htmlEscape(label) + "</strong>";
            chapter.blocks.push_back(std::move(head));
            BookBlock list;
            list.tag = "ul";
            for (const std::string& v : value.values()) {
                list.html += "<li>" + htmlEscape(v) + "</li>";
            }
            chapter.blocks.push_back(std::move(list));
            break;
        }
        case core::FieldType::ImageList: {
            for (const std::string& v : value.values()) {
                const std::string id = book.addImageFile(resolvePath(v, imageBase));
                const BookImage* img = book.image(id);
                if (img) {
                    BookBlock fig;
                    fig.tag = "figure";
                    fig.html = "<img src=\"../images/" + img->fileName +
                               "\" alt=\"" + htmlEscape(label) + "\"/>";
                    chapter.blocks.push_back(std::move(fig));
                } else {
                    // Image absente ou illisible : on garde la trace du
                    // fichier attendu plutôt que de la faire disparaître.
                    chapter.blocks.push_back(labelledParagraph(label, v));
                }
            }
            break;
        }
        case core::FieldType::CardRefList: {
            // Les liens sont affichés par TITRE : un identifiant technique
            // ne dirait rien au lecteur de la bible.
            std::string joined;
            for (const std::string& id : value.values()) {
                if (!joined.empty()) joined += ", ";
                const core::Card* target = library.find(id);
                joined += target ? target->title() : id;
            }
            chapter.blocks.push_back(labelledParagraph(label, joined));
            break;
        }
        case core::FieldType::Text:
        case core::FieldType::Number:
        case core::FieldType::ChapterRef:
        default: {
            std::string joined;
            for (const std::string& v : value.values()) {
                if (!joined.empty()) joined += ", ";
                joined += v;
            }
            chapter.blocks.push_back(labelledParagraph(label, joined));
            break;
        }
    }
}

} // namespace

Book bookFromCards(const core::CardLibrary& library,
                   const core::SchemaRegistry& registry,
                   const std::filesystem::path& imageBase) {
    Book book;
    book.meta.title = "Bible narrative";

    for (const core::CardSchema* schema : registry.all()) {
        const std::vector<const core::Card*> cards = library.byType(schema->typeId);
        if (cards.empty()) continue; // pas de chapitre vide

        BookChapter chapter;
        chapter.title = schema->label;
        BookBlock heading;
        heading.tag = "h1";
        heading.html = htmlEscape(schema->label);
        chapter.blocks.push_back(std::move(heading));

        for (const core::Card* card : cards) {
            BookBlock cardTitle;
            cardTitle.tag = "h2";
            cardTitle.html = htmlEscape(card->title().empty() ? card->id()
                                                              : card->title());
            chapter.blocks.push_back(std::move(cardTitle));

            if (!card->tags().empty()) {
                std::string joined;
                for (const std::string& t : card->tags()) {
                    if (!joined.empty()) joined += ", ";
                    joined += t;
                }
                chapter.blocks.push_back(labelledParagraph("Tags", joined));
            }

            // 1. Les champs du schéma, dans l'ordre du schéma (celui du
            //    formulaire : la bible se lit comme la fiche).
            for (const core::FieldDefinition& def : schema->fields) {
                const core::FieldValue* value = card->field(def.key);
                if (!value) continue;
                appendField(chapter, book, def.label, def.type, *value,
                            library, imageBase);
            }
            // 2. Les champs inconnus du schéma (fichier d'une autre
            //    version) : jamais perdus, ici non plus.
            for (const auto& entry : card->fields()) {
                if (schema->field(entry.first)) continue;
                appendField(chapter, book, humanLabel(entry.first),
                            core::FieldType::Text, entry.second,
                            library, imageBase);
            }
        }
        book.chapters.push_back(std::move(chapter));
    }

    // Les fiches dont le type n'a AUCUN schéma connu (type supprimé,
    // fichier importé) : regroupées en fin de bible plutôt qu'ignorées.
    BookChapter others;
    others.title = "Autres fiches";
    for (const auto& entry : library.cards()) {
        const core::Card& card = entry.second;
        if (registry.contains(card.typeId())) continue;
        if (others.blocks.empty()) {
            BookBlock heading;
            heading.tag = "h1";
            heading.html = htmlEscape(others.title);
            others.blocks.push_back(std::move(heading));
        }
        BookBlock cardTitle;
        cardTitle.tag = "h2";
        cardTitle.html = htmlEscape(card.title().empty() ? card.id() : card.title());
        others.blocks.push_back(std::move(cardTitle));
        for (const auto& field : card.fields()) {
            appendField(others, book, humanLabel(field.first),
                        core::FieldType::Text, field.second, library, imageBase);
        }
    }
    if (!others.blocks.empty()) book.chapters.push_back(std::move(others));

    return book;
}

} // namespace io
