// test_speller.cpp
// Tests du correcteur orthographique et du dictionnaire de synonymes
// (fin de la Phase 1) : lecture des règles d'affixes, développement des
// formes fléchies, vérification d'un texte, suggestions, thésaurus.
//
// Tout se joue sur un MINI-dictionnaire écrit ici même : le vrai
// dictionnaire français fait 20 Mo une fois développé, et un test qui en
// dépendrait ne dirait rien de clair en cas d'échec. La vérification
// contre les vraies données se fait à part, en lançant l'application.

#include "AffixDictionary.h"
#include "Speller.h"
#include "Thesaurus.h"
#include "Utf8.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool contains(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// Mini-dictionnaire d'essai, dans le format Hunspell réel : drapeaux sur
// deux caractères, un suffixe de pluriel conditionné, un suffixe de
// féminin, et un lemme qui n'est pas un mot à lui seul.
const char* const kAff =
    "# essai\n"
    "SET UTF-8\n"
    "FLAG long\n"
    "FULLSTRIP\n"
    "NEEDAFFIX ()\n"
    "FORBIDDENWORD {}\n"
    "REP 2\n"
    "REP ph f\n"
    "REP ai é\n"
    "\n"
    "SFX Pl Y 2\n"
    "SFX Pl 0 s [^sxz]\n"          // pluriel normal
    "SFX Pl 0 0 [sxz]\n"           // déjà au pluriel : inchangé
    "\n"
    "SFX Fe Y 2\n"
    "SFX Fe 0 e [^e]\n"            // féminin
    "SFX Fe eux euse [^s]eux\n"    // « heureux » -> « heureuse »
    "\n"
    "SFX Vb Y 3\n"
    "SFX Vb er ait [^c]er\n"       // imparfait
    "SFX Vb er ions [^c]er\n"
    "SFX Vb er é [^c]er\n";

const char* const kDic =
    "8\n"
    "chat/PlFe\n"
    "nez/Pl\n"
    "heureux/Fe\n"
    "chanter/Vb\n"
    "élise\n"
    "aine/()\n"          // NEEDAFFIX : « aine » seul n'est pas un mot
    "chatt/{}\n"         // forme explicitement fautive
    "arc-en-ciel\n";

core::Speller makeSpeller() {
    const io::AffixData affix = io::parseAff(kAff);
    core::Speller speller;
    speller.setForms(io::expandDictionary(kDic, affix));
    speller.setReplacements(affix.replacements);
    return speller;
}

} // namespace

