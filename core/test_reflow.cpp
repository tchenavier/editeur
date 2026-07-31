// test_reflow.cpp
// Tests du redécoupage d'un texte aplati (core/Reflow).
//
// LE SCÉNARIO RÉEL, celui qui a fait naître ce module : un correcteur
// extérieur rend le manuscrit corrigé d'un seul tenant, toute la structure
// perdue. La version d'avant, elle, l'a encore. Ce qu'on vérifie ici :
// que le texte corrigé retrouve EXACTEMENT les paragraphes de l'autre,
// titres compris, sans qu'un seul mot de la correction soit perdu.

#include "Reflow.h"
#include "Utf8.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::u32string u32(const char* utf8) { return utils::utf8ToU32(utf8); }

core::TextBlock make(core::BlockType type, const char* text) {
    core::TextBlock block(type);
    block.insertText(0, u32(text));
    return block;
}

core::TextBlock para(const char* text) { return make(core::BlockType::Paragraph, text); }

std::string textOf(const core::TextBlock& block) { return utils::u32ToUtf8(block.text()); }

// La version de référence : celle qui porte la structure voulue.
std::vector<core::TextBlock> reference() {
    return {
        make(core::BlockType::Heading1, "Acte I : Le Destin de Cendres"),
        para("Le vent hurlait contre les remparts. Regina veillait."),
        para("Elle rêvait de forêts. Personne ne le savait."),
        para("— Je pars, dit-elle."),
    };
}

} // namespace

