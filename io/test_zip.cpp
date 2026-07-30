// test_zip.cpp
// Tests de l'écrivain ZIP (io/Zip) : CRC, validation des noms, structure
// binaire de l'archive, bornes, écriture atomique sur disque.
//
// L'archive est relue ici par un petit lecteur d'annuaire central écrit
// pour le test : si nos décalages ou nos tailles étaient faux, il ne
// retrouverait pas les entrées. (La compatibilité avec un VRAI lecteur
// est vérifiée en plus, hors tests, en ouvrant le fichier produit.)

#include "Zip.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

struct ReadEntry {
    std::string name;
    std::string content;
    uint32_t    crc = 0;
    uint16_t    method = 0;
};

// Relit une archive en suivant l'annuaire central, comme le ferait un
// vrai extracteur. Retourne les entrées dans l'ordre de l'annuaire.
std::vector<ReadEntry> readZip(const std::string& zip) {
    std::vector<ReadEntry> out;
    assert(zip.size() >= 22);
    const size_t eocd = zip.size() - 22; // pas de commentaire d'archive
    assert(read32(zip, eocd) == 0x06054B50);

    const uint16_t count = read16(zip, eocd + 10);
    size_t pos = read32(zip, eocd + 16); // début de l'annuaire

    for (uint16_t i = 0; i < count; ++i) {
        assert(read32(zip, pos) == 0x02014B50);
        ReadEntry e;
        e.method = read16(zip, pos + 10);
        e.crc = read32(zip, pos + 16);
        const uint32_t size = read32(zip, pos + 24);
        const uint16_t nameLen = read16(zip, pos + 28);
        const uint16_t extraLen = read16(zip, pos + 30);
        const uint16_t commentLen = read16(zip, pos + 32);
        const uint32_t localOffset = read32(zip, pos + 42);
        e.name = zip.substr(pos + 46, nameLen);

        // Retrouve les données via l'en-tête local (c'est le chemin que
        // suit un extracteur : si le décalage est faux, tout casse).
        assert(read32(zip, localOffset) == 0x04034B50);
        const uint16_t localNameLen = read16(zip, localOffset + 26);
        const uint16_t localExtraLen = read16(zip, localOffset + 28);
        assert(localNameLen == nameLen);
        const size_t dataStart = localOffset + 30 + localNameLen + localExtraLen;
        e.content = zip.substr(dataStart, size);

        out.push_back(e);
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return out;
}

} // namespace

