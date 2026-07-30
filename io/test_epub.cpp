// test_epub.cpp
// Tests de la structure « livre » (io/Book) et de l'export EPUB
// (io/Epub) : découpage en chapitres, images embarquées, contenu des
// fichiers de l'archive, échappement, déterminisme, écriture sur disque.

#include "Book.h"
#include "CardModel.h"
#include "Epub.h"
#include "TextModel.h"
#include "Zip.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

namespace {

uint16_t read16(const std::string& b, size_t pos) {
    return static_cast<uint16_t>(static_cast<unsigned char>(b[pos]) |
                                 (static_cast<unsigned char>(b[pos + 1]) << 8));
}

uint32_t read32(const std::string& b, size_t pos) {
    return static_cast<uint32_t>(static_cast<unsigned char>(b[pos])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(b[pos + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(b[pos + 2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(b[pos + 3])) << 24);
}

// Relit l'archive comme le ferait une liseuse : par l'annuaire central.
// Retourne le contenu de chaque entrée, et la liste des noms DANS L'ORDRE.
std::map<std::string, std::string> readZip(const std::string& zip,
                                           std::vector<std::string>* order = nullptr) {
    std::map<std::string, std::string> out;
    assert(zip.size() >= 22);
    const size_t eocd = zip.size() - 22;
    assert(read32(zip, eocd) == 0x06054B50);
    const uint16_t count = read16(zip, eocd + 10);
    size_t pos = read32(zip, eocd + 16);

    for (uint16_t i = 0; i < count; ++i) {
        assert(read32(zip, pos) == 0x02014B50);
        const uint32_t size = read32(zip, pos + 24);
        const uint16_t nameLen = read16(zip, pos + 28);
        const uint16_t extraLen = read16(zip, pos + 30);
        const uint16_t commentLen = read16(zip, pos + 32);
        const uint32_t localOffset = read32(zip, pos + 42);
        const std::string name = zip.substr(pos + 46, nameLen);

        assert(read32(zip, localOffset) == 0x04034B50);
        const size_t dataStart = localOffset + 30 + read16(zip, localOffset + 26) +
                                 read16(zip, localOffset + 28);
        out[name] = zip.substr(dataStart, size);
        if (order) order->push_back(name);
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return out;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Un PNG minimal mais authentique (signature + IHDR) : ce qu'il faut pour
// que la détection de type par les octets le reconnaisse.
std::string fakePng() {
    std::string png;
    const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (unsigned char c : signature) png += static_cast<char>(c);
    png += std::string(64, '\0'); // corps quelconque : on ne décode pas l'image
    return png;
}

core::TextBlock makeBlock(core::BlockType type, const std::u32string& text) {
    core::TextBlock block(type);
    block.insertText(0, text);
    return block;
}

std::filesystem::path tempFile(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

} // namespace

int main() {
    std::cout << std::unitbuf; // voir les tests passés même si l'un échoue

    // --- Test 1 : découpage du manuscrit en chapitres ---
    {
        core::Document doc;
        doc.metadata().title = "Le Roman";
        doc.metadata().author = "Thibaud";
        doc.blocks().push_back(makeBlock(core::BlockType::Paragraph, U"Avant-propos."));
        doc.blocks().push_back(makeBlock(core::BlockType::Heading1, U"Chapitre premier"));
        doc.blocks().push_back(makeBlock(core::BlockType::Paragraph, U"Il faisait nuit."));
        doc.blocks().push_back(makeBlock(core::BlockType::Paragraph, U"   ")); // vide
        doc.blocks().push_back(makeBlock(core::BlockType::Heading2, U"Une sous-partie"));
        doc.blocks().push_back(makeBlock(core::BlockType::Paragraph, U"Suite."));
        doc.blocks().push_back(makeBlock(core::BlockType::Heading1, U"Chapitre second"));
        doc.blocks().push_back(makeBlock(core::BlockType::Paragraph, U"Fin."));

        const io::Book book = io::bookFromDocument(doc);
        assert(book.meta.title == "Le Roman");
        assert(book.meta.author == "Thibaud");
        // 3 chapitres : l'ouverture (avant le premier H1) + les deux titres.
        assert(book.chapters.size() == 3);
        assert(book.chapters[0].title == "Le Roman");
        assert(book.chapters[1].title == "Chapitre premier");
        assert(book.chapters[2].title == "Chapitre second");
        // Le paragraphe vide n'est pas exporté ; la sous-partie reste DANS
        // son chapitre (elle ne coupe pas le livre).
        assert(book.chapters[1].blocks.size() == 4); // h1, p, h2, p
        assert(book.chapters[1].blocks[0].tag == "h1");
        assert(book.chapters[1].blocks[2].tag == "h2");
        assert(book.chapters[1].blocks[1].html == "Il faisait nuit.");
        std::cout << "Test 1 OK (decoupage en chapitres)\n";
    }

    // --- Test 2 : manuscrit sans titre de chapitre, et document vide ---
    {
        core::Document plain;
        plain.blocks().push_back(makeBlock(core::BlockType::Paragraph, U"Un seul bloc."));
        const io::Book book = io::bookFromDocument(plain);
        assert(book.chapters.size() == 1);
        assert(book.chapters[0].title == "Texte"); // pas de titre : repli

        core::Document empty;
        empty.blocks().push_back(makeBlock(core::BlockType::Paragraph, U""));
        const io::Book emptyBook = io::bookFromDocument(empty);
        assert(emptyBook.chapters.empty()); // rien à exporter, mais pas de plantage
        const std::string epub = io::buildEpub(emptyBook);
        assert(!epub.empty()); // l'EPUB reste valide (page de titre seule)
        std::cout << "Test 2 OK (sans chapitre / document vide)\n";
    }

    // --- Test 3 : couverture embarquée, fichiers refusés ---
    {
        const std::filesystem::path png = tempFile("test_epub_couverture.png");
        {
            std::ofstream out(png, std::ios::binary);
            const std::string data = fakePng();
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
        const std::filesystem::path fake = tempFile("test_epub_faux.png");
        {
            std::ofstream out(fake, std::ios::binary);
            out << "ceci n'est pas une image";
        }

        core::Document doc;
        doc.metadata().title = "Avec couverture";
        doc.metadata().coverPath = png.string();
        doc.blocks().push_back(makeBlock(core::BlockType::Heading1, U"Un"));

        io::Book book = io::bookFromDocument(doc);
        assert(!book.coverImageId.empty());
        assert(book.images.size() == 1);
        assert(book.images[0].mediaType == "image/png");
        assert(book.images[0].fileName == "img001.png");

        // Le MÊME fichier ajouté deux fois n'est pas dupliqué.
        assert(book.addImageFile(png) == book.images[0].id);
        assert(book.images.size() == 1);
        // Un fichier qui n'est pas une image, ou absent : refusé sans casse.
        assert(book.addImageFile(fake).empty());
        assert(book.addImageFile(tempFile("inexistant_zzz.png")).empty());
        assert(book.images.size() == 1);

        std::error_code ec;
        std::filesystem::remove(png, ec);
        std::filesystem::remove(fake, ec);
        std::cout << "Test 3 OK (couverture embarquee, fichiers refuses)\n";
    }

    // --- Test 4 : structure de l'archive EPUB ---
    {
        core::Document doc;
        doc.metadata().title = "Mon Roman";
        doc.metadata().author = "A. Auteur";
        doc.metadata().summary = "Une histoire.";
        doc.blocks().push_back(makeBlock(core::BlockType::Heading1, U"Chapitre 1"));
        doc.blocks().push_back(makeBlock(core::BlockType::Paragraph, U"Texte."));
        doc.blocks().push_back(makeBlock(core::BlockType::Heading1, U"Chapitre 2"));
        doc.blocks().push_back(makeBlock(core::BlockType::Paragraph, U"Suite."));

        const std::string epub = io::buildEpub(io::bookFromDocument(doc));
        std::vector<std::string> order;
        const auto files = readZip(epub, &order);

        // « mimetype » DOIT être la première entrée du fichier.
        assert(order[0] == "mimetype");
        assert(files.at("mimetype") == "application/epub+zip");
        assert(read16(epub, 8) == 0); // et rester non compressée

        assert(files.count("META-INF/container.xml") == 1);
        assert(files.count("OEBPS/content.opf") == 1);
        assert(files.count("OEBPS/nav.xhtml") == 1);
        assert(files.count("OEBPS/toc.ncx") == 1);
        assert(files.count("OEBPS/style.css") == 1);
        assert(files.count("OEBPS/text/titre.xhtml") == 1);
        assert(files.count("OEBPS/text/chap001.xhtml") == 1);
        assert(files.count("OEBPS/text/chap002.xhtml") == 1);

        // Le container désigne bien le fichier de package présent.
        assert(contains(files.at("META-INF/container.xml"), "OEBPS/content.opf"));

        const std::string& opf = files.at("OEBPS/content.opf");
        assert(contains(opf, "<dc:title>Mon Roman</dc:title>"));
        assert(contains(opf, "<dc:creator>A. Auteur</dc:creator>"));
        assert(contains(opf, "<dc:description>Une histoire.</dc:description>"));
        assert(contains(opf, "<dc:language>fr</dc:language>"));
        assert(contains(opf, "dcterms:modified"));
        // Tout ce qui est dans le spine est dans le manifeste.
        assert(contains(opf, "<item id=\"chap001\" href=\"text/chap001.xhtml\""));
        assert(contains(opf, "<itemref idref=\"chap001\"/>"));
        assert(contains(opf, "<itemref idref=\"chap002\"/>"));
        // Ordre de lecture : titre, sommaire, puis les chapitres.
        assert(opf.find("idref=\"titlepage\"") < opf.find("idref=\"chap001\""));
        assert(opf.find("idref=\"chap001\"") < opf.find("idref=\"chap002\""));

        const std::string& chap = files.at("OEBPS/text/chap001.xhtml");
        assert(contains(chap, "<?xml version=\"1.0\" encoding=\"utf-8\"?>"));
        assert(contains(chap, "xmlns=\"http://www.w3.org/1999/xhtml\""));
        assert(contains(chap, "<h1>Chapitre 1</h1>"));
        assert(contains(chap, "<p>Texte.</p>"));
        assert(contains(chap, "href=\"../style.css\""));

        // Navigation : un lien par chapitre.
        const std::string& nav = files.at("OEBPS/nav.xhtml");
        assert(contains(nav, "href=\"text/chap001.xhtml\">Chapitre 1</a>"));
        assert(contains(nav, "href=\"text/chap002.xhtml\">Chapitre 2</a>"));
        assert(contains(files.at("OEBPS/toc.ncx"), "playOrder=\"2\""));
        std::cout << "Test 4 OK (structure de l'archive EPUB)\n";
    }

    // --- Test 5 : styles du texte et échappement dans tout le livre ---
    {
        core::Document doc;
        doc.metadata().title = "Titre & <balise>";
        core::TextBlock heading(core::BlockType::Heading1);
        heading.insertText(0, U"Chapitre \"un\" & suite");
        doc.blocks().push_back(heading);
        core::TextBlock para(core::BlockType::Paragraph);
        para.insertText(0, U"gras <script>");
        para.applyStyle(0, 4, core::TextStyle::Bold);
        doc.blocks().push_back(para);

        const std::string epub = io::buildEpub(io::bookFromDocument(doc));
        const auto files = readZip(epub);
        const std::string& chap = files.at("OEBPS/text/chap001.xhtml");
        assert(contains(chap, "<strong>gras</strong>"));
        assert(contains(chap, "&lt;script&gt;"));
        assert(!contains(chap, "<script>"));
        assert(contains(chap, "Chapitre &quot;un&quot; &amp; suite"));
        // Le titre du livre est échappé partout où il apparaît.
        assert(contains(files.at("OEBPS/content.opf"), "Titre &amp; &lt;balise&gt;"));
        assert(!contains(files.at("OEBPS/nav.xhtml"), "<balise>"));

        // Balise de bloc hors de la liste autorisée : ramenée à <p>.
        // C'est le seul morceau non échappé de la sortie ; il ne doit pas
        // pouvoir devenir n'importe quoi.
        {
            io::Book forged;
            forged.meta.title = "Forge";
            io::BookChapter chapter;
            chapter.title = "Chapitre";
            chapter.blocks.push_back({"script onload=\"x\"", "contenu", ""});
            chapter.blocks.push_back({"h2", "vrai titre", ""});
            forged.chapters.push_back(chapter);
            const std::string page =
                readZip(io::buildEpub(forged)).at("OEBPS/text/chap001.xhtml");
            assert(contains(page, "<p>contenu</p>"));
            assert(!contains(page, "script"));
            assert(contains(page, "<h2>vrai titre</h2>")); // les balises légitimes passent
        }
        std::cout << "Test 5 OK (styles, echappement, balises filtrees)\n";
    }

    // --- Test 6 : identifiant stable et sortie déterministe ---
    {
        core::Document doc;
        doc.metadata().title = "Stable";
        doc.metadata().author = "Auteur";
        doc.blocks().push_back(makeBlock(core::BlockType::Heading1, U"Un"));

        const std::string a = io::buildEpub(io::bookFromDocument(doc));
        const std::string b = io::buildEpub(io::bookFromDocument(doc));
        assert(a == b); // deux exports identiques, octet pour octet

        const std::string id1 = io::uuidFromText("Stable\nAuteur");
        assert(id1.rfind("urn:uuid:", 0) == 0);
        assert(id1.size() == 9 + 36);
        assert(id1 == io::uuidFromText("Stable\nAuteur"));
        assert(id1 != io::uuidFromText("Autre\nAuteur"));
        assert(id1[9 + 14] == '4'); // UUID de version 4, syntaxiquement valide
        assert(contains(readZip(a).at("OEBPS/content.opf"), id1));

        // Options : sans page de titre ni sommaire, le livre n'a que ses
        // chapitres.
        io::EpubOptions bare;
        bare.titlePage = false;
        bare.tableOfContents = false;
        const auto files = readZip(io::buildEpub(io::bookFromDocument(doc), bare));
        assert(files.count("OEBPS/text/titre.xhtml") == 0);
        assert(files.count("OEBPS/text/sommaire.xhtml") == 0);
        assert(files.count("OEBPS/text/chap001.xhtml") == 1);
        std::cout << "Test 6 OK (identifiant stable, export deterministe)\n";
    }

    // --- Test 7 : bible narrative (fiches -> livre) ---
    {
        core::SchemaRegistry registry;
        core::CardLibrary library;

        core::Card elise("elise", "personnage");
        elise.setTitle("Élise");
        elise.addTag("héroïne");
        elise.editField("description_physique")
            .setScalar("Grande, brune.\nUne cicatrice.");
        elise.editField("relations").add("marc");
        library.put(elise);

        core::Card marc("marc", "personnage");
        marc.setTitle("Marc & fils");
        library.put(marc);

        core::Card foret("foret", "lieu");
        foret.setTitle("La Forêt");
        foret.editField("points_interet").add("La clairière");
        foret.editField("points_interet").add("Le vieux chêne");
        library.put(foret);

        // Fiche d'un type inconnu du registre : conservée à part.
        core::Card etrange("etrange", "type_inconnu");
        etrange.setTitle("Objet étrange");
        etrange.editField("note_libre").setScalar("Valeur");
        library.put(etrange);

        const io::Book book = io::bookFromCards(library, registry);
        assert(book.chapters.size() >= 3); // personnages, lieux, autres

        std::string all;
        for (const auto& chapter : book.chapters) {
            for (const auto& block : chapter.blocks) all += block.tag + ":" + block.html + "\n";
        }
        assert(contains(all, "h2:Élise"));
        assert(contains(all, "h2:Marc &amp; fils")); // échappé
        assert(contains(all, "Grande, brune."));
        assert(contains(all, "Une cicatrice."));     // 2e ligne = 2e paragraphe
        // Une référence est affichée par le TITRE de la fiche visée (et
        // non par son identifiant technique « marc »).
        assert(contains(all, "Relations :</strong> Marc &amp; fils"));
        assert(contains(all, "ul:<li>La clairière</li><li>Le vieux chêne</li>"));
        assert(contains(all, "Tags :</strong> héroïne"));
        // Le type inconnu n'est pas perdu.
        assert(contains(all, "Autres fiches"));
        assert(contains(all, "h2:Objet étrange"));
        assert(contains(all, "Note libre :</strong> Valeur"));

        const auto files = readZip(io::buildEpub(book));
        assert(files.count("OEBPS/text/chap001.xhtml") == 1);
        assert(contains(files.at("OEBPS/content.opf"), "<dc:title>Bible narrative"));
        std::cout << "Test 7 OK (bible narrative)\n";
    }

    // --- Test 8 : écriture sur disque ---
    {
        core::Document doc;
        doc.metadata().title = "Sur disque";
        doc.blocks().push_back(makeBlock(core::BlockType::Heading1, U"Un"));
        doc.blocks().push_back(makeBlock(core::BlockType::Paragraph, U"Contenu accentué : éàù"));

        const std::filesystem::path path = tempFile("test_epub_livre.epub");
        std::error_code ec;
        std::filesystem::remove(path, ec);

        std::string error;
        assert(io::writeEpub(io::bookFromDocument(doc), path, {}, &error));
        assert(error.empty());
        assert(std::filesystem::exists(path));

        std::string onDisk;
        {
            std::ifstream in(path, std::ios::binary);
            onDisk.assign((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
        }
        const auto files = readZip(onDisk);
        assert(contains(files.at("OEBPS/text/chap001.xhtml"), "Contenu accentué : éàù"));

        // Destination impossible : échec propre et message renseigné.
        std::string err2;
        assert(!io::writeEpub(io::bookFromDocument(doc), path / "sous" / "x.epub",
                              {}, &err2));
        assert(!err2.empty());

        std::filesystem::remove(path, ec);
        std::cout << "Test 8 OK (ecriture de l'EPUB sur disque)\n";
    }

    std::cout << "Tous les tests Epub sont passes.\n";
    return 0;
}