int main() {
    std::cout << std::unitbuf;

    // --- Test 1 : repérage des phrases et des mots ---
    {
        const auto blocs = reference();
        const auto phrases = core::documentSentences(blocs);
        // Le titre compte pour une phrase, les deux paragraphes pour deux
        // chacun, la réplique pour une : cinq en tout... soit six.
        assert(phrases.size() == 6);
        assert(core::refText(blocs, phrases[0]) == u32("Acte I : Le Destin de Cendres"));
        assert(core::refText(blocs, phrases[2]) == u32("Regina veillait."));
        assert(phrases[2].blockIndex == 1);

        const auto mots = core::documentWords(blocs);
        assert(!mots.empty());
        assert(core::refText(blocs, mots[0]) == u32("Acte"));

        // Un document vide ne produit rien, et ne plante pas.
        assert(core::documentSentences({}).empty());
        assert(core::documentWords({}).empty());
        std::cout << "Test 1 OK (reperage des phrases et des mots)\n";
    }

    // --- Test 2 : mesure de platitude ---
    {
        // Un manuscrit découpé : chaque bloc ne porte qu'une ou deux phrases.
        assert(core::flatness(reference()) == 0.0);

        // Le même texte rendu d'un seul tenant : tout dans un bloc.
        std::vector<core::TextBlock> aplati = {
            para("Acte I : Le Destin de Cendres Le vent hurlait contre les remparts. "
                 "Regina veillait. Elle rêvait de forêts. Personne ne le savait. "
                 "— Je pars, dit-elle."),
        };
        assert(core::flatness(aplati) == 1.0);

        // Un texte trop court ne dit rien : la mesure s'abstient.
        assert(core::flatness({para("Il faisait nuit. Elle sortit.")}) == 0.0);
        assert(core::flatness({}) == 0.0);
        std::cout << "Test 2 OK (mesure de platitude)\n";
    }

    // --- Test 3 : LE cas d'usage — rendre ses paragraphes au texte corrigé ---
    {
        // Version corrigée, rendue d'un seul tenant. Deux corrections y
        // ont été apportées : « hurlait » -> « soufflait », « rêvait » ->
        // « songeait ». Le titre est fondu dans la masse, sans ponctuation
        // qui le sépare de la suite : aucun découpage en phrases ne
        // pourrait le retrouver.
        const std::vector<core::TextBlock> aplati = {
            para("Acte I : Le Destin de Cendres Le vent soufflait contre les remparts. "
                 "Regina veillait. Elle songeait à des forêts. Personne ne le savait. "
                 "— Je pars, dit-elle."),
        };

        const auto recoupe = core::reflowLike(aplati, reference());

        assert(recoupe.size() == 4);
        // Le titre a retrouvé son NIVEAU, pas seulement sa place.
        assert(recoupe[0].type() == core::BlockType::Heading1);
        assert(textOf(recoupe[0]) == "Acte I : Le Destin de Cendres");

        // Les corrections sont intactes : on recoupe, on ne réécrit pas.
        assert(recoupe[1].type() == core::BlockType::Paragraph);
        assert(textOf(recoupe[1]) == "Le vent soufflait contre les remparts. Regina veillait.");
        assert(textOf(recoupe[2]) == "Elle songeait à des forêts. Personne ne le savait.");

        // Le tiret de dialogue appartient à la réplique, pas à ce qui précède.
        assert(textOf(recoupe[3]) == "— Je pars, dit-elle.");
        std::cout << "Test 3 OK (paragraphes et titres rendus, corrections intactes)\n";
    }

    // --- Test 4 : rien ne se perd ---
    {
        const std::vector<core::TextBlock> aplati = {
            para("Acte I : Le Destin de Cendres Le vent soufflait contre les remparts. "
                 "Regina veillait. Elle songeait à des forêts. Personne ne le savait. "
                 "— Je pars, dit-elle."),
        };
        const auto recoupe = core::reflowLike(aplati, reference());

        // Tous les mots du texte aplati se retrouvent, dans l'ordre : la
        // seule chose que le redécoupage supprime, ce sont les espaces
        // devenus changements de paragraphe.
        const auto avant = core::documentWords(aplati);
        const auto apres = core::documentWords(recoupe);
        assert(avant.size() == apres.size());
        for (size_t i = 0; i < avant.size(); ++i) {
            assert(core::refText(aplati, avant[i]) == core::refText(recoupe, apres[i]));
        }
        std::cout << "Test 4 OK (aucun mot perdu ni deplace)\n";
    }

    // --- Test 5 : les styles suivent leur texte ---
    {
        // « soufflait » est en italique dans la version aplatie ; il doit
        // le rester après le redécoupage, à sa nouvelle position.
        core::TextBlock bloc = para("Acte I : Le Destin de Cendres Le vent soufflait contre "
                                    "les remparts. Regina veillait. Elle rêvait de forêts. "
                                    "Personne ne le savait. — Je pars, dit-elle.");
        const std::u32string mot = u32("soufflait");
        const size_t position = bloc.text().find(mot);
        assert(position != std::u32string::npos);
        bloc.applyStyle(position, mot.size(), core::TextStyle::Italic);

        const auto recoupe = core::reflowLike({bloc}, reference());
        assert(recoupe.size() == 4);

        const size_t nouvelle = recoupe[1].text().find(mot);
        assert(nouvelle != std::u32string::npos);
        assert(recoupe[1].styleAt(nouvelle) == core::TextStyle::Italic);
        assert(recoupe[1].styleAt(nouvelle + mot.size() - 1) == core::TextStyle::Italic);
        // Et rien autour.
        assert(recoupe[1].styleAt(nouvelle - 1) == core::TextStyle::None);
        assert(recoupe[1].styleAt(nouvelle + mot.size()) == core::TextStyle::None);
        std::cout << "Test 5 OK (styles conserves a leur nouvelle place)\n";
    }

    // --- Test 6 : refuser d'inventer ---
    {
        const auto blocs = reference();
        // Aucun mot commun : on ne touche à rien plutôt que de produire un
        // découpage arbitraire.
        const std::vector<core::TextBlock> etranger = {para("zzz yyy xxx www vvv uuu.")};
        const auto inchange = core::reflowLike(etranger, blocs);
        assert(inchange.size() == 1);
        assert(textOf(inchange[0]) == "zzz yyy xxx www vvv uuu.");

        // Un texte qui ne se raccroche qu'à moitié : sous le seuil, on
        // rend le texte tel quel plutôt qu'un découpage deviné.
        const std::vector<core::TextBlock> moitie = {
            para("Le vent hurlait contre les remparts. Regina veillait. "
                 "zzz yyy xxx www vvv uuu ttt sss rrr qqq ppp ooo nnn mmm lll.")};
        const auto refuse = core::reflowLike(moitie, blocs);
        assert(refuse.size() == 1);

        // Références vides des deux côtés : aucun plantage.
        assert(core::reflowLike({}, blocs).empty());
        assert(core::reflowLike(etranger, {}).size() == 1);
        assert(core::reflowLike({}, {}).empty());

        // Un texte déjà découpé passé au redécoupage rend la même chose.
        const auto idem = core::reflowLike(blocs, blocs);
        assert(idem.size() == blocs.size());
        for (size_t i = 0; i < idem.size(); ++i) {
            assert(textOf(idem[i]) == textOf(blocs[i]));
            assert(idem[i].type() == blocs[i].type());
        }
        std::cout << "Test 6 OK (aucun decoupage invente)\n";
    }

    // --- Test 7 : passage à l'échelle d'un roman ---
    {
        // Deux cents paragraphes de référence, le même texte aplati d'un
        // seul tenant : c'est la taille où un alignement naïf s'effondre.
        std::vector<core::TextBlock> structure;
        std::u32string tout;
        for (int i = 0; i < 200; ++i) {
            const std::u32string numero = u32(std::to_string(i).c_str());
            const std::u32string texte =
                u32("Le personnage numero ") + numero +
                u32(" traversa la plaine. Il ne dit rien de plus.");
            core::TextBlock bloc(core::BlockType::Paragraph);
            bloc.insertText(0, texte);
            structure.push_back(bloc);
            if (!tout.empty()) tout += U" ";
            tout += texte;
        }
        core::TextBlock aplati(core::BlockType::Paragraph);
        aplati.insertText(0, tout);

        const auto recoupe = core::reflowLike({aplati}, structure);
        assert(recoupe.size() == structure.size());
        for (size_t i = 0; i < recoupe.size(); ++i) {
            assert(textOf(recoupe[i]) == textOf(structure[i]));
        }
        std::cout << "Test 7 OK (200 paragraphes retrouves)\n";
    }

    std::cout << "Tous les tests de redecoupage sont passes.\n";
    return 0;
}
