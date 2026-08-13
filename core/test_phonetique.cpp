// test_phonetique.cpp
// Tests de la clé sonore (core/Phonetique).
//
// LA QUESTION À LAQUELLE CE FICHIER RÉPOND n'est pas « la clé est-elle
// une transcription phonétique correcte » — elle ne l'est pas, et ne
// cherche pas à l'être. C'est : « deux mots qu'un auteur risque de
// confondre reçoivent-ils la même clé, et deux mots qu'il ne confondrait
// jamais en reçoivent-ils de différentes ».
//
// Le premier groupe de tests est donc écrit à partir de FAUTES RÉELLES,
// celles qu'on fait en écrivant un mot comme on l'entend.

#include "Phonetique.h"

#include <cassert>
#include <iostream>
#include <string>

namespace {

std::string cle(const char* mot) { return core::phoneticKey(std::string(mot)); }

// Les deux mots sonnent-ils pareil ?
bool memeSon(const char* a, const char* b) {
    const std::string ka = cle(a);
    const std::string kb = cle(b);
    if (ka != kb) {
        std::cout << "  (« " << a << " » -> " << ka
                  << "  vs  « " << b << " » -> " << kb << ")\n";
    }
    return ka == kb && !ka.empty();
}

} // namespace

int main() {
    std::cout << std::unitbuf;

    // --- Test 1 : LE cas d'usage — écrire un mot comme on l'entend ---
    {
        // Chacune de ces paires est à DEUX opérations d'édition ou plus :
        // le correcteur actuel, qui s'arrête à une, ne les propose pas.
        assert(memeSon("fesais", "faisais"));
        assert(memeSon("ortografe", "orthographe"));
        assert(memeSon("filosofie", "philosophie"));
        assert(memeSon("cerize", "cerise"));
        assert(memeSon("randé", "rendait"));
        assert(memeSon("bocou", "beaucoup"));
        assert(memeSon("otomne", "automne"));

        // « boucou », en revanche, ne doit PAS être rapproché : « ou » et
        // « eau » ne sonnent pas pareil. Le filtre doit écarter ce qu'on
        // n'entend pas de la même façon, sinon il ne filtre rien.
        assert(!memeSon("boucou", "beaucoup"));
        std::cout << "Test 1 OK (fautes a l'oreille rattrapees)\n";
    }

    // --- Test 2 : ce que l'orthographe française n'entend pas ---
    {
        // Lettres muettes finales.
        assert(memeSon("chat", "cha"));
        assert(memeSon("petit", "peti"));
        assert(memeSon("nez", "né"));
        // Terminaisons verbales : toute la famille se rejoint, y compris
        // l'infinitif. C'est la faute française la plus répandue.
        assert(memeSon("parler", "parlé"));
        assert(memeSon("parlaient", "parlait"));
        assert(memeSon("parler", "parlait"));
        assert(memeSon("parlez", "parlé"));
        // Consonnes doubles.
        assert(memeSon("apeler", "appeler"));
        assert(memeSon("dificile", "difficile"));
        // Accents : ils ne s'entendent pas dans une clé sonore, alors que
        // le correcteur, lui, ne les ignore JAMAIS. Les deux cohabitent :
        // la clé sert à proposer, pas à valider.
        assert(memeSon("eleve", "élevé"));
        std::cout << "Test 2 OK (muettes, doubles, accents : meme son)\n";
    }

    // --- Test 3 : les groupes qui décident ---
    {
        assert(cle("eau") == cle("o"));
        assert(cle("chateau") == cle("chato"));
        assert(memeSon("kilo", "quilo"));
        assert(memeSon("sien", "cien"));
        // « gn » et « ch » sont des sons à part entière, notés par des
        // lettres majuscules pour ne pas se confondre avec « g »+« n ».
        assert(cle("gn") == "N");
        assert(cle("ch") == "C");
        std::cout << "Test 3 OK (groupes de lettres)\n";
    }

    // --- Test 4 : LES NASALES, notées par des chiffres ---
    {
        // Un chiffre ne peut pas entrer en collision avec une consonne :
        // « pain » et « pan » ne doivent surtout pas se confondre.
        assert(cle("pain") != cle("pan"));
        assert(cle("pain") != cle("pont"));
        assert(cle("un") != cle("on"));
        assert(memeSon("pain", "pin"));   // même nasale, même clé
        assert(memeSon("faim", "fin"));

        // Une nasale a besoin d'une consonne ou d'une fin après elle :
        // dans « année », « an » se prononce « a » puis « n ».
        assert(cle("annee") != cle("an"));
        // Le « e » muet final ne change rien : « ami » et « amie » sonnent
        // pareil, et c'est bien une confusion d'auteur.
        assert(memeSon("ami", "amie"));
        // « une » n'est pas nasal : le « n » est suivi d'une voyelle.
        assert(cle("une") != cle("un"));
        std::cout << "Test 4 OK (nasales distinctes des voyelles orales)\n";
    }

    // --- Test 5 : ce qui NE doit PAS se confondre ---
    {
        // Le filtre ne vaut que s'il écarte : une clé qui rapprocherait
        // tout de tout ne servirait à rien.
        const char* const distincts[][2] = {
            {"chat", "chien"}, {"maison", "oiseau"}, {"rouge", "route"},
            {"pierre", "pied"}, {"livre", "libre"}, {"grand", "gronde"},
        };
        for (const auto& paire : distincts) {
            assert(cle(paire[0]) != cle(paire[1]));
        }
        std::cout << "Test 5 OK (mots differents, cles differentes)\n";
    }

    // --- Test 6 : cas dégénérés ---
    {
        assert(cle("").empty());
        assert(cle("   ").empty());
        assert(cle("123").empty());
        assert(cle("!?,;").empty());
        // Une clé d'une seule lettre n'est jamais rognée jusqu'au vide.
        assert(!cle("a").empty());
        assert(!cle("e").empty());
        assert(!cle("et").empty());
        // Un mot très long ne fait pas déborder.
        assert(!cle(std::string(5000, 'a').c_str()).empty());
        // Les ligatures se déplient.
        assert(!cle("œuvre").empty());
        assert(cle("œuvre") == cle("oeuvre"));
        std::cout << "Test 6 OK (cas degeneres)\n";
    }

    // --- Test 7 : l'empreinte, telle que le correcteur la rangera ---
    {
        assert(core::phoneticHash(cle("faisais")) == core::phoneticHash(cle("fesais")));
        assert(core::phoneticHash(cle("chat")) != core::phoneticHash(cle("chien")));
        assert(core::phoneticHash("") == core::phoneticHash(""));
        std::cout << "Test 7 OK (empreinte stable)\n";
    }

    std::cout << "Tous les tests phonetiques sont passes.\n";
    return 0;
}