int main() {
    // Sortie non tamponnée : si une assertion échoue, on voit quand même
    // les tests déjà passés (le tampon serait perdu à l'arrêt brutal).
    std::cout << std::unitbuf;

    // --- Test 1 : CRC-32 sur des valeurs de référence ---
    {
        assert(io::ZipWriter::crc32("") == 0u);
        assert(io::ZipWriter::crc32("123456789") == 0xCBF43926u); // vecteur classique
        assert(io::ZipWriter::crc32("a") == 0xE8B7BE43u);
        // Deux contenus différents doivent donner des CRC différents.
        assert(io::ZipWriter::crc32("abc") != io::ZipWriter::crc32("abd"));
        std::cout << "Test 1 OK (CRC-32)\n";
    }

    // --- Test 2 : validation des noms d'entrées ---
    {
        assert(io::ZipWriter::isValidEntryName("mimetype"));
        assert(io::ZipWriter::isValidEntryName("OEBPS/text/chap01.xhtml"));
        assert(io::ZipWriter::isValidEntryName("images/couverture éé.png")); // UTF-8 accepté

        assert(!io::ZipWriter::isValidEntryName(""));
        assert(!io::ZipWriter::isValidEntryName("/absolu.txt"));
        assert(!io::ZipWriter::isValidEntryName("dossier/"));
        assert(!io::ZipWriter::isValidEntryName("..\\evasion.txt"));
        assert(!io::ZipWriter::isValidEntryName("../evasion.txt"));   // remontée
        assert(!io::ZipWriter::isValidEntryName("a/../../b.txt"));
        assert(!io::ZipWriter::isValidEntryName("C:/chemin.txt"));
        assert(!io::ZipWriter::isValidEntryName("a//b.txt"));
        assert(!io::ZipWriter::isValidEntryName(std::string("nul\0octet", 9)));
        std::cout << "Test 2 OK (noms d'entrees valides / refuses)\n";
    }

    // --- Test 3 : refus des doublons et des ajouts invalides ---
    {
        io::ZipWriter zip;
        assert(zip.addFile("a.txt", "contenu"));
        assert(!zip.addFile("a.txt", "autre"));      // doublon
        assert(!zip.addFile("../a.txt", "x"));       // nom refusé
        assert(zip.count() == 1);                    // rien n'a été ajouté
        assert(zip.contains("a.txt"));
        assert(!zip.contains("b.txt"));
        std::cout << "Test 3 OK (doublons et noms refuses)\n";
    }

    // --- Test 4 : structure de l'archive, ordre préservé, relecture ---
    {
        io::ZipWriter zip;
        assert(zip.addFile("mimetype", "application/epub+zip"));
        assert(zip.addFile("META-INF/container.xml", "<container/>"));
        assert(zip.addFile("OEBPS/text/chap01.xhtml", "<html>Chapitre 1</html>"));

        const std::string bytes = zip.build();
        // La toute première entrée commence à l'octet 0 (exigence EPUB
        // pour « mimetype »), en méthode stockée.
        assert(read32(bytes, 0) == 0x04034B50);
        assert(read16(bytes, 8) == 0);  // méthode 0 = stocké
        assert(bytes.compare(30, 8, "mimetype") == 0);

        const std::vector<ReadEntry> entries = readZip(bytes);
        assert(entries.size() == 3);
        assert(entries[0].name == "mimetype");
        assert(entries[0].content == "application/epub+zip");
        assert(entries[0].method == 0);
        assert(entries[0].crc == io::ZipWriter::crc32("application/epub+zip"));
        assert(entries[1].name == "META-INF/container.xml");
        assert(entries[2].content == "<html>Chapitre 1</html>");
        std::cout << "Test 4 OK (structure, ordre, relecture par l'annuaire)\n";
    }

    // --- Test 5 : contenu binaire quelconque (image) et entrée vide ---
    {
        std::string binary;
        for (int i = 0; i < 256; ++i) binary += static_cast<char>(i);
        binary += binary; // 512 octets couvrant toutes les valeurs

        io::ZipWriter zip;
        assert(zip.addFile("images/couverture.png", binary));
        assert(zip.addFile("vide.txt", ""));

        const std::vector<ReadEntry> entries = readZip(zip.build());
        assert(entries.size() == 2);
        assert(entries[0].content == binary);       // aucun octet altéré
        assert(entries[1].content.empty());
        assert(entries[1].crc == 0u);
        std::cout << "Test 5 OK (contenu binaire integral, entree vide)\n";
    }

    // --- Test 6 : bornes de taille (aucun champ ne peut déborder) ---
    {
        static_assert(io::ZipWriter::kMaxEntrySize <= io::ZipWriter::kMaxArchiveSize,
                      "une entree seule ne peut pas depasser l'archive");
        // Le format ZIP (sans ZIP64) code les tailles sur 32 bits : nos
        // bornes doivent rester nettement en dessous.
        static_assert(io::ZipWriter::kMaxArchiveSize < 0xFFFFFFFFu / 2,
                      "borne trop haute pour un champ 32 bits");

        io::ZipWriter zip;
        // Une entrée juste au-dessus de la borne est refusée, et l'écrivain
        // reste utilisable ensuite (rien n'a été modifié).
        const std::string tooBig(io::ZipWriter::kMaxEntrySize + 1, 'x');
        assert(!zip.addFile("enorme.bin", tooBig));
        assert(zip.count() == 0);
        assert(zip.addFile("normal.txt", "toujours utilisable"));
        assert(zip.count() == 1);
        std::cout << "Test 6 OK (borne de taille refusee proprement)\n";
    }

    // --- Test 7 : horodatage réglable, sortie déterministe ---
    {
        io::ZipWriter a, b;
        a.addFile("x.txt", "meme contenu");
        b.addFile("x.txt", "meme contenu");
        assert(a.build() == b.build()); // deux exports identiques, octet pour octet

        io::ZipWriter c;
        c.setTimestamp(2026, 7, 25, 14, 30, 0);
        c.addFile("x.txt", "meme contenu");
        assert(c.build() != a.build()); // la date figure bien dans le fichier
        assert(c.build().size() == a.build().size());

        // Date hors bornes MS-DOS : ramenée dans la plage, jamais tronquée
        // n'importe comment (l'archive reste lisible).
        io::ZipWriter d;
        d.setTimestamp(1900, 99, -5, 42, 61, 61);
        d.addFile("x.txt", "meme contenu");
        assert(d.build().size() == a.build().size());
        std::cout << "Test 7 OK (horodatage borne et deterministe)\n";
    }

    // --- Test 8 : écriture sur disque (atomique) et relecture ---
    {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "test_zip_editeur.zip";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        io::ZipWriter zip;
        zip.addFile("mimetype", "application/epub+zip");
        zip.addFile("OEBPS/text/chap01.xhtml", "<html>é à ù</html>");

        std::string error;
        assert(zip.writeToFile(path, &error));
        assert(error.empty());
        assert(std::filesystem::exists(path));
        // Aucun fichier temporaire ne doit rester derrière.
        std::filesystem::path tmp = path;
        tmp += ".tmp";
        assert(!std::filesystem::exists(tmp));

        // Lecture dans un bloc : le flux DOIT être refermé avant la
        // suppression du fichier (sous Windows, effacer un fichier encore
        // ouvert échoue).
        std::string onDisk;
        {
            std::ifstream in(path, std::ios::binary);
            onDisk.assign((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
        }
        assert(onDisk == zip.build());
        const std::vector<ReadEntry> entries = readZip(onDisk);
        assert(entries.size() == 2);
        assert(entries[1].content == "<html>é à ù</html>");

        // Chemin impossible : échec propre, message renseigné.
        std::string err2;
        assert(!zip.writeToFile(path / "sous" / "dossier" / "x.zip", &err2));
        assert(!err2.empty());

        std::filesystem::remove(path, ec);
        std::cout << "Test 8 OK (ecriture atomique sur disque)\n";
    }

    // --- Test 9 : borne du NOMBRE d'entrées (champ 16 bits du format) ---
    {
        io::ZipWriter zip;
        for (size_t i = 0; i < io::ZipWriter::kMaxEntries; ++i) {
            assert(zip.addFile("f" + std::to_string(i), ""));
        }
        assert(zip.count() == io::ZipWriter::kMaxEntries);
        // L'entrée de trop est refusée : sans elle, le compteur d'entrées
        // de l'archive déborderait sur 16 bits et le fichier produit
        // serait illisible — une corruption SILENCIEUSE.
        assert(!zip.addFile("une_de_trop", ""));
        assert(zip.count() == io::ZipWriter::kMaxEntries);

        // Le compteur relu dans l'archive vaut exactement le nombre
        // d'entrées : aucune troncature.
        const std::string bytes = zip.build();
        const size_t eocd = bytes.size() - 22;
        assert(read32(bytes, eocd) == 0x06054B50);
        assert(read16(bytes, eocd + 10) == 0xFFFF); // 65535
        std::cout << "Test 9 OK (borne du nombre d'entrees)\n";
    }

    std::cout << "Tous les tests Zip sont passes.\n";
    return 0;
}