int main() {
    std::cout << std::unitbuf;

    // --- Test 1 : lecture du fichier de règles ---
    {
        const io::AffixData affix = io::parseAff(kAff);
        assert(affix.flagLength == 2);           // « FLAG long »
        assert(affix.fullStrip);
        assert(affix.needAffix == "()");
        assert(affix.forbiddenWord == "{}");
        assert(affix.classes.size() == 3);
        assert(affix.classes.at("Pl").suffix);
        assert(affix.classes.at("Pl").crossProduct);
        assert(affix.classes.at("Pl").rules.size() == 2);
        assert(affix.classes.at("Vb").rules.size() == 3);
        // La première ligne REP est un décompte, pas une règle.
        assert(affix.replacements.size() == 2);
        assert(affix.replacements[0].first == "ph");

        // Drapeaux de deux caractères : « PlFe » vaut « Pl » puis « Fe ».
        const std::vector<std::string> flags = io::splitFlags("PlFe", 2);
        assert(flags.size() == 2 && flags[0] == "Pl" && flags[1] == "Fe");
        std::cout << "Test 1 OK (lecture des regles d'affixes)\n";
    }

    // --- Test 2 : moteur de conditions ---
    {
        // Classe négative : « chat » finit par « t », donc accepté.
        assert(io::conditionMatches("chat", "[^sxz]", true));
        assert(!io::conditionMatches("nez", "[^sxz]", true));
        assert(io::conditionMatches("nez", "[sxz]", true));
        // Plusieurs atomes, dont un point.
        assert(io::conditionMatches("chanter", "[^c]er", true));
        assert(!io::conditionMatches("placer", "[^c]er", true));
        assert(io::conditionMatches("heureux", "[^s]eux", true));
        // Condition vide : toujours vraie. Mot trop court : jamais.
        assert(io::conditionMatches("a", "", true));
        assert(!io::conditionMatches("a", "[^sxz]er", true));
        // Un préfixe regarde le DÉBUT du mot.
        assert(io::conditionMatches("chat", "c", false));
        assert(!io::conditionMatches("chat", "t", false));
        // Les accents comptent pour UN caractère, pas pour deux octets.
        assert(io::conditionMatches("élisé", "é", true));
        assert(!io::conditionMatches("élise", "é", true));
        std::cout << "Test 2 OK (moteur de conditions)\n";
    }

    // --- Test 3 : développement des formes ---
    {
        const io::AffixData affix = io::parseAff(kAff);
        const std::vector<std::string> forms = io::expandDictionary(kDic, affix);

        assert(contains(forms, "chat"));
        assert(contains(forms, "chats"));      // pluriel
        assert(contains(forms, "chate"));      // féminin (règle d'essai)
        assert(contains(forms, "nez"));        // invariable : règle « 0 0 »
        assert(contains(forms, "heureux"));
        assert(contains(forms, "heureuse"));   // retrait « eux » + ajout « euse »
        assert(contains(forms, "chanter"));
        assert(contains(forms, "chantait"));
        assert(contains(forms, "chantions"));
        assert(contains(forms, "chanté"));
        assert(contains(forms, "arc-en-ciel"));

        // NEEDAFFIX : le lemme seul n'entre pas dans le dictionnaire.
        assert(!contains(forms, "aine"));
        // FORBIDDENWORD : la forme est explicitement rejetée.
        assert(!contains(forms, "chatt"));
        // Ce qu'aucune règle ne produit ne doit pas apparaître.
        assert(!contains(forms, "nezs"));
        assert(!contains(forms, "chanterait")); // notre règle retire « er »

        // Résultat trié et sans doublon : la recherche binaire en dépend.
        for (size_t i = 1; i < forms.size(); ++i) assert(forms[i - 1] < forms[i]);
        std::cout << "Test 3 OK (developpement des formes flechies)\n";
    }

    // --- Test 4 : mots connus, casse, apostrophes, traits d'union ---
    {
        const core::Speller speller = makeSpeller();
        assert(speller.loaded());

        assert(speller.isKnown("chat"));
        assert(speller.isKnown("chats"));
        assert(!speller.isKnown("chatz"));

        // Casse : la majuscule initiale et le tout-majuscules passent.
        assert(speller.isKnown("Chat"));
        assert(speller.isKnown("CHAT"));
        assert(speller.isKnown("Élise"));
        assert(speller.isKnown("ÉLISE"));
        // Mais les accents ne sont JAMAIS ignorés : c'est le point d'un
        // correcteur français.
        assert(!speller.isKnown("elise"));

        // Composé : connu tel quel, ou par ses morceaux.
        assert(speller.isKnown("arc-en-ciel"));
        assert(speller.isKnown("chat-chats"));   // chaque morceau est connu
        assert(!speller.isKnown("chat-zzz"));

        // Ce qui contient un chiffre n'est pas du ressort du correcteur.
        assert(speller.isKnown("1er"));
        assert(speller.isKnown("2026"));
        assert(speller.isKnown("A4"));
        std::cout << "Test 4 OK (mots connus : casse, composes, chiffres)\n";
    }

    // --- Test 5 : vérification d'un texte et positions ---
    {
        const core::Speller speller = makeSpeller();

        const std::u32string texte = U"Le chat chantait. Un zzzz passa.";
        const std::vector<core::Speller::Misspelling> fautes = speller.check(texte);
        // « Le », « Un » et « passa » sont inconnus de ce mini-dictionnaire,
        // tout comme « zzzz » : on vérifie surtout les POSITIONS.
        bool trouve = false;
        for (const auto& faute : fautes) {
            if (faute.word == "zzzz") {
                trouve = true;
                assert(texte.substr(faute.start, faute.length) == U"zzzz");
            }
        }
        assert(trouve);

        // Positions en POINTS DE CODE, pas en octets : avec des accents
        // avant le mot fautif, un décompte en octets serait décalé.
        const std::u32string accents = U"Élise élise zzzz";
        const auto fautes2 = speller.check(accents);
        bool ok = false;
        for (const auto& faute : fautes2) {
            if (faute.word == "zzzz") {
                assert(faute.start == 12); // et non 14 (octets)
                ok = true;
            }
        }
        assert(ok);

        // L'apostrophe sépare, et la particule élidée n'est pas une faute.
        const auto elision = speller.check(U"l'chat d'chat qu'chat");
        assert(elision.empty());
        // Apostrophe courbe aussi.
        assert(speller.check(U"l’chat").empty());
        // Une particule qui n'en est pas une reste signalée.
        const auto fausse = speller.check(U"zz'chat");
        assert(fausse.size() == 1 && fausse[0].word == "zz");

        // Sans dictionnaire chargé, on ne signale RIEN (plutôt que de
        // souligner tout le manuscrit en rouge).
        core::Speller vide;
        assert(vide.check(U"n'importe quoi").empty());
        std::cout << "Test 5 OK (verification d'un texte, positions)\n";
    }

    // --- Test 6 : suggestions ---
    {
        const core::Speller speller = makeSpeller();

        // Une lettre en trop, en moins, inversée, ou fausse.
        assert(contains(speller.suggest("chatt"), "chat"));
        assert(contains(speller.suggest("cha"), "chat"));
        assert(contains(speller.suggest("chta"), "chat"));
        assert(contains(speller.suggest("chit"), "chat"));

        // La casse d'origine est rendue : corriger « Chatt » ne doit pas
        // faire perdre la majuscule.
        assert(contains(speller.suggest("Chatt"), "Chat"));

        // Les confusions déclarées par le dictionnaire (REP) sont
        // exploitées : « ph » -> « f », « ai » -> « é ».
        core::Speller repSpeller;
        repSpeller.setForms({"chanté", "fantôme"});
        repSpeller.setReplacements({{"ph", "f"}, {"ai", "é"}});
        assert(contains(repSpeller.suggest("phantôme"), "fantôme"));
        assert(contains(repSpeller.suggest("chantai"), "chanté"));

        // Un mot sans voisin ne produit rien, et jamais de plantage.
        assert(speller.suggest("zzzzzzzz").empty());
        assert(speller.suggest("").empty());
        // La limite demandée est respectée.
        assert(speller.suggest("chat", 2).size() <= 2);
        std::cout << "Test 6 OK (suggestions)\n";
    }

    // --- Test 7 : dictionnaire personnel et mots ignorés ---
    {
        core::Speller speller = makeSpeller();
        assert(!speller.isKnown("Ysalinde"));

        // Un nom inventé ajouté au dictionnaire personnel devient connu.
        assert(speller.addPersonal("Ysalinde"));
        assert(!speller.addPersonal("Ysalinde")); // déjà présent
        assert(speller.isKnown("Ysalinde"));
        assert(speller.isKnown("YSALINDE"));      // casse
        assert(speller.personal().size() == 1);

        // Il peut aussi être proposé en correction.
        assert(contains(speller.suggest("Ysalind"), "Ysalinde"));

        assert(speller.removePersonal("Ysalinde"));
        assert(!speller.isKnown("Ysalinde"));
        assert(!speller.removePersonal("Ysalinde"));

        // « Ignorer » vaut pour la session seulement.
        speller.ignoreWord("Ysalinde");
        assert(speller.isKnown("Ysalinde"));
        speller.clearIgnored();
        assert(!speller.isKnown("Ysalinde"));
        std::cout << "Test 7 OK (dictionnaire personnel, mots ignores)\n";
    }

    // --- Test 8 : thésaurus ---
    {
        const std::string contenu =
            "UTF-8\n"
            "roman|2\n"
            "(Nom)|récit|fiction|histoire\n"
            "(Adjectif)|romanesque\n"
            "forêt|1\n"
            "(Nom)|bois|futaie|sylve\n";

        io::Thesaurus thesaurus;
        assert(thesaurus.load(contenu));
        assert(thesaurus.entryCount() == 2);

        const auto sens = thesaurus.lookup("roman");
        assert(sens.size() == 2);
        assert(sens[0].label == "(Nom)");
        assert(sens[0].synonyms.size() == 3);
        assert(sens[0].synonyms[0] == "récit");
        assert(sens[1].label == "(Adjectif)");

        // Recherche insensible à la casse initiale, et accents respectés.
        assert(thesaurus.lookup("Roman").size() == 2);
        assert(thesaurus.lookup("forêt").size() == 1);
        assert(thesaurus.lookup("foret").empty());
        assert(thesaurus.lookup("inconnu").empty());
        assert(thesaurus.lookup("").empty());

        // Contenu invalide : refus propre, pas de plantage.
        io::Thesaurus mauvais;
        assert(!mauvais.load(""));
        assert(!mauvais.load("ISO8859-1\nmot|1\n(Nom)|autre\n")); // encodage refusé
        assert(!mauvais.loaded());

        // Fichier abîmé : on lit ce qui est lisible, on ignore le reste.
        io::Thesaurus abime;
        assert(abime.load("UTF-8\nbon|1\n(Nom)|valable\ncassé|3\n(Nom)|un\n"));
        assert(abime.lookup("bon").size() == 1);
        assert(abime.lookup("cassé").size() == 1); // annonce 3 sens, en donne 1
        std::cout << "Test 8 OK (thesaurus MyThes)\n";
    }

    std::cout << "Tous les tests du correcteur sont passes.\n";
    return 0;
}
