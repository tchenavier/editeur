// test_utils.cpp
// Tests des fonctions transverses de la Phase 5 (jalon A) : empreinte
// SHA-256 et identifiants de projet.
//
// L'empreinte est vérifiée contre les VECTEURS DE RÉFÉRENCE officiels
// (FIPS 180-4). C'est le seul test qui vaille pour un algorithme
// cryptographique : une implémentation « qui a l'air de marcher » mais
// dont un décalage est faux produirait des empreintes stables, cohérentes
// entre elles… et fausses pour le reste du monde.

#include "Sha256.h"
#include "Uuid.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

int main() {
    std::cout << std::unitbuf;

    // --- Test 1 : vecteurs de référence FIPS 180-4 ---
    {
        assert(utils::sha256Hex("") ==
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        assert(utils::sha256Hex("abc") ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        assert(utils::sha256Hex(
                   "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
        // Message long (deux blocs de remplissage) : vecteur officiel.
        assert(utils::sha256Hex(
                   "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                   "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
               "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
        std::cout << "Test 1 OK (vecteurs de reference SHA-256)\n";
    }

    // --- Test 2 : longueur, casse et sensibilité au moindre octet ---
    {
        const std::string h = utils::sha256Hex("Élise");
        assert(h.size() == 64);
        for (char c : h) {
            assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        }
        // Un seul bit de différence doit tout changer.
        assert(utils::sha256Hex("Élise") != utils::sha256Hex("Elise"));
        assert(utils::sha256Hex("a") != utils::sha256Hex("b"));
        // Les octets nuls au milieu comptent (contenu binaire).
        assert(utils::sha256Hex(std::string("a\0b", 3)) !=
               utils::sha256Hex(std::string("ab", 2)));
        std::cout << "Test 2 OK (forme et sensibilite de l'empreinte)\n";
    }

    // --- Test 3 : calcul en flux == calcul d'un bloc ---
    {
        // La même donnée, poussée en une fois ou en morceaux de tailles
        // irrégulières, doit donner la même empreinte : c'est ce qui
        // permet d'empreinter un gros fichier sans le charger en mémoire.
        std::string data;
        for (int i = 0; i < 5000; ++i) data += static_cast<char>((i * 31) % 256);

        utils::Sha256 streamed;
        size_t offset = 0;
        size_t piece = 1;
        while (offset < data.size()) {
            const size_t take = (piece > data.size() - offset) ? data.size() - offset : piece;
            streamed.update(data.data() + offset, take);
            offset += take;
            piece = (piece * 3 + 7) % 512 + 1; // morceaux de tailles variées
        }
        assert(streamed.finalHex() == utils::sha256Hex(data));

        // Cas limites du remplissage : les tailles autour d'un bloc (64)
        // et autour de la frontière 56 sont celles qui cassent une
        // implémentation bancale.
        for (size_t n : {size_t(0), size_t(1), size_t(55), size_t(56), size_t(57),
                         size_t(63), size_t(64), size_t(65), size_t(119),
                         size_t(120), size_t(128)}) {
            const std::string message(n, 'x');
            utils::Sha256 h;
            h.update(message);
            assert(h.finalHex() == utils::sha256Hex(message));
        }
        std::cout << "Test 3 OK (calcul en flux, cas limites du remplissage)\n";
    }

    // --- Test 4 : empreinte d'un fichier ---
    {
        const auto path = std::filesystem::temp_directory_path() / "test_utils_sha.bin";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        std::string content;
        for (int i = 0; i < 200000; ++i) content += static_cast<char>(i % 256);
        {
            std::ofstream out(path, std::ios::binary);
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        bool ok = false;
        const std::string fileHash = utils::sha256File(path, &ok);
        assert(ok);
        // Un fichier de 200 000 octets lus par morceaux doit donner
        // exactement la même empreinte que la chaîne entière.
        assert(fileHash == utils::sha256Hex(content));

        // Fichier absent : échec propre, pas d'exception, pas d'empreinte
        // inventée (un inventaire ne doit pas s'arrêter pour autant).
        bool missingOk = true;
        assert(utils::sha256File(path.parent_path() / "absent_zzz.bin", &missingOk).empty());
        assert(!missingOk);

        std::filesystem::remove(path, ec);
        std::cout << "Test 4 OK (empreinte de fichier, fichier absent)\n";
    }

    // --- Test 5 : identifiants de projet ---
    {
        const std::string id = utils::newUuid();
        assert(id.size() == 36);
        assert(utils::isValidUuid(id));
        assert(id[8] == '-' && id[13] == '-' && id[18] == '-' && id[23] == '-');
        assert(id[14] == '4'); // version 4 (aléatoire)

        // Aucune collision sur un grand nombre de tirages : c'est toute
        // l'utilité de l'identifiant.
        std::set<std::string> seen;
        for (int i = 0; i < 20000; ++i) {
            const std::string generated = utils::newUuid();
            assert(utils::isValidUuid(generated));
            assert(seen.insert(generated).second); // jamais vu auparavant
        }

        // Validation : ce qui vient d'un fichier édité à la main.
        assert(!utils::isValidUuid(""));
        assert(!utils::isValidUuid("pas-un-identifiant"));
        assert(!utils::isValidUuid("550e8400-e29b-41d4-a716-44665544000"));   // trop court
        assert(!utils::isValidUuid("550e8400-e29b-41d4-a716-4466554400000")); // trop long
        assert(!utils::isValidUuid("550e8400xe29b-41d4-a716-446655440000"));  // séparateur
        assert(!utils::isValidUuid("550e8400-e29b-41d4-a716-44665544zzzz"));  // caractère
        assert(utils::isValidUuid("550E8400-E29B-41D4-A716-446655440000"));   // majuscules tolérées
        std::cout << "Test 5 OK (identifiants de projet)\n";
    }

    std::cout << "Tous les tests utils sont passes.\n";
    return 0;
}
